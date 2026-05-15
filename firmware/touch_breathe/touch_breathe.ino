#define LED_PIN 2
#define TOUCH_PIN 4
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8
#define TOUCH_THRESHOLD 800

int inhale_delay = 8;
int exhale_delay = 12;
int rest_delay = 1000;
int hold_delay = 300;

bool touched = false;

void setup() {
  ledcAttach(LED_PIN, PWM_FREQ, PWM_RESOLUTION);
  Serial.begin(115200);
}

void loop() {
  int touch_value = touchRead(TOUCH_PIN);
  touched = (touch_value < TOUCH_THRESHOLD);

  if (touched) {
    inhale_delay = 3;
    exhale_delay = 4;
    rest_delay = 200;
    hold_delay = 100;
  } else {
    inhale_delay = 8;
    exhale_delay = 12;
    rest_delay = 1000;
    hold_delay = 300;
  }

  Serial.print("touch: ");
  Serial.print(touch_value);
  Serial.print(touched ? " <- touching" : "");
  Serial.println();

  for (int b = 0; b <= 255; b++) {
    ledcWrite(LED_PIN, b);
    delay(inhale_delay);
  }
  delay(hold_delay);
  for (int b = 255; b >= 0; b--) {
    ledcWrite(LED_PIN, b);
    delay(exhale_delay);
  }
  delay(rest_delay);
}
