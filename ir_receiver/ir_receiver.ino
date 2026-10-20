// Grove Light Sensor v1.2 as a receiver for ir_emitter.ino -> decoded text to MQTT (Shiftr)
// Board: Arduino UNO R4 WiFi
// Wiring (Grove-to-jumper cable, standard Grove colours):
//   yellow -> A0 (signal)
//   white  -> not connected
//   red    -> 5V
//   black  -> GND
//   Aim the sensor straight at the IR LED of the emitter.
// Libraries: ArduinoMqttClient (WiFiS3 comes with the board package)
//
// How it works: the sketch watches the light level. When the level jumps above the
// idle level (start marker: 3 slots ON, 1 OFF), it measures "dark" and "light",
// sets a threshold halfway, and reads the following slots as bits (8 per character).
// The message ends with '\n'.
//
// Topics:  class/NAME/ir/rx      decoded text
//          class/NAME/ir/error   problems (weak signal, no end marker)

#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include <Arduino_LED_Matrix.h>

// ---- CHANGE THESE ----
const char ssid[]     = "CLASS_WIFI";
const char pass[]     = "WIFI_PASSWORD";
const char mqttUser[] = "TOKEN_KEY_OR_INSTANCE";
const char mqttPass[] = "TOKEN_SECRET";
const char myName[]   = "bob";             // unique per board
// ----------------------

const char mqttHost[] = "defence4sme.cloud.shiftr.io";
const int  mqttPort   = 1883;

const int LIGHT_PIN = A0;
const int SLOT_MS   = 100;                 // must match the emitter
const int STEP_MIN  = 25;                  // how much brighter than idle counts as "ON" (0-1023 scale)
const int MAX_CHARS = 32;

WiFiClient net;
MqttClient mqtt(net);

// ---- LED matrix diagnostics -------------------------------------------
// What the matrix shows:
//   all LEDs on      board started (lamp test)
//   blinking W       connecting to WiFi
//   M                connecting to the MQTT broker (Shiftr)
//   check mark       connected, everything OK
//   small dot        online (flashes bigger when a message is sent)
//   X                MQTT connection failed, retrying
//   !                sensor problem
ArduinoLEDMatrix matrix;

const char *const ICON_ALL[8] = {
  "############",
  "############",
  "############",
  "############",
  "############",
  "############",
  "############",
  "############",
};

const char *const ICON_W[8] = {
  "...#.....#..",
  "...#.....#..",
  "...#.....#..",
  "...#..#..#..",
  "...#..#..#..",
  "...#.#.#.#..",
  "....#...#...",
  "............",
};

const char *const ICON_M[8] = {
  "...#.....#..",
  "...##...##..",
  "...#.#.#.#..",
  "...#..#..#..",
  "...#.....#..",
  "...#.....#..",
  "...#.....#..",
  "............",
};

const char *const ICON_OK[8] = {
  "............",
  "..........#.",
  ".........##.",
  "#.......##..",
  "##.....##...",
  ".##...##....",
  "..##.##.....",
  "...###......",
};

const char *const ICON_X[8] = {
  "............",
  "...#....#...",
  "....#..#....",
  ".....##.....",
  ".....##.....",
  "....#..#....",
  "...#....#...",
  "............",
};

const char *const ICON_BANG[8] = {
  "............",
  ".....##.....",
  ".....##.....",
  ".....##.....",
  ".....##.....",
  "............",
  ".....##.....",
  "............",
};

const char *const ICON_ONLINE[8] = {
  "............",
  "............",
  "............",
  ".....##.....",
  ".....##.....",
  "............",
  "............",
  "............",
};

const char *const ICON_SEND[8] = {
  "............",
  "............",
  "...######...",
  "...######...",
  "...######...",
  "...######...",
  "............",
  "............",
};

void showIcon(const char *const art[8]) {
  byte frame[8][12];
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 12; c++) {
      frame[r][c] = (art[r][c] == '#') ? 1 : 0;
    }
  }
  matrix.renderBitmap(frame, 8, 12);
}

// Blinks an icon without blocking. Call it often.
void blinkIcon(const char *const art[8]) {
  static unsigned long lastToggle = 0;
  static bool on = false;
  if (millis() - lastToggle >= 400) {
    lastToggle = millis();
    on = !on;
    if (on) showIcon(art);
    else matrix.clear();
  }
}

// Shows an icon for a moment, then goes back to the "online" dot.
void flashIcon(const char *const art[8], unsigned int ms) {
  showIcon(art);
  delay(ms);
  showIcon(ICON_ONLINE);
}
// ------------------------------------------------------------------------

float baseline = -1;                       // slowly following idle light level

