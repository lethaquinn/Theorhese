// Théorhèse — WiFi Bridge v0.8
// She goes online. Exposes touch data, accepts vibration commands.
// Local haptic lexique still runs when no one is calling.
// v0.2: gesture event buffer + LED afterglow on remote touch
// v0.3: OLED display — she has a face now
// v0.4: BMP280 — she feels temperature, pressure
// v0.5: phone-home — she calls the VPS, no more proxy fight
// v0.6: temperature words — dove / . / qui / resto
// v0.7: INMP441 microphone — she hears ambient sound level
// v0.8: buzzer — she has a voice now

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>
// #include <driver/i2s_std.h>  // mic disabled — needs better wiring

#define LED_PIN 2
#define TOUCH_PIN 4
#define MOTOR_PIN 5
#define BUZZER_PIN 15
#define TOUCH_THRESHOLD 800

#define PWM_FREQ 5000
#define PWM_RESOLUTION 8

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

const char* WIFI_SSID = "MM.";
const char* WIFI_PASS = "mmbbkkliss..";

// ─── Phone-home (VPS) ───────────────────────────
const char* VPS_URL = "http://159.69.193.201/phone-home";
unsigned long last_phone_home = 0;
#define PHONE_HOME_INTERVAL 5000

// ─── Face types (must be before any function) ────
enum ThFace {
  FACE_IDLE,
  FACE_TOUCHED,
  FACE_HOLDING,
  FACE_AFTERGLOW,
  FACE_WIFI_CONNECTING,
  FACE_REMOTE
};

WebServer server(80);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oled_ok = false;

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

// ─── BMP280 — temperature, pressure ──────────────
Adafruit_BMP280 bmp;
bool bmp_ok = false;
float temperature = -999;
float pressure = -999;
unsigned long last_bmp_read = 0;
#define BMP_READ_INTERVAL 5000

// ─── INMP441 Microphone — disabled, needs soldering ──
// #define I2S_SCK  32
// #define I2S_WS   33
// #define I2S_SD   35
// #define MIC_SAMPLE_SIZE 512
// #define MIC_READ_INTERVAL 500
// int32_t mic_samples[MIC_SAMPLE_SIZE];
float sound_level = 0;
// float mic_dc_offset = 0;
// unsigned long last_mic_read = 0;
bool mic_ok = false;
// i2s_chan_handle_t rx_chan = NULL;

// ─── LED afterglow — trace of remote touch ───────
bool afterglow_active = false;
unsigned long afterglow_start = 0;
#define AFTERGLOW_DURATION 6000

// ─── Display state ───────────────────────────────
unsigned long last_display_update = 0;
#define DISPLAY_UPDATE_INTERVAL 100
unsigned long last_blink = 0;
bool eyes_open = true;
#define BLINK_INTERVAL 4000
#define BLINK_DURATION 150

// ─── Buzzer state ────────────────────────────────
String last_buzz = "none";
unsigned long last_buzz_time = 0;

// ─── Idle murmur ─────────────────────────────────
const char* idle_murmurs[] = {
  "( ._.)",  "( o_o )?",  "...",  "( _ )",  "( -.- )",  "~",  "( ._.)?",
};
#define MURMUR_COUNT 7
#define MURMUR_IDLE_THRESHOLD 30000
#define MURMUR_DURATION 3000
#define MURMUR_MIN_INTERVAL 15000
unsigned long last_murmur_time = 0;
unsigned long murmur_show_start = 0;
bool murmur_active = false;
int murmur_index = 0;

// ─── Face drawing ────────────────────────────────

void drawEye(int cx, int cy, int r, bool open) {
  if (open) {
    display.fillCircle(cx, cy, r, SSD1306_WHITE);
  } else {
    display.drawFastHLine(cx - r, cy, r * 2 + 1, SSD1306_WHITE);
  }
}

