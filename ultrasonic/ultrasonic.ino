// Grove Ultrasonic Distance Sensor v2.0 -> WiFi -> MQTT (Shiftr)
// Board: Arduino UNO R4 WiFi
// Wiring (Grove-to-jumper cable, standard Grove colours):
//   yellow -> D7 (signal)
//   white  -> not connected
//   red    -> 5V
//   black  -> GND
// Libraries: ArduinoMqttClient (WiFiS3 comes with the board package).
//            No sensor library needed.
//
// Topic:   class/NAME/distance   distance in centimetres (roughly 3-350 cm)
// The LED matrix shows the distance in cm (3 digits) and a bar on the bottom row.

#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include <Arduino_LED_Matrix.h>

// ---- CHANGE THESE ----
const char ssid[]     = "Othrys2G";
const char pass[]     = "22122017Nissin";
const char mqttUser[] = "defence4sme";
const char mqttPass[] = "22KjngRJZIpVbdPR";
const char myName[]   = "distancesensor2";            // unique per board
// ----------------------

const char mqttHost[] = "defence4sme.cloud.shiftr.io";
const int  mqttPort   = 1883;

const int SIG_PIN = 7;
const unsigned long INTERVAL_MS = 500;

WiFiClient net;
MqttClient mqtt(net);

// ---- LED matrix diagnostics -------------------------------------------
// What the matrix shows:
//   all LEDs on      board started (lamp test)
//   blinking W       connecting to WiFi
//   M                connecting to the MQTT broker (Shiftr)
//   check mark       connected, everything OK
//   small dot        online, waiting for the first reading
//   number           distance in cm, with a bar on the bottom row
//   ---              no reliable reading (out of range)
//   X                MQTT connection failed, retrying
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

// ---- Distance on the LED matrix: tiny 3x5 digits, no extra library -----
const char *const DIGITS[11][5] = {
  {"###", "#.#", "#.#", "#.#", "###"},   // 0
  {".#.", "##.", ".#.", ".#.", "###"},   // 1
  {"###", "..#", "###", "#..", "###"},   // 2
  {"###", "..#", "###", "..#", "###"},   // 3
  {"#.#", "#.#", "###", "..#", "..#"},   // 4
  {"###", "#..", "###", "..#", "###"},   // 5
  {"###", "#..", "###", "#.#", "###"},   // 6
  {"###", "..#", "..#", "..#", "..#"},   // 7
  {"###", "#.#", "###", "#.#", "###"},   // 8
  {"###", "#.#", "###", "..#", "###"},   // 9
  {"...", "...", "###", "...", "..."}    // 10 = minus sign
};
const int BAR_MAX_CM = 120;              // the bar on the bottom row is full at this distance

// cm < 0 shows "---" (no reliable reading)
void showDistance(int cm) {
  byte frame[8][12] = {0};
  int digit[3];
  int n;

  if (cm < 0) {
    n = 3;
    digit[0] = digit[1] = digit[2] = 10;
  } else {
    if (cm > 999) cm = 999;
    String s = String(cm);
    n = s.length();
    for (int i = 0; i < n; i++) digit[i] = s[i] - '0';
  }

  int x0 = (12 - (n * 4 - 1)) / 2;       // centre the digits
  for (int d = 0; d < n; d++) {
    for (int r = 0; r < 5; r++) {
      for (int c = 0; c < 3; c++) {
        if (DIGITS[digit[d]][r][c] == '#') frame[r + 1][x0 + d * 4 + c] = 1;
      }
    }
  }

  if (cm >= 0) {                         // bar: longer = farther away
    int bar = (long)cm * 12 / BAR_MAX_CM;
    if (bar < 1) bar = 1;
    if (bar > 12) bar = 12;
    for (int c = 0; c < bar; c++) frame[7][c] = 1;
  }
  matrix.renderBitmap(frame, 8, 12);
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
  mqtt.setId(String(myName) + "-distance");
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

// One measurement. Returns -1 if nothing was measured.
float singleReadCm() {
  pinMode(SIG_PIN, OUTPUT);
  digitalWrite(SIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(SIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(SIG_PIN, LOW);
  pinMode(SIG_PIN, INPUT);

  unsigned long duration = pulseIn(SIG_PIN, HIGH, 30000);   // timeout 30 ms
  if (duration == 0) return -1;
  float cm = duration / 58.0;                               // sound: about 58 us per cm (round trip)
  if (cm < 2 || cm > 400) return -1;
  return cm;
}

// Median of 5 measurements, which removes single wrong readings. Returns -1 if unreliable.
float readCm() {
  float v[5];
  int n = 0;
  for (int i = 0; i < 5; i++) {
    float cm = singleReadCm();
    if (cm > 0) v[n++] = cm;
    delay(30);
  }
  if (n < 3) return -1;
  for (int i = 0; i < n - 1; i++) {
    for (int j = 0; j < n - 1 - i; j++) {
      if (v[j] > v[j + 1]) {
        float tmp = v[j];
        v[j] = v[j + 1];
        v[j + 1] = tmp;
      }
    }
  }
  return v[n / 2];
}

void setup() {
  Serial.begin(9600);
  matrix.begin();
  showIcon(ICON_ALL);      // lamp test
  delay(500);
  connectWifi();
  connectMqtt();
}

void loop() {
  keepConnected();

  if (millis() - last >= INTERVAL_MS) {
    last = millis();

    float cm = readCm();
    if (cm < 0) {
      Serial.println("No reliable reading (out of range?)");
      showDistance(-1);
      return;
    }
    Serial.print("distance: ");
    Serial.print(cm, 1);
    Serial.println(" cm");

    showDistance((int)round(cm));

    if (mqtt.connected()) publishNumber(topic("distance"), cm, 1);
  }
}