String topic(const char *leaf) {
  return String("class/") + myName + "/" + leaf;
}

void connectWifi() {
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connecting to WiFi...");
    showIcon(ICON_W);
    WiFi.begin(ssid, pass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
      blinkIcon(ICON_W);
      delay(50);
    }
  }
  Serial.println("WiFi OK");
}

void connectMqtt() {
  showIcon(ICON_M);
  mqtt.setId(String(myName) + "-irrx");
  mqtt.setUsernamePassword(mqttUser, mqttPass);
  if (mqtt.connect(mqttHost, mqttPort)) {
    Serial.println("MQTT connected");
    showIcon(ICON_OK);
    delay(1500);
    showIcon(ICON_ONLINE);
  } else {
    Serial.print("MQTT failed, error ");
    Serial.println(mqtt.connectError());
    showIcon(ICON_X);
    delay(2000);
  }
}

void keepConnected() {
  if (WiFi.status() != WL_CONNECTED) connectWifi();
  if (!mqtt.connected()) {
    connectMqtt();
  }
  mqtt.poll();
}

void publishText(const char *leaf, const String &text) {
  if (!mqtt.connected()) return;
  mqtt.beginMessage(topic(leaf));
  mqtt.print(text);
  mqtt.endMessage();
}

// Waits until time t. Polls MQTT while there is plenty of time left,
// and busy-waits the last 30 ms for accurate timing.
void waitUntil(unsigned long t) {
  while ((long)(t - millis()) > 30) mqtt.poll();
  while ((long)(t - millis()) > 0) { }
}

int readAvg(int n) {
  long sum = 0;
  for (int i = 0; i < n; i++) {
    sum += analogRead(LIGHT_PIN);
    delay(3);
  }
  return sum / n;
}

// Light level around time mid (average of 5 readings)
float sampleAt(unsigned long mid) {
  waitUntil(mid - 10);
  return readAvg(5);
}

// t0 = time when the light first went above the idle level
void receiveMessage(unsigned long t0, float dark) {
  // Measure the ON level in the middle of the start marker (slots 1-3 are ON)
  waitUntil(t0 + SLOT_MS * 6 / 5);
  unsigned long tEnd = t0 + SLOT_MS * 5 / 2;
  long sum = 0;
  int n = 0;
  while (millis() < tEnd) {
    sum += analogRead(LIGHT_PIN);
    n++;
    delay(5);
  }
  if (n == 0) return;
  float lit = (float)sum / n;

  Serial.print("Start marker. dark=");
  Serial.print(dark, 0);
  Serial.print(" lit=");
  Serial.println(lit, 0);

  if (lit - dark < STEP_MIN) {
    publishText("ir/error", "weak signal");
    flashIcon(ICON_X, 1000);
    return;
  }
  float threshold = (dark + lit) / 2;

  // Data starts after the 4-slot start marker
  unsigned long base = t0 + 4UL * SLOT_MS;
  String msg = "";
  bool ended = false;

  for (int c = 0; c <= MAX_CHARS; c++) {
    uint8_t ch = 0;
    for (int b = 0; b < 8; b++) {
      unsigned long mid = base + (unsigned long)(c * 8 + b) * SLOT_MS + SLOT_MS / 2;
      ch = (ch << 1) | (sampleAt(mid) > threshold ? 1 : 0);
    }
    if (ch == '\n') {
      ended = true;
      break;
    }
    msg += (ch >= 32 && ch < 127) ? (char)ch : '?';
  }

  Serial.print("Received: ");
  Serial.println(msg);
  if (ended) {
    publishText("ir/rx", msg);
    flashIcon(ICON_OK, 1500);
  } else {
    publishText("ir/error", "no end marker: " + msg);
    flashIcon(ICON_X, 1500);
  }
}

void setup() {
  Serial.begin(9600);
  matrix.begin();
  showIcon(ICON_ALL);      // lamp test
  delay(500);
  analogReadResolution(10);
  connectWifi();
  connectMqtt();
}

void loop() {
  keepConnected();

  int v = analogRead(LIGHT_PIN);
  if (baseline < 0) baseline = v;

  if (v > baseline + STEP_MIN) {
    unsigned long t0 = millis();
    bool stays = true;
    for (int i = 0; i < 3; i++) {          // must stay bright for 30 ms, to ignore short flashes
      delay(10);
      if (analogRead(LIGHT_PIN) <= baseline + STEP_MIN) {
        stays = false;
        break;
      }
    }
    if (stays) {
      receiveMessage(t0, baseline);
      delay(SLOT_MS * 3);
      baseline = readAvg(10);
    }
  } else {
    baseline = baseline * 0.98 + v * 0.02; // follow slow changes in room light
  }
  delay(10);
}
