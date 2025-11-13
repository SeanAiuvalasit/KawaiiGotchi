#include <Arduino.h>
#include "HX711.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>


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
const float ENOUGH_PER_SIP_ML = 100.0f; // adjust your per-sip target


// --------- State Machine ----------
enum State { NO_BOTTLE, BOTTLE_PRESENT };
State state = NO_BOTTLE;


// --------- Baselines / detection helpers ----------
float presentMaxBaseline = 0.0f;  // max stable level while present
float lastStableLevel    = 0.0f;  // last accepted plateau


bool  dropArmed           = false;  // tracking a candidate drop
float dropStartLevel      = 0.0f;
unsigned long dropStartMs = 0;


// Remove/return decision uses this
float pendingBeforeRemoval = 0.0f;


// Timing for “waiting for sips…” hint
unsigned long presentSinceMs = 0;  // set when bottle becomes present


// ---------- Simple helpers ----------
void buzz(uint16_t freq, uint16_t durMs) { tone(BUZZER_PIN, freq, durMs); }


void displayLines(const String &l1, const String &l2 = "", const String &l3 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);


  display.setCursor(0, 0);
  display.println("Kawaiigochi");
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);


  if (l1.length()) { display.setCursor(0, 18); display.println(l1); }
  if (l2.length()) { display.setCursor(0, 30); display.println(l2); }
  if (l3.length()) { display.setCursor(0, 42); display.println(l3); }


  display.display();
}


// Show weight; optionally include the “Waiting for sips...” hint
void showWeight(float w, bool showWaitingHint) {
  if (showWaitingHint) {
    displayLines(String("Weight: ") + String(w, 1) + " g", "Waiting for sips...");
  } else {
    displayLines(String("Weight: ") + String(w, 1) + " g");
  }
}


// Smoothed reading (grams ≈ mL)
float readWeight() { return scale.get_units(10); }


// ---------- Drawing helpers (procedural “images”) ----------
// Draw a simple arc using many short points
void drawArc(int cx, int cy, int r, float startDeg, float endDeg) {
  const float PI_F = 3.14159265f;
  for (float a = startDeg; a <= endDeg; a += 1.0f) {
    float rad = a * PI_F / 180.0f;
    int x = cx + (int)roundf(r * cosf(rad));
    int y = cy + (int)roundf(r * sinf(rad));
    display.drawPixel(x, y, SSD1306_WHITE);
  }
}


// Cute cat face (happy or sad) centered at (cx,cy)
void drawCatFace(int cx, int cy, bool happy) {
  display.clearDisplay();


  // Face circle
  const int R = 23; // radius
  display.drawCircle(cx, cy, R, SSD1306_WHITE);


  // Ears (filled triangles)
  // Left ear
  display.fillTriangle(cx - 18, cy - 5,  cx - 5, cy - 24,  cx - 1, cy - 6, SSD1306_WHITE);
  // Right ear
  display.fillTriangle(cx + 18, cy - 5,  cx + 5, cy - 24,  cx + 1, cy - 6, SSD1306_WHITE);


  // Eyes
  display.fillCircle(cx - 8, cy - 3, 2, SSD1306_WHITE);
  display.fillCircle(cx + 8, cy - 3, 2, SSD1306_WHITE);


  // Nose (small triangle)
  display.fillTriangle(cx - 2, cy + 2, cx + 2, cy + 2, cx, cy + 4, SSD1306_WHITE);


  // Whiskers
  display.drawLine(cx - 5, cy + 3, cx - 18, cy + 1, SSD1306_WHITE);
  display.drawLine(cx - 5, cy + 5, cx - 18, cy + 5, SSD1306_WHITE);
  display.drawLine(cx - 5, cy + 7, cx - 18, cy + 9, SSD1306_WHITE);


  display.drawLine(cx + 5, cy + 3, cx + 18, cy + 1, SSD1306_WHITE);
  display.drawLine(cx + 5, cy + 5, cx + 18, cy + 5, SSD1306_WHITE);
  display.drawLine(cx + 5, cy + 7, cx + 18, cy + 9, SSD1306_WHITE);


  // Mouth: arc (smile or frown)
  // Happy: upward arc; Sad: downward arc
  if (happy) {
    drawArc(cx, cy + 10, 10, 200, 340); // smile
  } else {
    drawArc(cx, cy + 16, 10, 20, 160);  // frown
  }


  display.display();
}


// ---------- When a drink is detected ----------
void onDrink(float amount) {
  bool enough = (amount >= ENOUGH_PER_SIP_ML);


  // Center the cat in the upper 48 px; leave text on the bottom
  drawCatFace(64, 26, /*happy=*/enough);


  // Text near bottom
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 52);
  display.print("Drank: ");
  display.print(amount, 0);
  display.print(" mL  ");
  display.print("Enough: ");
  display.print(enough ? "Yes" : "No");
  display.display();


  buzz(enough ? 1200 : 800, enough ? 150 : 250);
  delay(10000); // keep result for 10 seconds
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


        // start the 20s window before showing the waiting hint
        presentSinceMs = millis();


        state = BOTTLE_PRESENT;
        showWeight(w, false); // no "Waiting..." yet
        delay(120);
        return;
      }


      // Fresh appearance with no pending decision
      if (w >= THRESHOLD_ON) {
        presentMaxBaseline = w;
        lastStableLevel    = w;
        dropArmed          = false;


        // start the 20s window before showing the waiting hint
        presentSinceMs = millis();


        state = BOTTLE_PRESENT;
        displayLines("Bottle detected!", "Hello!");
        delay(300);
      } else {
        // No bottle present; just show the weight line (no waiting hint)
        showWeight(w, false);
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
            // After onDrink, continue normal display rules
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
        // Show "Waiting for sips..." only after 20s from placement
        bool showHint = (millis() - presentSinceMs >= 20000UL);
        showWeight(w, showHint);
      }
    } break;
  }


  delay(120);
}


