/**
 * bird_light_v2.ino
 *
 * Bird presence light controller — v2
 * Improved menu system with state machine, fast-increment, and
 * always-visible status screen. Uses LightScheduler library.
 *
 * Hardware:
 *   DS1302 RTC  : RST→D5, DAT→D6, CLK→D7
 *   LCD 16x2 I2C: SDA, SCL
 *   L298N ENA   : D9 (PWM)  IN1→D2  IN2→D3
 *   Button 1    : D11  (increment / enter clock config)
 *   Button 2    : D12  (confirm / enter schedule config)
 *   LED lamp    : L298N OUT+ / OUT-  with external 12V supply
 *
 * Button logic:
 *   IDLE   → BTN1 short : set clock
 *   IDLE   → BTN2 short : set schedule
 *   IN MENU→ BTN1       : increment value (hold for fast)
 *   IN MENU→ BTN2       : confirm and go to next step
 *   Auto-exit after 8 s of inactivity
 *
 * Libraries required:
 *   DS1302            (Makuna)
 *   LiquidCrystal_I2C (johnrickman)
 *   LightScheduler    (IAMTorres) — github.com/IAMTorres/LightScheduler
 */

#include <Wire.h>
#include <DS1302.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <LightScheduler.h>

// ── Pins ──────────────────────────────────────────────────────────────────────
const int RST_PIN  = 5;
const int DAT_PIN  = 6;
const int CLK_PIN  = 7;
const int PWM_PIN  = 9;
const int IN1_PIN  = 2;
const int IN2_PIN  = 3;
const int BTN1_PIN = 11;   // increment / enter clock config
const int BTN2_PIN = 12;   // confirm   / enter schedule config

// ── Config ────────────────────────────────────────────────────────────────────
const int           EEPROM_ADDR     = 0;      // 4 bytes used
const unsigned long MENU_TIMEOUT_MS = 8000;   // return to idle after 8 s
const unsigned long HOLD_FAST_MS    = 800;    // hold time before fast increment
const unsigned long FAST_STEP_MS    = 100;    // interval during fast increment
const unsigned long SLOW_STEP_MS    = 400;    // interval during slow increment

// ── Objects ───────────────────────────────────────────────────────────────────
DS1302           rtc(RST_PIN, DAT_PIN, CLK_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);
LightScheduler   scheduler(PWM_PIN, 60);      // 60-min sunset dim

// ── State machine ─────────────────────────────────────────────────────────────
enum State {
    IDLE,
    SET_ON_HR,
    SET_ON_MIN,
    SET_OFF_HR,
    SET_OFF_MIN,
    SET_CLK_HR,
    SET_CLK_MIN
};

State         currentState = IDLE;
unsigned long lastActivity = 0;

// Values being edited
uint8_t editHr  = 0;
uint8_t editMin = 0;

// Button state
unsigned long btn1HoldStart = 0;
unsigned long btn2HoldStart = 0;
unsigned long lastIncrement = 0;
bool          btn1WasDown   = false;
bool          btn2WasDown   = false;

// Custom bird character
byte birdChar[8] = {
    0b00100,
    0b01010,
    0b00100,
    0b11111,
    0b00100,
    0b00100,
    0b01010,
    0b10001
};

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(9600);

    pinMode(IN1_PIN, OUTPUT);
    pinMode(IN2_PIN, OUTPUT);
    digitalWrite(IN1_PIN, HIGH);
    digitalWrite(IN2_PIN, LOW);

    pinMode(BTN1_PIN, INPUT_PULLUP);
    pinMode(BTN2_PIN, INPUT_PULLUP);

    lcd.init();
    lcd.backlight();
    lcd.createChar(0, birdChar);

    rtc.halt(false);
    rtc.writeProtect(false);

    scheduler.loadFromEEPROM(EEPROM_ADDR);

    ScheduleTime on  = scheduler.getOnTime();
    ScheduleTime off = scheduler.getOffTime();
    Serial.print("Schedule: ON ");
    Serial.print(on.hour);  Serial.print(":"); Serial.print(on.minute);
    Serial.print("  OFF "); Serial.print(off.hour); Serial.print(":"); Serial.println(off.minute);

    lastActivity = millis();
    lcd.clear();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    Time t = rtc.time();

    scheduler.update(t.hr, t.min);
    handleButtons(t);
    updateDisplay(t);

    // Auto-return to idle on timeout
    if (currentState != IDLE && millis() - lastActivity > MENU_TIMEOUT_MS) {
        currentState = IDLE;
        lcd.clear();
    }
}

// ── Button handling ───────────────────────────────────────────────────────────
void handleButtons(Time& t) {
    bool btn1Down = (digitalRead(BTN1_PIN) == LOW);
    bool btn2Down = (digitalRead(BTN2_PIN) == LOW);

    // ── BTN1: increment / enter clock config ──────────────────────────────────
    if (btn1Down) {
        if (!btn1WasDown) {
            btn1HoldStart = millis();
            btn1WasDown   = true;
        }

        // Hold-to-fast-increment (only in menu states)
        if (currentState != IDLE) {
            unsigned long held     = millis() - btn1HoldStart;
            unsigned long interval = (held > HOLD_FAST_MS) ? FAST_STEP_MS : SLOW_STEP_MS;

            if (millis() - lastIncrement > interval) {
                incrementEditValue();
                lastIncrement = millis();
                lastActivity  = millis();
            }
        }
    } else {
        if (btn1WasDown) {
            // Released — short press
            if (millis() - btn1HoldStart < HOLD_FAST_MS) {
                onBtn1ShortPress(t);
                lastActivity = millis();
            }
            btn1WasDown = false;
        }
    }

    // ── BTN2: confirm / enter schedule config ─────────────────────────────────
    if (btn2Down) {
        if (!btn2WasDown) {
            btn2HoldStart = millis();
            btn2WasDown   = true;
        }
    } else {
        if (btn2WasDown) {
            if (millis() - btn2HoldStart < 800) {
                onBtn2ShortPress(t);
                lastActivity = millis();
            }
            btn2WasDown = false;
        }
    }
}

