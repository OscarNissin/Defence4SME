// Grove Light Sensor v1.2 -> WiFi -> MQTT (Shiftr)
// Board: Arduino UNO R4 WiFi
// Wiring (Grove-to-jumper cable, standard Grove colours):
//   yellow -> A0 (signal)
//   white  -> not connected
//   red    -> 5V
//   black  -> GND
// Libraries: ArduinoMqttClient (WiFiS3 comes with the board package)
//
// Topics:  class/NAME/light          raw value 0-1023 (higher = brighter)
//          class/NAME/light_percent  the same as 0-100

#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include <Arduino_LED_Matrix.h>

// ---- CHANGE THESE ----
const char ssid[]     = "CLASS_WIFI";
const char pass[]     = "WIFI_PASSWORD";
const char mqttUser[] = "TOKEN_KEY_OR_INSTANCE";
const char mqttPass[] = "TOKEN_SECRET";
const char myName[]   = "anna";            // unique per board
// ----------------------

const char mqttHost[] = "defence4sme.cloud.shiftr.io";
const int  mqttPort   = 1883;

const int LIGHT_PIN = A0;
const unsigned long INTERVAL_MS = 1000;    // how often to publish

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

unsigned long last = 0;

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
  mqtt.setId(String(myName) + "-light");
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

void publishNumber(const String &t, float value, int decimals) {
  mqtt.beginMessage(t);
  mqtt.print(value, decimals);
  mqtt.endMessage();
}

void setup() {
  Serial.begin(9600);
  matrix.begin();
  showIcon(ICON_ALL);      // lamp test
  delay(500);
  analogReadResolution(10);                // 0-1023
  connectWifi();
  connectMqtt();
}

void loop() {
  keepConnected();

  if (millis() - last >= INTERVAL_MS) {
    last = millis();

    long sum = 0;
    for (int i = 0; i < 10; i++) {         // average 10 readings to reduce noise
      sum += analogRead(LIGHT_PIN);
      delay(2);
    }
    int raw = sum / 10;
    int percent = raw * 100L / 1023;

    Serial.print("light: ");
    Serial.println(raw);

    if (mqtt.connected()) {
      publishNumber(topic("light"), raw, 0);
      publishNumber(topic("light_percent"), percent, 0);
      flashIcon(ICON_SEND, 80);
    }
  }
}