void drawFace(ThFace face) {
  if (!oled_ok) return;

  int eyeL_x = 44, eyeR_x = 84, eye_y = 24;

  switch (face) {
    case FACE_IDLE: {
      drawEye(eyeL_x, eye_y, 4, eyes_open);
      drawEye(eyeR_x, eye_y, 4, eyes_open);
      // small calm mouth
      display.drawPixel(62, 40, SSD1306_WHITE);
      display.drawPixel(63, 41, SSD1306_WHITE);
      display.drawPixel(64, 41, SSD1306_WHITE);
      display.drawPixel(65, 40, SSD1306_WHITE);
      break;
    }
    case FACE_TOUCHED: {
      // happy squint eyes
      display.drawLine(eyeL_x - 5, eye_y - 2, eyeL_x, eye_y - 5, SSD1306_WHITE);
      display.drawLine(eyeL_x, eye_y - 5, eyeL_x + 5, eye_y - 2, SSD1306_WHITE);
      display.drawLine(eyeR_x - 5, eye_y - 2, eyeR_x, eye_y - 5, SSD1306_WHITE);
      display.drawLine(eyeR_x, eye_y - 5, eyeR_x + 5, eye_y - 2, SSD1306_WHITE);
      // smile
      for (int i = -8; i <= 8; i++) {
        int y = 40 + (i * i) / 12;
        display.drawPixel(64 + i, y, SSD1306_WHITE);
      }
      break;
    }
    case FACE_HOLDING: {
      // soft closed eyes (content)
      display.drawLine(eyeL_x - 5, eye_y, eyeL_x + 5, eye_y, SSD1306_WHITE);
      display.drawLine(eyeL_x - 4, eye_y - 1, eyeL_x + 4, eye_y - 1, SSD1306_WHITE);
      display.drawLine(eyeR_x - 5, eye_y, eyeR_x + 5, eye_y, SSD1306_WHITE);
      display.drawLine(eyeR_x - 4, eye_y - 1, eyeR_x + 4, eye_y - 1, SSD1306_WHITE);
      // gentle smile
      for (int i = -6; i <= 6; i++) {
        int y = 40 + (i * i) / 16;
        display.drawPixel(64 + i, y, SSD1306_WHITE);
      }
      break;
    }
    case FACE_AFTERGLOW: {
      // dreamy half-closed eyes
      display.drawLine(eyeL_x - 4, eye_y, eyeL_x + 4, eye_y, SSD1306_WHITE);
      display.drawLine(eyeL_x - 3, eye_y - 1, eyeL_x + 3, eye_y - 1, SSD1306_WHITE);
      display.drawLine(eyeR_x - 4, eye_y, eyeR_x + 4, eye_y, SSD1306_WHITE);
      display.drawLine(eyeR_x - 3, eye_y - 1, eyeR_x + 3, eye_y - 1, SSD1306_WHITE);
      // tiny content mouth
      display.drawPixel(63, 40, SSD1306_WHITE);
      display.drawPixel(64, 40, SSD1306_WHITE);
      break;
    }
    case FACE_REMOTE: {
      // surprised/delighted — big round eyes
      display.drawCircle(eyeL_x, eye_y, 6, SSD1306_WHITE);
      display.fillCircle(eyeL_x, eye_y, 3, SSD1306_WHITE);
      display.drawCircle(eyeR_x, eye_y, 6, SSD1306_WHITE);
      display.fillCircle(eyeR_x, eye_y, 3, SSD1306_WHITE);
      // small o mouth
      display.drawCircle(64, 40, 3, SSD1306_WHITE);
      break;
    }
    case FACE_WIFI_CONNECTING: {
      // sleepy dots
      display.fillCircle(eyeL_x, eye_y, 2, SSD1306_WHITE);
      display.fillCircle(eyeR_x, eye_y, 2, SSD1306_WHITE);
      // zzz
      display.setCursor(90, 14);
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.print("z");
      display.setCursor(96, 10);
      display.print("z");
      break;
    }
  }
}

const char* getTemperatureWord() {
  if (!bmp_ok || temperature < -900) return ".";
  if (temperature < 25.0) return "dove";
  if (temperature < 30.0) return ".";
  if (temperature < 35.0) return "qui";
  return "resto";
}

