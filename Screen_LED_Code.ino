#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Pin definitions (adjust based on your board)
// These are common I2C pins for ESP32, but can be changed.
// You may need to connect SCL to GPIO 22 and SDA to GPIO 21.
#define OLED_SDA 21
#define OLED_SCL 22

// Define the display dimensions (e.g., 128x64)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Create an Adafruit_SSD1306 object
// The "false" arguments mean you're not using the default I2C pins,
// and instead are using the ones defined above (OLED_SDA, OLED_SCL).
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, OLED_SDA, OLED_SCL);

void setup() {
  Serial.begin(115200);

  // Initialize the I2C bus
  Wire.begin(); 

  // Initialize the OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    while (true); // Halt if initialization fails
  }

  // Display a brief message and wait before starting the main loop
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Hello!");
  display.display();
  delay(2000); 
}

void loop() {
  // Clear the display buffer
  display.clearDisplay();

  // Set text properties
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);

  // Print text
  display.println("Welcome to ESP32!");
  display.setCursor(0, 10); // Move cursor down
  display.print("Voltage: ");
  display.print("3.3V"); // Replace with a variable from a sensor for a real project
  
  // Update the display to show the changes
  display.display();

  delay(1000); // Wait for a second
}


