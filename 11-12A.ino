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
const float THRESHOLD_ON   = 15.0f;     // enter "present" when >= this
const float THRESHOLD_OFF  = 8.0f;      // leave "present" when <= this
const float MIN_SIP        = 5.0f;      // minimum delta to count as a sip
const float STABLE_EPS     = 2.0f;      // small deadband for “stable”
const unsigned long DROP_DEBOUNCE_MS = 1500; // drop must persist this long


// --------- “Enough?” per-sip rule ----------
const float ENOUGH_PER_SIP_ML = 30.0f; // change to what you want counted as “enough”


// --------- State Machine ----------
enum State { NO_BOTTLE, BOTTLE_PRESENT };
State state = NO_BOTTLE;


// --------- Baselines / detection helpers ----------
float presentMaxBaseline = 0.0f;  // max stable level while present
float lastStableLevel    = 0.0f;  // last accepted plateau


bool  dropArmed           = false;  // tracking a candidate drop
float dropStartLevel      = 0.0f;
unsigned long dropStartMs = 0;


// remove/return decision uses this
float pendingBeforeRemoval = 0.0f;


// --------- Helpers ----------
void buzz(uint16_t freq, uint16_t durMs) { tone(BUZZER_PIN, freq, durMs); }


void displayLines(const String &l1, const String &l2, const String &l3 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);


  display.setCursor(0, 0);
  display.println("Kawaiigochi");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);


  display.setCursor(0, 18); display.println(l1);
  display.setCursor(0, 30); display.println(l2);
  if (l3.length()) { display.setCursor(0, 42); display.println(l3); }


  display.display();
}


void showWeight(float w) {
  displayLines(String("Weight: ") + String(w, 1) + " g",
               "Waiting for sips...");
}


// Smoothed reading (grams ≈ mL)
float readWeight() { return scale.get_units(10); }


// When a drink is detected
void onDrink(float amount) {
  bool enough = (amount >= ENOUGH_PER_SIP_ML);
  displayLines(String("Drank: ") + String(amount, 0) + " mL",
               String("Enough: ") + (enough ? "Yes" : "No"),
               enough ? "Nice! 💧" : "Bigger sip next time");
  buzz(enough ? 1200 : 800, enough ? 150 : 250);
  delay(800);
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


  displayLines("Hi! I'm Kawaiigochi", "Let's stay hydrated!");
  delay(1200);
}


void loop() {
  float w = readWeight(); // negative is fine


  switch (state) {
    case NO_BOTTLE: {
      // If returning after removal, decide immediately when it reappears
      if (pendingBeforeRemoval > 0.0f && w >= THRESHOLD_ON) {
        float delta = pendingBeforeRemoval - w; // positive = drank while away
        if (delta >= MIN_SIP) onDrink(delta);


        // initialize baselines
        presentMaxBaseline   = w;
        lastStableLevel      = w;
        dropArmed            = false;
        pendingBeforeRemoval = 0.0f;
        state = BOTTLE_PRESENT;
        showWeight(w);
        delay(120);
        return;
      }


      // Fresh appearance with no pending decision
      if (w >= THRESHOLD_ON) {
        presentMaxBaseline = w;
        lastStableLevel    = w;
        dropArmed          = false;
        state = BOTTLE_PRESENT;
        displayLines("Bottle detected!", "Hello!");
        delay(300);
      } else {
        showWeight(w);
      }
    } break;


    case BOTTLE_PRESENT: {
      // Track the highest stable level while present
      if (w > presentMaxBaseline) presentMaxBaseline = w;


      // Case A: sip without removal (sustained drop)
      float deltaFromStable = lastStableLevel - w;


      if (!dropArmed) {
        if (deltaFromStable >= MIN_SIP) {
          dropArmed      = true;
          dropStartLevel = lastStableLevel;
          dropStartMs    = millis();
        }
      } else {
        if (millis() - dropStartMs >= DROP_DEBOUNCE_MS) {
          float consumed = dropStartLevel - w;
          if (consumed >= MIN_SIP) {
            onDrink(consumed);
            lastStableLevel = w; // new plateau after sip
          }
          dropArmed = false;
        } else {
          // Cancel if it bounced back
          if (w >= dropStartLevel - STABLE_EPS) dropArmed = false;
        }
      }


      // Allow stable level to creep up between sips
      if (fabs(w - lastStableLevel) > STABLE_EPS && w > lastStableLevel)
        lastStableLevel = w;


      // Case B: bottle removed
      if (w <= THRESHOLD_OFF) {
        pendingBeforeRemoval = presentMaxBaseline; // remember pre-removal level
        state = NO_BOTTLE;
        displayLines("Bottle removed", "See you soon!");
        delay(250);
      } else {
        showWeight(w);
      }
    } break;
  }


  delay(120);
}