void onBtn1ShortPress(Time& t) {
    if (currentState == IDLE) {
        // Enter clock config
        Time now = rtc.time();
        editHr  = now.hr;
        editMin = now.min;
        currentState = SET_CLK_HR;
        lcd.clear();
    }
    // In menu: short press handled by hold logic on first tick
}

void onBtn2ShortPress(Time& t) {
    switch (currentState) {

        case IDLE:
            // Enter schedule config — start with ON time
            editHr  = scheduler.getOnTime().hour;
            editMin = scheduler.getOnTime().minute;
            currentState = SET_ON_HR;
            lcd.clear();
            break;

        case SET_ON_HR:
            currentState = SET_ON_MIN;
            lcd.clear();
            break;

        case SET_ON_MIN:
            scheduler.setOnTime(editHr, editMin);
            // Move to OFF time
            editHr  = scheduler.getOffTime().hour;
            editMin = scheduler.getOffTime().minute;
            currentState = SET_OFF_HR;
            lcd.clear();
            break;

        case SET_OFF_HR:
            currentState = SET_OFF_MIN;
            lcd.clear();
            break;

        case SET_OFF_MIN:
            scheduler.setOffTime(editHr, editMin);
            scheduler.saveToEEPROM(EEPROM_ADDR);
            Serial.println("Schedule saved.");
            currentState = IDLE;
            lcd.clear();
            break;

        case SET_CLK_HR:
            currentState = SET_CLK_MIN;
            lcd.clear();
            break;

        case SET_CLK_MIN: {
            Time now  = rtc.time();
            now.hr    = editHr;
            now.min   = editMin;
            now.sec   = 0;
            rtc.time(now);
            Serial.print("Clock set to ");
            Serial.print(editHr); Serial.print(":"); Serial.println(editMin);
            currentState = IDLE;
            lcd.clear();
            break;
        }
    }
}

void incrementEditValue() {
    switch (currentState) {
        case SET_ON_HR:
        case SET_OFF_HR:
        case SET_CLK_HR:
            editHr = (editHr + 1) % 24;
            break;
        case SET_ON_MIN:
        case SET_OFF_MIN:
        case SET_CLK_MIN:
            editMin = (editMin + 1) % 60;
            break;
        default:
            break;
    }
}

// ── Display ───────────────────────────────────────────────────────────────────
void updateDisplay(Time& t) {
    static unsigned long lastDraw  = 0;
    static State         lastState = IDLE;

    // Idle: refresh every 1s. Menu: refresh every time something changes.
    if (currentState == IDLE && millis() - lastDraw < 1000) return;
    lastDraw = millis();

    if (currentState != lastState) {
        lcd.clear();
        lastState = currentState;
    }

    switch (currentState) {
        case IDLE:
            drawIdleScreen(t);
            break;
        case SET_ON_HR:
            drawEditScreen("ON time", "hour", editHr, editMin);
            break;
        case SET_ON_MIN:
            drawEditScreen("ON time", "min ", editHr, editMin);
            break;
        case SET_OFF_HR:
            drawEditScreen("OFF time", "hour", editHr, editMin);
            break;
        case SET_OFF_MIN:
            drawEditScreen("OFF time", "min ", editHr, editMin);
            break;
        case SET_CLK_HR:
            drawEditScreen("Clock", "hour", editHr, editMin);
            break;
        case SET_CLK_MIN:
            drawEditScreen("Clock", "min ", editHr, editMin);
            break;
    }
}

void drawIdleScreen(Time& t) {
    // Line 1: ON→OFF + bird icon
    // e.g.  "08:00→22:00    🐦"
    ScheduleTime on  = scheduler.getOnTime();
    ScheduleTime off = scheduler.getOffTime();

    lcd.setCursor(0, 0);
    char line1[17];
    snprintf(line1, sizeof(line1), "%02d:%02d->%02d:%02d    ",
             on.hour, on.minute, off.hour, off.minute);
    lcd.print(line1);
    lcd.setCursor(15, 0);
    lcd.write((uint8_t)0);  // bird character

    // Line 2: current time + status
    // e.g.  "14:35:22  ON   " or "22:15:03  47%  "
    lcd.setCursor(0, 1);
    char line2[17];
    const char* statusStr;
    char dimBuf[6] = "";

    if (scheduler.isDimming()) {
        snprintf(dimBuf, sizeof(dimBuf), "%3d%%", map(scheduler.getBrightness(), 0, 255, 0, 100));
        statusStr = dimBuf;
    } else if (scheduler.isOn()) {
        statusStr = " ON ";
    } else {
        statusStr = "OFF ";
    }

    snprintf(line2, sizeof(line2), "%02d:%02d:%02d  %-4s  ",
             t.hr, t.min, t.sec, statusStr);
    lcd.print(line2);
}

void drawEditScreen(const char* label, const char* field, uint8_t hr, uint8_t min) {
    // Line 1: what we're setting
    // e.g.  "Set ON time     "
    lcd.setCursor(0, 0);
    char line1[17];
    snprintf(line1, sizeof(line1), "Set %-11s", label);
    lcd.print(line1);

    // Line 2: current value + which field is active
    // e.g.  "08:30  [hour] ▲ "
    lcd.setCursor(0, 1);
    char line2[17];
    snprintf(line2, sizeof(line2), "%02d:%02d [%s]  ", hr, min, field);
    lcd.print(line2);
}