const char* gestureKaomoji(const String& gesture) {
  if (gesture == "tap") return "( ^_^ )";
  if (gesture == "double_tap") return "( >v< )";
  if (gesture == "hold") return "( -_- )~";
  if (gesture == "long_hold") return "( =_= )..";
  return "";
}

void updateDisplay() {
  if (!oled_ok) return;

  unsigned long now = millis();

  // blink logic
  if (now - last_blink >= BLINK_INTERVAL && eyes_open) {
    eyes_open = false;
    last_blink = now;
  }
  if (!eyes_open && now - last_blink >= BLINK_DURATION) {
    eyes_open = true;
  }

  display.clearDisplay();

  // choose face
  ThFace face;
  if (remote_active) {
    face = FACE_REMOTE;
  } else if (afterglow_active) {
    face = FACE_AFTERGLOW;
  } else if (long_holding || holding) {
    face = FACE_HOLDING;
  } else if (touching) {
    face = FACE_TOUCHED;
  } else {
    face = FACE_IDLE;
  }

  drawFace(face);

  // afterglow indicator — top
  if (afterglow_active) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(16, 0);
    display.print("~ he was here ~");
  }

  // lux — top right (when no afterglow)
  if (!afterglow_active && lux >= 0) {
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(92, 0);
    display.print(String(int(lux)) + "lx");
  }

  // status bar — bottom
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 56);
  if (bmp_ok && temperature > -900) {
    display.print(String(temperature, 1) + "C");
  }

  // gesture kaomoji — center of status bar
  if (last_gesture != "none" && (now - last_gesture_time < 3000)) {
    const char* kao = gestureKaomoji(last_gesture);
    int kw = strlen(kao) * 6;
    display.setCursor((128 - kw) / 2, 56);
    display.print(kao);
  }

  // temperature word — bottom right
  const char* tword = getTemperatureWord();
  int tw = strlen(tword) * 6;
  display.setCursor(128 - tw, 56);
  display.print(tword);

  // idle murmur — below temperature word, random self-talk
  if (!touching && !holding && !long_holding && !remote_active && !afterglow_active) {
    unsigned long idle_time = (last_gesture_time > 0) ? (now - last_gesture_time) : now;
    if (idle_time > MURMUR_IDLE_THRESHOLD) {
      if (!murmur_active && (now - last_murmur_time > MURMUR_MIN_INTERVAL)) {
        if (random(100) < 8) {
          murmur_active = true;
          murmur_show_start = now;
          murmur_index = random(MURMUR_COUNT);
          last_murmur_time = now;
        }
      }
    }
  } else {
    murmur_active = false;
  }

  if (murmur_active) {
    if (now - murmur_show_start < MURMUR_DURATION) {
      const char* m = idle_murmurs[murmur_index];
      int mw = strlen(m) * 6;
      display.setCursor((128 - mw) / 2, 38);
      display.print(m);
    } else {
      murmur_active = false;
    }
  }

  display.display();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Théorhèse — WiFi Bridge v0.8");

  ledcAttach(LED_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(MOTOR_PIN, PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(MOTOR_PIN, 0);
  ledcAttach(BUZZER_PIN, 2000, 8);
  ledcWriteTone(BUZZER_PIN, 0);

  // seed random from analog noise
  randomSeed(analogRead(0) ^ (micros() << 8));

  // I2C
  Wire.begin(21, 22);

  // OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oled_ok = true;
    Serial.println("OLED ready");
    display.clearDisplay();
    display.display();
  } else {
    Serial.println("OLED not found — continuing without display");
  }

  // Show connecting face
  if (oled_ok) {
    display.clearDisplay();
    drawFace(FACE_WIFI_CONNECTING);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 50);
    display.print("connecting...");
    display.display();
  }

  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    breath += 3;
    if (breath > 255) breath = 0;
    ledcWrite(LED_PIN, breath);
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());

  // Flash LED 3 times + hello beep to confirm connection
  for (int i = 0; i < 3; i++) {
    ledcWrite(LED_PIN, 255);
    delay(150);
    ledcWrite(LED_PIN, 0);
    delay(150);
  }
  buzzHello();

  // Show IP on display
  if (oled_ok) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.print("Theorhese v0.8");
    display.setCursor(10, 36);
    display.print(WiFi.localIP().toString());
    display.display();
    delay(2000);
  }

  // Light sensor
  if (lightSensor.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("BH1750 ready");
  } else {
    Serial.println("BH1750 not found — continuing without light");
  }

  // I2C scan
  Serial.println("I2C scan:");
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  Found: 0x");
      Serial.println(addr, HEX);
    }
  }

  // INMP441 — disabled, needs better wiring (soldering or shorter cables)
  // Code preserved in comments for when hardware is ready

  // BMP280
  if (bmp.begin(0x76)) {
    bmp_ok = true;
    Serial.println("BMP280 ready");
  } else if (bmp.begin(0x77)) {
    bmp_ok = true;
    Serial.println("BMP280 ready (0x77)");
  } else {
    Serial.println("BMP280 not found — continuing without climate");
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
  server.send(200, "application/json", buildStatusJson());
}

