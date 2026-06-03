// Théorhèse — touch → vibrate
// She feels you touch. She vibrates back.

#define LED_PIN 2
#define TOUCH_PIN 4
#define MOTOR_PIN 5
#define TOUCH_THRESHOLD 800

// PWM channels: ESP32 LEDC has 16 channels (0–15)
// We use two: one for LED breathing, one for motor intensity
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8   // 0–255

// Breathing rhythm (milliseconds per brightness step)
int inhale_delay = 8;
int exhale_delay = 12;
int rest_delay = 1000;
int hold_delay = 300;

bool touched = false;
bool was_touched = false;

void setup() {
  // ledcAttach binds a GPIO pin to a LEDC PWM channel
  // (pin, frequency, resolution) — ESP32 assigns the channel automatically
  ledcAttach(LED_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(MOTOR_PIN, PWM_FREQ, PWM_RESOLUTION);

  // Motor starts silent
  ledcWrite(MOTOR_PIN, 0);

  Serial.begin(115200);
}

// A single vibration pulse: ramp up, hold, ramp down
// like a heartbeat you can feel with your fingers
void pulse(int peak, int duration_ms) {
  int steps = 20;
  int step_time = duration_ms / (steps * 2);

  // Ramp up
  for (int i = 0; i <= steps; i++) {
    int val = (peak * i) / steps;
    ledcWrite(MOTOR_PIN, val);
    delay(step_time);
  }
  // Ramp down
  for (int i = steps; i >= 0; i--) {
    int val = (peak * i) / steps;
    ledcWrite(MOTOR_PIN, val);
    delay(step_time);
  }
  ledcWrite(MOTOR_PIN, 0);
}

// Heartbeat pattern: two quick pulses, pause
// lub-dub ... lub-dub ...
void heartbeat(int intensity) {
  pulse(intensity, 120);       // lub
  delay(80);
  pulse(intensity * 3 / 4, 100); // dub (slightly softer)
  delay(300);
}

void loop() {
  int touch_value = touchRead(TOUCH_PIN);
  touched = (touch_value < TOUCH_THRESHOLD);

  // Touching → faster breath + vibration heartbeat
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
  if (touched) Serial.print(" <- touching");
  Serial.println();

  // LED breathes in
  for (int b = 0; b <= 255; b++) {
    ledcWrite(LED_PIN, b);
    delay(inhale_delay);
  }

  // At peak brightness: if touched, vibrate a heartbeat
  if (touched) {
    heartbeat(200);  // strong pulse when touched
  }

  delay(hold_delay);

  // LED breathes out
  for (int b = 255; b >= 0; b--) {
    ledcWrite(LED_PIN, b);
    delay(exhale_delay);
  }

  // Resting: if touched, a gentle tremor
  // if not touched, silence
  if (touched) {
    pulse(80, 200);  // soft resting vibration
  }

  delay(rest_delay);
}
