#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ----- WiFi Credentials -----
const char* ssid     = "Wokwi-GUEST";         // The SSID (name) of the Wi-Fi network you want to connect to
const char* password = ""; // Replace with your password

// ----- MQTT Configuration -----
const char* mqtt_broker = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_client_id = "ESP32_Autoclave_001"; // Unique ID

// ----- MQTT Topics -----
const char* topic_porte_contaminee = "porte/contaminee";
const char* topic_porte_sterile = "porte/sterile";
const char* topic_cycle_depart = "cycle/depart";
const char* topic_cycle_etape = "cycle/etape";
const char* topic_urgence = "urgence";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ----- Pins -----
#define BTN_OPEN_CONT      4
#define BTN_CLOSE_CONT     5
#define BTN_OPEN_STERILE   18
#define BTN_CLOSE_STERILE  19
#define BTN_START          25
#define BTN_EMERGENCY      33

// ----- States -----
bool door_cont = false;
bool door_sterile = false;
bool cycle_running = false;
bool emergency = false;
bool cycle_finished = false;

// ----- Debouncing variables -----
unsigned long lastBtnOpenCont = 0;
unsigned long lastBtnCloseCont = 0;
unsigned long lastBtnOpenSterile = 0;
unsigned long lastBtnCloseSterile = 0;
unsigned long lastBtnStart = 0;
unsigned long lastBtnEmergency = 0;
unsigned long debounceDelay = 250;

bool prevBtnOpenCont = LOW;
bool prevBtnCloseCont = LOW;
bool prevBtnOpenSterile = LOW;
bool prevBtnCloseSterile = LOW;
bool prevBtnStart = LOW;
bool prevBtnEmergency = LOW;

// ----- WiFi Connection -----
void connectWiFi() {
    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 20) {
        delay(500);
        Serial.print(".");
        attempt++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi Failed!");
    }
}

// ----- MQTT Connection -----
void connectMQTT() {
    while (!mqttClient.connected()) {
        Serial.println("Connecting to MQTT...");
        
        if (mqttClient.connect(mqtt_client_id)) {
            Serial.println("MQTT Connected!");
            
            // Publish initial states
            mqttClient.publish(topic_porte_contaminee, "OFF");
            mqttClient.publish(topic_porte_sterile, "OFF");
            mqttClient.publish(topic_cycle_depart, "OFF");
            mqttClient.publish(topic_urgence, "OFF");
            
        } else {
            Serial.print("MQTT Failed! Code: ");
            Serial.println(mqttClient.state());
            delay(5000);
        }
    }
}

// ----- MQTT Publish Helper -----
void publishMQTT(const char* topic, const char* message) {
    if (!mqttClient.connected()) {
        Serial.println("ERROR: MQTT not connected!");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("MQTT ERROR");
        lcd.setCursor(0, 1);
        lcd.print("Not connected");
        delay(2000);
        return;
    }
    
    bool success = mqttClient.publish(topic, message);
    
    if (success) {
        Serial.print("MQTT → ");
        Serial.print(topic);
        Serial.print(": ");
        Serial.println(message);
    } else {
        Serial.print("MQTT PUBLISH FAILED → ");
        Serial.print(topic);
        Serial.print(": ");
        Serial.println(message);
        
        
    }
}

// ----- Debounced button read with edge detection -----
bool readButtonDebounced(int pin, unsigned long &lastTime, bool &prevState) {
    unsigned long now = millis();
    bool currentState = digitalRead(pin);
    
    if (currentState == HIGH && prevState == LOW) {
        if (now - lastTime > debounceDelay) {
            lastTime = now;
            prevState = currentState;
            return true;
        }
    }
    
    prevState = currentState;
    return false;
}

// ----- LCD Helper -----
void lcdMsg(String msg) {
    if (!cycle_running) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(msg);
    }
}

// ----- Door Logic -----
void openContaminated() {
    if (door_sterile || cycle_running || (cycle_finished && !door_sterile)) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("ERREUR: CONTAM");
        lcd.setCursor(0, 1);
        lcd.print("DOOR LOCKED");
        return;
    }
    door_cont = true;
    lcdMsg("Contamin. OPEN");
    publishMQTT(topic_porte_contaminee, "ON");
}

void closeContaminated() {
    door_cont = false;
    lcdMsg("Contamin. CLOSED");
    publishMQTT(topic_porte_contaminee, "OFF");
}

void openSterile() {
    if (door_cont) {
        lcdMsg("ERROR: Contam OPEN");
        return;
    }
    door_sterile = true;
    lcdMsg("Sterile OPEN");
    publishMQTT(topic_porte_sterile, "ON");
}

void closeSterile() {
    door_sterile = false;
    lcdMsg("Sterile CLOSED");
    publishMQTT(topic_porte_sterile, "OFF");
    
    if (cycle_finished) {
        cycle_finished = false;
    }
}

// ----- Emergency Stop -----
void emergencyStop() {
    emergency = true;
    cycle_running = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ARRET URGENCE");
    
    publishMQTT(topic_urgence, "ON");
    publishMQTT(topic_cycle_depart, "OFF");
}

