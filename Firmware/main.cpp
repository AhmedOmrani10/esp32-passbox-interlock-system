#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ----- Pins -----
#define BTN_OPEN_CONT      4
#define BTN_CLOSE_CONT     5
#define BTN_OPEN_STERILE   18
#define BTN_CLOSE_STERILE  19

// ----- States -----
bool door_cont = false;
bool door_sterile = false;

// Keep track of last LCD message to avoid flicker
String lastMsg = "";

// ----- LCD Helper -----
void lcdMsg(String msg) {
    if (msg != lastMsg) {      // Only update if message changed
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

void setup() {
    lcd.init();
    lcd.backlight();
    lcdMsg("System Ready");

    pinMode(BTN_OPEN_CONT, INPUT);
    pinMode(BTN_CLOSE_CONT, INPUT);
    pinMode(BTN_OPEN_STERILE, INPUT);
    pinMode(BTN_CLOSE_STERILE, INPUT);
}

void loop() {
    if (digitalRead(BTN_OPEN_CONT) == HIGH)   openContaminated();
    if (digitalRead(BTN_CLOSE_CONT) == HIGH)  closeContaminated();
    if (digitalRead(BTN_OPEN_STERILE) == HIGH) openSterile();
    if (digitalRead(BTN_CLOSE_STERILE) == HIGH) closeSterile();

    delay(150);
}
