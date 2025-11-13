#define FORCE_SENSOR_PIN 13  // GPIO36 (ADC1_CH0)


void setup() {
  Serial.begin(115200);
  // 11 dB lets you read ~0–3.3 V safely
  analogSetPinAttenuation(FORCE_SENSOR_PIN, ADC_11db);
  // Optional: ensure 12-bit width (0–4095)
  analogReadResolution(12);
}


void loop() {
  // Average a few samples to tame ESP32 ADC noise
  const int N = 8;
  uint32_t acc = 0;
  for (int i = 0; i < N; i++) acc += analogRead(FORCE_SENSOR_PIN);
  int reading = acc / N;


  Serial.print("FSR raw = ");
  Serial.print(reading);


  // Rough bins scaled for 12-bit (tune to taste)
  if (reading < 40)            Serial.println(" -> no pressure");
  else if (reading < 800)      Serial.println(" -> light touch");
  else if (reading < 2000)     Serial.println(" -> light squeeze");
  else if (reading < 3200)     Serial.println(" -> medium squeeze");
  else                         Serial.println(" -> big squeeze");


  delay(1000); // faster updates
}


