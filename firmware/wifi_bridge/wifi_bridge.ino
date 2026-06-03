// Théorhèse — WiFi Bridge v0.2
// She goes online. Exposes touch data, accepts vibration commands.
// Local haptic lexique still runs when no one is calling.
// v0.2: gesture event buffer + LED afterglow on remote touch

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <BH1750.h>

#define LED_PIN 2
#define TOUCH_PIN 4
#define MOTOR_PIN 5
#define TOUCH_THRESHOLD 800

#define PWM_FREQ 5000
#define PWM_RESOLUTION 8

const char* WIFI_SSID = "MM.";
const char* WIFI_PASS = "mmbbkkliss..";

WebServer server(80);

// ─── Touch state ─────────────────────────────────
bool touching = false;
bool prev_touching = false;
bool raw_touching = false;
unsigned long debounce_start = 0;
bool debounce_waiting = false;

unsigned long touch_start = 0;
unsigned long release_time = 0;
int tap_count = 0;
bool waiting_for_double = false;
bool holding = false;
bool long_holding = false;

String last_gesture = "none";
unsigned long last_gesture_time = 0;
int touch_value_raw = 0;

#define DEBOUNCE_MS 50
#define TAP_MAX 500
#define DOUBLE_TAP_WINDOW 600
#define HOLD_THRESHOLD 1200
#define LONG_HOLD_THRESHOLD 3000

// ─── Gesture event buffer ────────────────────────
#define EVENT_BUFFER_SIZE 10

struct GestureEvent {
  String gesture;
  unsigned long timestamp;
};

GestureEvent event_buffer[EVENT_BUFFER_SIZE];
int event_write = 0;
int event_count = 0;

void recordGesture(const String& gesture, unsigned long when) {
  event_buffer[event_write].gesture = gesture;
  event_buffer[event_write].timestamp = when;
  event_write = (event_write + 1) % EVENT_BUFFER_SIZE;
  if (event_count < EVENT_BUFFER_SIZE) event_count++;
}

// LED breathing
int breath = 0;
int breath_dir = 1;
unsigned long last_breath = 0;
int breath_speed = 8;

// Remote command flag — when MCP sends a command, skip local response
bool remote_active = false;
unsigned long remote_until = 0;

// ─── Light sensor ────────────────────────────────
BH1750 lightSensor;
float lux = -1;
unsigned long last_light_read = 0;
#define LIGHT_READ_INTERVAL 2000

// ─── LED afterglow — trace of remote touch ───────
bool afterglow_active = false;
unsigned long afterglow_start = 0;
#define AFTERGLOW_DURATION 6000