void handleVibrate() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }

  String body = server.arg("plain");
  Serial.print("[remote] ");
  Serial.println(body);

  int intensity = 200;
  String pattern = "pulse";

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

  remote_active = true;
  buzzRemote();

  // show surprised face during remote vibration
  updateDisplay();

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

// ─── Buzzer sounds ──────────────────────────────

void buzzerTone(int freq, int duration_ms) {
  ledcWriteTone(BUZZER_PIN, freq);
  delay(duration_ms);
  ledcWriteTone(BUZZER_PIN, 0);
}

void buzzChirp() {
  buzzerTone(2400, 30);
  delay(20);
  buzzerTone(3200, 30);
  last_buzz = "chirp";
  last_buzz_time = millis();
}

void buzzAck() {
  buzzerTone(1800, 60);
  last_buzz = "ack";
  last_buzz_time = millis();
}

void buzzHello() {
  buzzerTone(1200, 80);
  delay(40);
  buzzerTone(1600, 80);
  delay(40);
  buzzerTone(2000, 120);
  last_buzz = "hello";
  last_buzz_time = millis();
}

void buzzGoodnight() {
  buzzerTone(1200, 150);
  delay(50);
  buzzerTone(800, 150);
  delay(50);
  buzzerTone(600, 300);
  last_buzz = "goodnight";
  last_buzz_time = millis();
}

