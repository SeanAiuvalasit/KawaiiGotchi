#include <Arduino.h>
#include "HX711.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


// --------- Pins ----------
#define DOUT_PIN    33
#define SCK_PIN     14
#define BUZZER_PIN  27
#define SDA_PIN     22
#define SCL_PIN     21


// --------- Display ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


// --------- Scale ----------
HX711 scale;
float CAL_FACTOR = 2280.0f;     // adjust after calibration


// --------- Thresholds (grams/mL) ----------
const float THRESHOLD_ON   = 15.0f;  // enter "present" when >= this
const float THRESHOLD_OFF  = 8.0f;   // leave "present" when <= this
const float MIN_SIP        = 5.0f;   // minimum delta to count
const float STABLE_EPS     = 2.0f;   // deadband for “stable”
const unsigned long DROP_DEBOUNCE_MS = 1500; // drop must persist this long


// --------- Goal/Tracking ----------
float dailyGoal   = 500.0f;    // demo goal (mL)
float totalWater  = 0.0f;      // accumulated mL
unsigned long lastReminderMs = 0;
const unsigned long REMINDER_PERIOD_MS = 30UL * 60UL * 1000UL; // 30 min


// --------- State Machine ----------
enum State { NO_BOTTLE, BOTTLE_PRESENT };
State state = NO_BOTTLE;


// --------- Baselines / detection helpers ----------
float presentMaxBaseline = 0.0f;  // max stable level while present
float lastStableLevel    = 0.0f;  // last accepted plateau


bool  dropArmed          = false; // tracking a candidate drop
float dropStartLevel     = 0.0f;
unsigned long dropStartMs = 0;


// For remove→return decision
float pendingBeforeRemoval = 0.0f;


// --------- Helpers ----------
void buzz(uint16_t freq, uint16_t durMs) { tone(BUZZER_PIN, freq, durMs); }


void displayMessage(const String &msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Kawaiigochi");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
  display.setCursor(0, 18);
  display.println(msg);
  display.display();
}


void displayStatus(float w) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);


  display.setCursor(0, 0);
  display.print("W: ");
  display.print(w, 1);
  display.print(" g");


  display.setCursor(0, 12);
  display.print("Total: ");
  display.print(totalWater, 0);
  display.print("/");
  display.print(dailyGoal, 0);
  display.println(" mL");


  float pct = (dailyGoal > 0) ? (100.0f * totalWater / dailyGoal) : 0.0f;
  display.setCursor(0, 24);
  display.print("Progress: ");
  display.print(pct, 0);
  display.println("%");


  display.setCursor(0, 40);
  if (pct >= 75)      display.println("I'm so happy!");
  else if (pct >= 25) display.println("Keep sipping!");
  else                display.println("I'm sad...");


  display.display();
}


// Smoothed reading (grams ≈ mL)
float readWeight() { return scale.get_units(10); }


// Count a drink
void onDrink(float amount) {
  totalWater += amount;
  displayMessage("Yay! You drank some water!");
  buzz(1000, 150);
  delay(600);
}


void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);


  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);


  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    // continue even if display not found
  }
  display.clearDisplay();
  display.display();


  scale.begin(DOUT_PIN, SCK_PIN);
  scale.set_scale(CAL_FACTOR);
  scale.tare();


  displayMessage("Hi! I'm Kawaiigochi\nLet's stay hydrated!");
  delay(1500);
}


void loop() {
  float w = readWeight(); // may be negative; that's fine


  switch (state) {
    case NO_BOTTLE: {
      // If we were waiting for a return after removal, decide immediately on reappear
      if (pendingBeforeRemoval > 0.0f && w >= THRESHOLD_ON) {
        float delta = pendingBeforeRemoval - w; // positive = drank
        if (delta >= MIN_SIP) onDrink(delta);


        // Initialize baselines for new present state
        presentMaxBaseline = w;
        lastStableLevel    = w;
        dropArmed          = false;
        pendingBeforeRemoval = 0.0f;
        state = BOTTLE_PRESENT;
        displayStatus(w);
        delay(120);
        return;
      }


      // Fresh appearance with no pending delta
      if (w >= THRESHOLD_ON) {
        presentMaxBaseline = w;
        lastStableLevel    = w;
        dropArmed          = false;
        state = BOTTLE_PRESENT;
        displayMessage("Hello bottle!");
        delay(300);
      }


      displayStatus(w);
    } break;


    case BOTTLE_PRESENT: {
      // Track max stable level while present
      if (w > presentMaxBaseline) presentMaxBaseline = w;


      // -------- Case A: sip without removal (sustained drop) --------
      float deltaFromStable = lastStableLevel - w;


      if (!dropArmed) {
        if (deltaFromStable >= MIN_SIP) {
          dropArmed     = true;
          dropStartLevel = lastStableLevel;
          dropStartMs    = millis();
        }
      } else {
        // Confirm drop has persisted
        if (millis() - dropStartMs >= DROP_DEBOUNCE_MS) {
          float consumed = dropStartLevel - w;
          if (consumed >= MIN_SIP) {
            onDrink(consumed);
            lastStableLevel = w;  // new plateau after the sip
          }
          dropArmed = false;
        } else {
          // Cancel if it bounced back to near the old level
          if (w >= dropStartLevel - STABLE_EPS) dropArmed = false;
        }
      }


      // Allow stable level to creep up slowly between sips
      if (fabs(w - lastStableLevel) > STABLE_EPS && w > lastStableLevel)
        lastStableLevel = w;


      // -------- Case B: bottle removed --------
      if (w <= THRESHOLD_OFF) {
        pendingBeforeRemoval = presentMaxBaseline; // remember pre-removal level
        state = NO_BOTTLE;
        displayMessage("See you soon!");
        delay(250);
      } else {
        displayStatus(w);
      }
    } break;
  }


  // ----- Periodic reminder -----
  if (millis() - lastReminderMs >= REMINDER_PERIOD_MS) {
    float pct = (dailyGoal > 0) ? (100.0f * totalWater / dailyGoal) : 0.0f;
    if (pct < 75.0f) {
      displayMessage("Time to sip!");
      buzz(1500, 250);
      delay(400);
    } else {
      displayMessage("Great job staying hydrated!");
      delay(500);
    }
    lastReminderMs = millis();
  }


  delay(120); // small UI pacing delay
}





