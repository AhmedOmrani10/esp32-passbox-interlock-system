#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

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

// Keep track of last LCD message to avoid flicker
String lastMsg = "";

// ----- LCD Helper -----
void lcdMsg(String msg) {
    if (!cycle_running && msg != lastMsg) { // only update if cycle not running
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(msg);
        lastMsg = msg;
    }
}

// ----- Door Logic -----
void openContaminated() {
    if (door_sterile) {
        lcdMsg("BLOCK: Sterile OPEN");
        return;
    }
    door_cont = true;
    lcdMsg("Contamin. OPEN");
}

void closeContaminated() {
    door_cont = false;
    lcdMsg("Contamin. CLOSED");
}

void openSterile() {
    if (door_cont) {
        lcdMsg("BLOCK: Contam OPEN");
        return;
    }
    door_sterile = true;
    lcdMsg("Sterile OPEN");
}

void closeSterile() {
    door_sterile = false;
    lcdMsg("Sterile CLOSED");
}

// ----- Emergency Stop -----
void emergencyStop() {
    emergency = true;
    cycle_running = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ARRET URGENCE");
}

// ----- Non-blocking wait with countdown -----
bool waitStep(String stepName, unsigned long durationSec) {
    unsigned long start = millis();
    unsigned long durationMs = durationSec * 1000;

    while (millis() - start < durationMs) {
        if (digitalRead(BTN_EMERGENCY) == HIGH) {
            emergencyStop();
            return false; // interrupted
        }

        // Update countdown every 200ms
        unsigned long elapsed = (millis() - start) / 1000;
        unsigned long remaining = durationSec - elapsed;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(stepName);
        lcd.setCursor(0, 1);
        lcd.print("Time: ");
        lcd.print(remaining);
        lcd.print(" s");

        // Allow door control during cycle but do NOT update LCD
        if (digitalRead(BTN_OPEN_CONT) == HIGH)   door_cont = !door_sterile ? true : door_cont;
        if (digitalRead(BTN_CLOSE_CONT) == HIGH)  door_cont = false;
        if (digitalRead(BTN_OPEN_STERILE) == HIGH) door_sterile = !door_cont ? true : door_sterile;
        if (digitalRead(BTN_CLOSE_STERILE) == HIGH) door_sterile = false;

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

    if (!waitStep("Extraction air", 3)) return;
    if (!waitStep("Arret de l'air", 2)) return;
    if (!waitStep("Injection produit", 2)) return;
    if (!waitStep("Pause sterilisation", 20)) return; // 20s for test
    if (!waitStep("Extraction produit", 3)) return;
    if (!waitStep("Air propre", 3)) return;

    cycle_running = false;
    lcdMsg("Cycle termine");
}

void setup() {
    lcd.init();
    lcd.backlight();
    lcdMsg("System Ready");

    pinMode(BTN_OPEN_CONT, INPUT);
    pinMode(BTN_CLOSE_CONT, INPUT);
    pinMode(BTN_OPEN_STERILE, INPUT);
    pinMode(BTN_CLOSE_STERILE, INPUT);
    pinMode(BTN_START, INPUT);
    pinMode(BTN_EMERGENCY, INPUT);
}

void loop() {
    if (digitalRead(BTN_EMERGENCY) == HIGH) emergencyStop();
    if (emergency) return;

    if (!cycle_running) { // door logic only updates LCD if cycle not running
        if (digitalRead(BTN_OPEN_CONT) == HIGH)   openContaminated();
        if (digitalRead(BTN_CLOSE_CONT) == HIGH)  closeContaminated();
        if (digitalRead(BTN_OPEN_STERILE) == HIGH) openSterile();
        if (digitalRead(BTN_CLOSE_STERILE) == HIGH) closeSterile();
    } else {
        // door logic updates state silently
        if (digitalRead(BTN_OPEN_CONT) == HIGH && !door_sterile) door_cont = true;
        if (digitalRead(BTN_CLOSE_CONT) == HIGH) door_cont = false;
        if (digitalRead(BTN_OPEN_STERILE) == HIGH && !door_cont) door_sterile = true;
        if (digitalRead(BTN_CLOSE_STERILE) == HIGH) door_sterile = false;
    }

    if (digitalRead(BTN_START) == HIGH && !cycle_running) startCycle();

    delay(100);
}
