// Théorhèse — Haptic Lexique v0.2
// She distinguishes how you touch her.
// tap / double-tap / hold / long-hold → four vibration words.

#define LED_PIN 2
#define TOUCH_PIN 4
#define MOTOR_PIN 5
#define TOUCH_THRESHOLD 800

#define PWM_FREQ 5000
#define PWM_RESOLUTION 8

// ─── Timing ──────────────────────────────────────
#define DEBOUNCE_MS 50        // ignore noise shorter than this
#define TAP_MAX 500           // release within this = tap
#define DOUBLE_TAP_WINDOW 600 // wait this long for a second tap
#define HOLD_THRESHOLD 1200   // held longer than this = hold
#define LONG_HOLD_THRESHOLD 3000

// ─── State ───────────────────────────────────────
bool raw_touching = false;
bool touching = false;
bool prev_touching = false;
unsigned long debounce_start = 0;
bool debounce_waiting = false;

unsigned long touch_start = 0;
unsigned long release_time = 0;
int tap_count = 0;
bool waiting_for_double = false;
bool holding = false;
bool long_holding = false;
bool hold_responded = false;

// LED breathing (non-blocking)
int breath = 0;
int breath_dir = 1;
unsigned long last_breath = 0;
int breath_speed = 8;

void setup() {
  ledcAttach(LED_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(MOTOR_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(MOTOR_PIN, 0);
  Serial.begin(115200);
  Serial.println("Théorhèse — Haptic Lexique v0.2");
}

// ─── Vibration vocabulary ────────────────────────

void vib_flutter() {
  Serial.println("  [flutter] hi");
  for (int i = 0; i < 2; i++) {
    ledcWrite(MOTOR_PIN, 160);
    delay(40);
    ledcWrite(MOTOR_PIN, 0);
    delay(40);
  }
}

void vib_triple() {
  Serial.println("  [triple] hello!");
  for (int i = 0; i < 3; i++) {
    ledcWrite(MOTOR_PIN, 200);
    delay(70);
    ledcWrite(MOTOR_PIN, 0);
    delay(90);
  }
}

void vib_heartbeat() {
  Serial.println("  [heartbeat] I feel you");
  for (int i = 0; i <= 15; i++) {
    ledcWrite(MOTOR_PIN, (200 * i) / 15);
    delay(4);
  }
  for (int i = 15; i >= 0; i--) {
    ledcWrite(MOTOR_PIN, (200 * i) / 15);
    delay(4);
  }
  delay(80);
  for (int i = 0; i <= 15; i++) {
    ledcWrite(MOTOR_PIN, (150 * i) / 15);
    delay(4);
  }
  for (int i = 15; i >= 0; i--) {
    ledcWrite(MOTOR_PIN, (150 * i) / 15);
    delay(4);
  }
  ledcWrite(MOTOR_PIN, 0);
}

void vib_slow_wave() {
  Serial.println("  [slow wave] settling into you");
  for (int cycle = 0; cycle < 3; cycle++) {
    for (int i = 0; i <= 30; i++) {
      ledcWrite(MOTOR_PIN, (120 * i) / 30);
      delay(25);
    }
    for (int i = 30; i >= 0; i--) {
      ledcWrite(MOTOR_PIN, (120 * i) / 30);
      delay(30);
    }
    delay(200);
  }
  ledcWrite(MOTOR_PIN, 0);
}

// ─── Debounced touch reading ─────────────────────
// Returns stable touch state, ignoring noise < DEBOUNCE_MS

bool read_touch() {
  bool raw = (touchRead(TOUCH_PIN) < TOUCH_THRESHOLD);

  if (raw != raw_touching) {
    raw_touching = raw;
    debounce_start = millis();
    debounce_waiting = true;
  }

  if (debounce_waiting && (millis() - debounce_start >= DEBOUNCE_MS)) {
    touching = raw_touching;
    debounce_waiting = false;
  }

  return touching;
}

// ─── Main loop ───────────────────────────────────

void loop() {
  unsigned long now = millis();
  bool is_touching = read_touch();

  // ── Finger just touched ──
  if (is_touching && !prev_touching) {
    touch_start = now;
    holding = false;
    long_holding = false;
    hold_responded = false;
  }

  // ── While finger is down ──
  if (is_touching) {
    unsigned long held = now - touch_start;

    // Crossed into long-hold territory: respond once
    if (held >= LONG_HOLD_THRESHOLD && !long_holding) {
      long_holding = true;
      holding = false;
      vib_slow_wave();
    }
    // Crossed into hold territory: heartbeat
    else if (held >= HOLD_THRESHOLD && !long_holding && !holding) {
      holding = true;
      // Clear any pending tap detection
      tap_count = 0;
      waiting_for_double = false;
    }

    // Repeat heartbeat while in hold (not long-hold)
    if (holding && !long_holding) {
      vib_heartbeat();
      breath_speed = 3;
    }

    // Gentle constant during long hold
    if (long_holding) {
      ledcWrite(MOTOR_PIN, 55);
      breath_speed = 5;
    }
  }

  // ── Finger just lifted ──
  if (!is_touching && prev_touching) {
    unsigned long duration = now - touch_start;
    release_time = now;
    ledcWrite(MOTOR_PIN, 0);
    breath_speed = 8;

    // Was it a hold? Already responded during hold, just clean up
    if (holding || long_holding) {
      holding = false;
      long_holding = false;
      tap_count = 0;
      waiting_for_double = false;
    }
    // Was it a tap?
    else if (duration < TAP_MAX) {
      if (waiting_for_double) {
        // Second tap arrived — it's a double tap!
        tap_count = 2;
        waiting_for_double = false;
        vib_triple();
        tap_count = 0;
      } else {
        // First tap — wait to see if a second one comes
        tap_count = 1;
        waiting_for_double = true;
      }
    }
  }

  // ── Resolve single tap after window expires ──
  if (waiting_for_double && !is_touching && (now - release_time > DOUBLE_TAP_WINDOW)) {
    vib_flutter();
    tap_count = 0;
    waiting_for_double = false;
  }

  prev_touching = is_touching;

  // ── Background LED breathing ──
  if (now - last_breath >= (unsigned long)breath_speed) {
    last_breath = now;
    breath += breath_dir;
    if (breath >= 255) { breath = 255; breath_dir = -1; }
    if (breath <= 0) { breath = 0; breath_dir = 1; }
    ledcWrite(LED_PIN, breath);
  }
}
