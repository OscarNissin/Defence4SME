// Grove 3-Axis Digital Accelerometer (+-16g, ADXL345) -> WiFi -> MQTT (Shiftr)
// Board: Arduino UNO R4 WiFi
// Wiring (Grove-to-jumper cable, standard Grove colours):
//   yellow -> SCL (pin A5, or the SCL pin next to AREF)
//   white  -> SDA (pin A4, or the SDA pin next to AREF)
//   red    -> 5V
//   black  -> GND
// Libraries: ArduinoMqttClient (WiFiS3 and Wire come with the board package).
//            No sensor library needed, the sensor is read directly over I2C.
//
// Topics:  class/NAME/accel/x, /y, /z   acceleration in g
//          class/NAME/accel/total       length of the vector (about 1.0 at rest)
//          class/NAME/accel/xyz         "x,y,z" in g, sent often, for animations

#include <WiFiS3.h>
#include <Wire.h>
#include <ArduinoMqttClient.h>
#include <Arduino_LED_Matrix.h>

// ---- CHANGE THESE ----
const char ssid[]     = "Othrys2G";
const char pass[]     = "22122017Nissin";
const char mqttUser[] = "defence4sme";
const char mqttPass[] = "22KjngRJZIpVbdPR";
const char myName[]   = "Accelerometer1";            // unique per board
// ----------------------

const char mqttHost[] = "defence4sme.cloud.shiftr.io";
const int  mqttPort   = 1883;

const unsigned long INTERVAL_MS = 500;     // x, y, z, total: 4 messages per round, so don't go too low
const unsigned long FAST_MS = 200;         // accel/xyz for animations (lower = smoother, more messages)

// ADXL345 registers
const uint8_t ADXL_ADDR     = 0x53;
const uint8_t REG_DEVID     = 0x00;        // should read 0xE5
const uint8_t REG_BW_RATE   = 0x2C;
const uint8_t REG_POWER_CTL = 0x2D;
const uint8_t REG_DATA_FMT  = 0x31;
const uint8_t REG_DATAX0    = 0x32;

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
unsigned long lastFast = 0;

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
  mqtt.setId(String(myName) + "-accel");
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

// ---- ADXL345 over I2C ----
void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADXL_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

int readReg(uint8_t reg) {
  Wire.beginTransmission(ADXL_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return -1;
  if (Wire.requestFrom((uint8_t)ADXL_ADDR, (uint8_t)1) != 1) return -1;
  return Wire.read();
}

bool initAccel() {
  if (readReg(REG_DEVID) != 0xE5) return false;
  writeReg(REG_DATA_FMT, 0x0B);            // full resolution, +-16 g
  writeReg(REG_BW_RATE, 0x0A);             // 100 Hz output rate
  writeReg(REG_POWER_CTL, 0x08);           // start measuring
  return true;
}

bool readAccel(float &x, float &y, float &z) {
  Wire.beginTransmission(ADXL_ADDR);
  Wire.write(REG_DATAX0);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)ADXL_ADDR, (uint8_t)6) != 6) return false;

  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();

  int16_t rx = (int16_t)((b[1] << 8) | b[0]);
  int16_t ry = (int16_t)((b[3] << 8) | b[2]);
  int16_t rz = (int16_t)((b[5] << 8) | b[4]);

  const float G_PER_LSB = 0.0039;          // 3.9 mg per step in full resolution mode
  x = rx * G_PER_LSB;
  y = ry * G_PER_LSB;
  z = rz * G_PER_LSB;
  return true;
}

void setup() {
  Serial.begin(9600);
  matrix.begin();
  showIcon(ICON_ALL);      // lamp test
  delay(500);
  Wire.begin();
  while (!initAccel()) {
    Serial.println("ADXL345 not found. Check the yellow (SCL) and white (SDA) jumper wires.");
    showIcon(ICON_BANG);
    delay(1000);
    matrix.clear();
    delay(1000);
  }
  Serial.println("Accelerometer OK");
  connectWifi();
  connectMqtt();
}

void loop() {
  keepConnected();

  if (millis() - lastFast < FAST_MS) return;
  lastFast = millis();

  float x, y, z;
  if (!readAccel(x, y, z)) {
    Serial.println("Read failed");
    return;
  }
  float total = sqrt(x * x + y * y + z * z);

  // Fast topic: all three axes in one message, for the 3D animation
  if (mqtt.connected()) {
    mqtt.beginMessage(topic("accel/xyz"));
    mqtt.print(x, 2);
    mqtt.print(",");
    mqtt.print(y, 2);
    mqtt.print(",");
    mqtt.print(z, 2);
    mqtt.endMessage();
  }

  // Slow topics: one value per message, for the dashboard graphs
  if (millis() - last >= INTERVAL_MS) {
    last = millis();

    Serial.print("x=");  Serial.print(x, 2);
    Serial.print(" y="); Serial.print(y, 2);
    Serial.print(" z="); Serial.print(z, 2);
    Serial.print(" total="); Serial.println(total, 2);

    if (mqtt.connected()) {
      publishNumber(topic("accel/x"), x, 2);
      publishNumber(topic("accel/y"), y, 2);
      publishNumber(topic("accel/z"), z, 2);
      publishNumber(topic("accel/total"), total, 2);
      flashIcon(ICON_SEND, 80);
    }
  }
}
