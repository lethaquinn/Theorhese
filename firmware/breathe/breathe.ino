#define LED_PIN 2
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8

void setup() {
  ledcAttach(LED_PIN, PWM_FREQ, PWM_RESOLUTION);
}

void loop() {
  for (int brightness = 0; brightness <= 255; brightness++) {
    ledcWrite(LED_PIN, brightness);
    delay(8);
  }
  delay(300);
  for (int brightness = 255; brightness >= 0; brightness--) {
    ledcWrite(LED_PIN, brightness);
    delay(12);
  }
  delay(1000);
}
