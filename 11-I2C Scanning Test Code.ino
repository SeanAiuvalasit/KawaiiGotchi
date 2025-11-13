#include <Wire.h>
void scan(int sda, int scl) {
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  delay(200);
  Serial.printf("\nScanning on SDA=%d SCL=%d...\n", sda, scl);
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found I2C device at 0x%02X\n", addr);
      count++;
    }
  }
  if (!count) Serial.println("  No I2C devices found.");
}


void setup() {
  Serial.begin(115200);
  delay(500);
  scan(21,22);   // typical ESP32 default
  scan(22,21);   // your current wiring
}


void loop() {}