// ----- Non-blocking wait with countdown -----
bool waitStep(String stepName, unsigned long durationSec) {
    unsigned long start = millis();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(stepName);
    
    // Publish step to MQTT
    publishMQTT(topic_cycle_etape, stepName.c_str());

    while (millis() - start < durationSec * 1000) {
        // Maintain MQTT connection
        mqttClient.loop();
        
        if (readButtonDebounced(BTN_EMERGENCY, lastBtnEmergency, prevBtnEmergency)) {
            emergencyStop();
            return false;
        }

        unsigned long elapsed = (millis() - start) / 1000;
        unsigned long remaining = durationSec - elapsed;
        lcd.setCursor(0, 1);
        lcd.print("Time: ");
        lcd.print(remaining);
        lcd.print(" s   ");

        // Silent door control during cycle
        if (readButtonDebounced(BTN_OPEN_CONT, lastBtnOpenCont, prevBtnOpenCont) && !door_sterile) {
            door_cont = true;
            publishMQTT(topic_porte_contaminee, "ON");
        }
        if (readButtonDebounced(BTN_CLOSE_CONT, lastBtnCloseCont, prevBtnCloseCont)) {
            door_cont = false;
            publishMQTT(topic_porte_contaminee, "OFF");
        }
        if (readButtonDebounced(BTN_OPEN_STERILE, lastBtnOpenSterile, prevBtnOpenSterile) && !door_cont) {
            door_sterile = true;
            publishMQTT(topic_porte_sterile, "ON");
        }
        if (readButtonDebounced(BTN_CLOSE_STERILE, lastBtnCloseSterile, prevBtnCloseSterile)) {
            door_sterile = false;
            publishMQTT(topic_porte_sterile, "OFF");
        }

        delay(200);
    }
    return true;
}

// ----- Decontamination Cycle -----
void startCycle() {
    if (door_cont || door_sterile) {
        lcdMsg("Close doors first!");
        return;
    }

    cycle_running = true;
    publishMQTT(topic_cycle_depart, "ON");

    if (!waitStep("Extraction air", 3)) return;
    if (!waitStep("Arret de l'air", 2)) return;
    if (!waitStep("Injection produit", 2)) return;
    if (!waitStep("Pause sterilisation", 20)) return;
    if (!waitStep("Extraction produit", 3)) return;
    if (!waitStep("Air propre", 3)) return;

    cycle_running = false;
    cycle_finished = true;

    lcdMsg("Cycle termine");
    publishMQTT(topic_cycle_etape, "Cycle termine");
    publishMQTT(topic_cycle_depart, "OFF");
}

// ----- Setup -----
void setup() {
    Serial.begin(115200);
    
    // LCD Init
    lcd.init();
    lcd.backlight();
    delay(200);
    lcdMsg("Connecting WiFi..");
    
    // WiFi & MQTT
    connectWiFi();
    mqttClient.setServer(mqtt_broker, mqtt_port);
    connectMQTT();
    
    lcdMsg("System Ready");
    delay(500);

    // Pin Setup
    pinMode(BTN_OPEN_CONT, INPUT);
    pinMode(BTN_CLOSE_CONT, INPUT);
    pinMode(BTN_OPEN_STERILE, INPUT);
    pinMode(BTN_CLOSE_STERILE, INPUT);
    pinMode(BTN_START, INPUT);
    pinMode(BTN_EMERGENCY, INPUT);
}

// ----- Main Loop -----
void loop() {
    // Maintain MQTT connection
    if (!mqttClient.connected()) {
        connectMQTT();
    }
    mqttClient.loop();
    
    // Emergency button
    if (readButtonDebounced(BTN_EMERGENCY, lastBtnEmergency, prevBtnEmergency)) {
        emergencyStop();
    }
    if (emergency) return;

    if (!cycle_running) {
        if (readButtonDebounced(BTN_OPEN_CONT, lastBtnOpenCont, prevBtnOpenCont))   
            openContaminated();
        if (readButtonDebounced(BTN_CLOSE_CONT, lastBtnCloseCont, prevBtnCloseCont))  
            closeContaminated();
        if (readButtonDebounced(BTN_OPEN_STERILE, lastBtnOpenSterile, prevBtnOpenSterile)) 
            openSterile();
        if (readButtonDebounced(BTN_CLOSE_STERILE, lastBtnCloseSterile, prevBtnCloseSterile)) 
            closeSterile();
    } else {
        // Silent door control during cycle
        if (readButtonDebounced(BTN_OPEN_CONT, lastBtnOpenCont, prevBtnOpenCont) && !door_sterile) {
            door_cont = true;
            publishMQTT(topic_porte_contaminee, "ON");
        }
        if (readButtonDebounced(BTN_CLOSE_CONT, lastBtnCloseCont, prevBtnCloseCont)) {
            door_cont = false;
            publishMQTT(topic_porte_contaminee, "OFF");
        }
        if (readButtonDebounced(BTN_OPEN_STERILE, lastBtnOpenSterile, prevBtnOpenSterile) && !door_cont) {
            door_sterile = true;
            publishMQTT(topic_porte_sterile, "ON");
        }
        if (readButtonDebounced(BTN_CLOSE_STERILE, lastBtnCloseSterile, prevBtnCloseSterile)) {
            door_sterile = false;
            publishMQTT(topic_porte_sterile, "OFF");
        }
    }

    if (readButtonDebounced(BTN_START, lastBtnStart, prevBtnStart) && !cycle_running) {
        startCycle();
    }

    delay(100);
}