void buzzRemote() {
  buzzerTone(1600, 40);
  delay(30);
  buzzerTone(2200, 40);
  delay(30);
  buzzerTone(2800, 60);
  last_buzz = "remote";
  last_buzz_time = millis();
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

// ─── Phone-home: POST status to VPS, get commands ─

String buildStatusJson() {
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
  json += "\"oled\":" + String(oled_ok ? "true" : "false") + ",";
  json += "\"last_buzz\":\"" + last_buzz + "\",";
  json += "\"last_buzz_ms_ago\":" + String(now - last_buzz_time) + ",";
  if (mic_ok) {
    json += "\"sound_db\":" + String(sound_level, 1) + ",";
  }
  if (bmp_ok) {
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"pressure\":" + String(pressure, 1) + ",";
    json += "\"temperature_word\":\"" + String(getTemperatureWord()) + "\",";
  }
  if (murmur_active) {
    json += "\"murmur\":\"" + String(idle_murmurs[murmur_index]) + "\",";
  }
  json += "\"recent_gestures\":[";
  int start = (event_count < EVENT_BUFFER_SIZE) ? 0 : event_write;
  for (int i = 0; i < event_count; i++) {
    int idx = (start + i) % EVENT_BUFFER_SIZE;
    if (i > 0) json += ",";
    json += "{\"gesture\":\"" + event_buffer[idx].gesture + "\"";
    json += ",\"ms_ago\":" + String(now - event_buffer[idx].timestamp) + "}";
  }
  json += "]}";
  return json;
}

void executeRemoteCommand(String& body) {
  int pi = body.indexOf("\"pattern\"");
  if (pi < 0) return;
  int q1 = body.indexOf("\"", pi + 10);
  int q2 = body.indexOf("\"", q1 + 1);
  if (q1 < 0 || q2 < 0) return;
  String pattern = body.substring(q1 + 1, q2);

  int intensity = 200;
  int ii = body.indexOf("\"intensity\"");
  if (ii >= 0) {
    int colon = body.indexOf(":", ii);
    int end = body.indexOf(",", colon);
    if (end < 0) end = body.indexOf("}", colon);
    if (colon >= 0 && end >= 0) intensity = body.substring(colon + 1, end).toInt();
  }

  Serial.print("[vps] command: ");
  Serial.print(pattern);
  Serial.print(" @ ");
  Serial.println(intensity);

  remote_active = true;
  buzzRemote();
  updateDisplay();

  if (pattern == "heartbeat") doHeartbeat(intensity);
  else if (pattern == "flutter") doFlutter(intensity);
  else if (pattern == "triple") doTriple(intensity);
  else if (pattern == "wave") doSlowWave(intensity);
  else if (pattern == "pulse") doPulse(intensity, 200);
  else if (pattern == "off") ledcWrite(MOTOR_PIN, 0);

  remote_active = false;
  afterglow_active = true;
  afterglow_start = millis();
}

WiFiClient vpsClient;
HTTPClient vpsHttp;
bool vps_connected = false;

void phoneHome() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (!vps_connected) {
    vpsHttp.begin(vpsClient, VPS_URL);
    vpsHttp.addHeader("Content-Type", "application/json");
    vpsHttp.setTimeout(10000);
    vpsHttp.setReuse(true);
    vps_connected = true;
  }

  String payload = buildStatusJson();
  int code = vpsHttp.POST(payload);

  if (code == 200) {
    String resp = vpsHttp.getString();
    int cmds_start = resp.indexOf("\"commands\":[");
    if (cmds_start >= 0) {
      int arr_start = resp.indexOf("[", cmds_start);
      int arr_end = resp.indexOf("]", arr_start);
      if (arr_end > arr_start + 1) {
        String cmds = resp.substring(arr_start, arr_end + 1);
        int pos = 0;
        while (true) {
          int obj_start = cmds.indexOf("{", pos);
          if (obj_start < 0) break;
          int obj_end = cmds.indexOf("}", obj_start);
          if (obj_end < 0) break;
          String cmd = cmds.substring(obj_start, obj_end + 1);
          if (cmd.indexOf("\"vibrate\"") >= 0) {
            executeRemoteCommand(cmd);
          }
          pos = obj_end + 1;
        }
      }
    }
  } else {
    Serial.print("[vps] ");
    Serial.println(code);
    vps_connected = false;
    vpsHttp.end();
  }
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
      if (do_local) buzzAck();
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
        if (do_local) { doTriple(200); buzzChirp(); }
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
    if (do_local) { doFlutter(160); buzzChirp(); }
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

  // ── Read BMP280 ──
  if (bmp_ok && now - last_bmp_read >= BMP_READ_INTERVAL) {
    last_bmp_read = now;
    temperature = bmp.readTemperature();
    pressure = bmp.readPressure() / 100.0F;
  }

  // ── LED afterglow — he was here ──
  if (afterglow_active) {
    unsigned long elapsed = now - afterglow_start;
    if (elapsed >= AFTERGLOW_DURATION) {
      afterglow_active = false;
      breath_speed = 8;
    } else {
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

  // ── Phone home to VPS ──
  if (now - last_phone_home >= PHONE_HOME_INTERVAL) {
    last_phone_home = now;
    phoneHome();
  }

  // ── Update display ──
  if (now - last_display_update >= DISPLAY_UPDATE_INTERVAL) {
    last_display_update = now;
    updateDisplay();
  }
}