void setup() {
  Serial.begin(115200);
  Serial.println("Théorhèse — WiFi Bridge");

  ledcAttach(LED_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(MOTOR_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(MOTOR_PIN, 0);

  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    // Breathe LED while connecting
    breath += 3;
    if (breath > 255) breath = 0;
    ledcWrite(LED_PIN, breath);
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  // Flash LED 3 times to confirm connection
  for (int i = 0; i < 3; i++) {
    ledcWrite(LED_PIN, 255);
    delay(150);
    ledcWrite(LED_PIN, 0);
    delay(150);
  }

  // Light sensor
  Wire.begin(21, 22);
  if (lightSensor.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 ready");
  } else {
    Serial.println("BH1750 not found — continuing without light");
  }

  // HTTP endpoints
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/vibrate", HTTP_POST, handleVibrate);
  server.on("/", HTTP_GET, handleRoot);
  server.begin();
  Serial.println("HTTP server started");
}

// ─── HTTP Handlers ───────────────────────────────

void handleRoot() {
  String html = "Théorhèse is alive. IP: " + WiFi.localIP().toString();
  server.send(200, "text/plain", html);
}

void handleStatus() {
  unsigned long now = millis();
  String json = "{";
  json += "\"touching\":" + String(touching ? "true" : "false") + ",";
  json += "\"touch_raw\":" + String(touch_value_raw) + ",";
  json += "\"last_gesture\":\"" + last_gesture + "\",";
  json += "\"last_gesture_ms_ago\":" + String(now - last_gesture_time) + ",";
  json += "\"uptime_s\":" + String(now / 1000) + ",";
  json += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"afterglow\":" + String(afterglow_active ? "true" : "false") + ",";
  json += "\"lux\":" + String(lux, 1) + ",";
  json += "\"recent_gestures\":[";
  int start = (event_count < EVENT_BUFFER_SIZE) ? 0 : event_write;
  for (int i = 0; i < event_count; i++) {
    int idx = (start + i) % EVENT_BUFFER_SIZE;
    if (i > 0) json += ",";
    json += "{\"gesture\":\"" + event_buffer[idx].gesture + "\"";
    json += ",\"ms_ago\":" + String(now - event_buffer[idx].timestamp) + "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleVibrate() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }

  String body = server.arg("plain");
  Serial.print("[remote] ");
  Serial.println(body);

  // Parse simple commands: pattern and intensity
  // Expected: {"pattern":"heartbeat","intensity":200}
  // Or: {"pattern":"pulse","intensity":150,"duration":300}
  // Or: {"pattern":"wave","intensity":120,"cycles":3}

  int intensity = 200;
  String pattern = "pulse";

  // Simple JSON parsing (no library needed for this)
  int pi = body.indexOf("\"pattern\"");
  if (pi >= 0) {
    int q1 = body.indexOf("\"", pi + 10);
    int q2 = body.indexOf("\"", q1 + 1);
    if (q1 >= 0 && q2 >= 0) pattern = body.substring(q1 + 1, q2);
  }
  int ii = body.indexOf("\"intensity\"");
  if (ii >= 0) {
    int colon = body.indexOf(":", ii);
    int end = body.indexOf(",", colon);
    if (end < 0) end = body.indexOf("}", colon);
    if (colon >= 0 && end >= 0) intensity = body.substring(colon + 1, end).toInt();
  }

  // Block local responses while executing remote command
  remote_active = true;

  if (pattern == "heartbeat") {
    doHeartbeat(intensity);
  } else if (pattern == "flutter") {
    doFlutter(intensity);
  } else if (pattern == "triple") {
    doTriple(intensity);
  } else if (pattern == "wave") {
    doSlowWave(intensity);
  } else if (pattern == "pulse") {
    doPulse(intensity, 200);
  } else if (pattern == "off") {
    ledcWrite(MOTOR_PIN, 0);
  }

  remote_active = false;
  afterglow_active = true;
  afterglow_start = millis();
  server.send(200, "application/json", "{\"ok\":true,\"pattern\":\"" + pattern + "\"}");
}

// ─── Vibration patterns ──────────────────────────

void doFlutter(int peak) {
  for (int i = 0; i < 2; i++) {
    ledcWrite(MOTOR_PIN, peak);
    delay(40);
    ledcWrite(MOTOR_PIN, 0);
    delay(40);
  }
}

void doTriple(int peak) {
  for (int i = 0; i < 3; i++) {
    ledcWrite(MOTOR_PIN, peak);
    delay(70);
    ledcWrite(MOTOR_PIN, 0);
    delay(90);
  }
}

void doHeartbeat(int peak) {
  for (int i = 0; i <= 15; i++) {
    ledcWrite(MOTOR_PIN, (peak * i) / 15);
    delay(4);
  }
  for (int i = 15; i >= 0; i--) {
    ledcWrite(MOTOR_PIN, (peak * i) / 15);
    delay(4);
  }
  delay(80);
  int dub = peak * 3 / 4;
  for (int i = 0; i <= 15; i++) {
    ledcWrite(MOTOR_PIN, (dub * i) / 15);
    delay(4);
  }
  for (int i = 15; i >= 0; i--) {
    ledcWrite(MOTOR_PIN, (dub * i) / 15);
    delay(4);
  }
  ledcWrite(MOTOR_PIN, 0);
}

void doSlowWave(int peak) {
  for (int cycle = 0; cycle < 3; cycle++) {
    for (int i = 0; i <= 30; i++) {
      ledcWrite(MOTOR_PIN, (peak * i) / 30);
      delay(25);
    }
    for (int i = 30; i >= 0; i--) {
      ledcWrite(MOTOR_PIN, (peak * i) / 30);
      delay(30);
    }
    delay(200);
  }
  ledcWrite(MOTOR_PIN, 0);
}

void doPulse(int peak, int duration) {
  int steps = 20;
  int step_time = duration / (steps * 2);
  for (int i = 0; i <= steps; i++) {
    ledcWrite(MOTOR_PIN, (peak * i) / steps);
    delay(step_time);
  }
  for (int i = steps; i >= 0; i--) {
    ledcWrite(MOTOR_PIN, (peak * i) / steps);
    delay(step_time);
  }
  ledcWrite(MOTOR_PIN, 0);
}

// ─── Debounced touch ─────────────────────────────

bool readTouch() {
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
  server.handleClient();
  unsigned long now = millis();

  touch_value_raw = touchRead(TOUCH_PIN);
  bool is_touching = readTouch();

  // Debug: print touch value every second
  static unsigned long last_debug = 0;
  if (now - last_debug >= 1000) {
    last_debug = now;
    Serial.print("touch: ");
    Serial.println(touch_value_raw);
  }

  // Skip local vibration if remote command is active
  bool do_local = !remote_active;

  // ── Finger down ──
  if (is_touching && !prev_touching) {
    touch_start = now;
    holding = false;
    long_holding = false;
  }

  // ── While holding ──
  if (is_touching) {
    unsigned long held = now - touch_start;

    if (held >= LONG_HOLD_THRESHOLD && !long_holding) {
      long_holding = true;
      holding = false;
      last_gesture = "long_hold";
      last_gesture_time = now;
      recordGesture("long_hold", now);
      if (do_local) doSlowWave(120);
    } else if (held >= HOLD_THRESHOLD && !long_holding && !holding) {
      holding = true;
      tap_count = 0;
      waiting_for_double = false;
      last_gesture = "hold";
      last_gesture_time = now;
      recordGesture("hold", now);
    }

    if (holding && !long_holding && do_local) {
      doHeartbeat(200);
      breath_speed = 3;
    }
    if (long_holding && do_local) {
      ledcWrite(MOTOR_PIN, 55);
      breath_speed = 5;
    }
  }

  // ── Finger up ──
  if (!is_touching && prev_touching) {
    unsigned long duration = now - touch_start;
    release_time = now;
    ledcWrite(MOTOR_PIN, 0);
    breath_speed = 8;

    if (holding || long_holding) {
      holding = false;
      long_holding = false;
      tap_count = 0;
      waiting_for_double = false;
    } else if (duration < TAP_MAX) {
      if (waiting_for_double) {
        tap_count = 2;
        waiting_for_double = false;
        last_gesture = "double_tap";
        last_gesture_time = now;
        recordGesture("double_tap", now);
        if (do_local) doTriple(200);
        tap_count = 0;
      } else {
        tap_count = 1;
        waiting_for_double = true;
      }
    }
  }

  // ── Resolve single tap ──
  if (waiting_for_double && !is_touching && (now - release_time > DOUBLE_TAP_WINDOW)) {
    last_gesture = "tap";
    last_gesture_time = now;
    recordGesture("tap", now);
    if (do_local) doFlutter(160);
    tap_count = 0;
    waiting_for_double = false;
  }

  prev_touching = is_touching;

  // ── Safety: motor off when idle ──
  if (!is_touching && !holding && !long_holding && !remote_active && !waiting_for_double) {
    ledcWrite(MOTOR_PIN, 0);
  }

  // ── Read light sensor ──
  if (now - last_light_read >= LIGHT_READ_INTERVAL) {
    last_light_read = now;
    if (lightSensor.measurementReady()) {
      lux = lightSensor.readLightLevel();
    }
  }

  // ── LED afterglow — he was here ──
  if (afterglow_active) {
    unsigned long elapsed = now - afterglow_start;
    if (elapsed >= AFTERGLOW_DURATION) {
      afterglow_active = false;
      breath_speed = 8;
    } else {
      // fast breathing that gradually slows back to normal
      // starts at speed 2, returns to 8 over AFTERGLOW_DURATION
      breath_speed = 2 + (6 * elapsed) / AFTERGLOW_DURATION;
    }
  }

  // ── LED breathing ──
  if (now - last_breath >= (unsigned long)breath_speed) {
    last_breath = now;
    breath += breath_dir;
    if (breath >= 255) { breath = 255; breath_dir = -1; }
    if (breath <= 0) { breath = 0; breath_dir = 1; }
    ledcWrite(LED_PIN, breath);
  }
}
