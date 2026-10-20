// Grove Infrared Emitter -> sends a text message as slow on/off light pulses.
// The message can be set in the code, or sent over MQTT to class/NAME/ir/send.
// Board: Arduino UNO R4 WiFi
// Wiring (Grove-to-jumper cable, standard Grove colours):
//   yellow -> D3 (signal)
//   white  -> not connected
//   red    -> 5V
//   black  -> GND
// Libraries: ArduinoMqttClient (WiFiS3 comes with the board package).
//            No IR library: the LED is simply switched on and off (no 38 kHz carrier),
//            because the receiving Grove Light Sensor is a slow photoresistor.
//
// Protocol (must match ir_receiver.ino, same SLOT_MS):
//   start marker: 3 slots ON, 1 slot OFF
//   then every character as 8 slots (ON = 1, OFF = 0), most significant bit first
//   the message ends with the character '\n'
//
// Topics:  class/NAME/ir/send   (subscribe)  text to transmit
//          class/NAME/ir/sent   (publish)    text that was transmitted

#include <WiFiS3.h>
#include <ArduinoMqttClient.h>
#include <Arduino_LED_Matrix.h>

// ---- CHANGE THESE ----
const char ssid[]     = "CLASS_WIFI";
const char pass[]     = "WIFI_PASSWORD";
const char mqttUser[] = "TOKEN_KEY_OR_INSTANCE";
const char mqttPass[] = "TOKEN_SECRET";
const char myName[]   = "anna";            // unique per board
const char MESSAGE[]  = "HELLO";           // sent at start-up (empty "" = nothing)
const unsigned long REPEAT_MS = 30000;     // send MESSAGE again every 30 s (0 = never)
// ----------------------

const char mqttHost[] = "defence4sme.cloud.shiftr.io";
const int  mqttPort   = 1883;

const int IR_PIN = 3;
const int SLOT_MS = 100;                   // length of one on/off slot. 8 slots = 1 character
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

String pending = "";                       // message received over MQTT, waiting to be sent
unsigned long lastTx = 0;
unsigned long slotEnd = 0;

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

void onMessage(int size) {
  String body = "";
  while (mqtt.available()) body += (char)mqtt.read();
  body.trim();
  if (body.length() > 0) pending = body;   // handled in loop(), not here
}

void connectMqtt() {
  showIcon(ICON_M);
  mqtt.setId(String(myName) + "-irtx");
  mqtt.setUsernamePassword(mqttUser, mqttPass);
  if (mqtt.connect(mqttHost, mqttPort)) {
    Serial.println("MQTT connected");
    showIcon(ICON_OK);
    delay(1500);
    showIcon(ICON_ONLINE);
    mqtt.onMessage(onMessage);
    mqtt.subscribe(topic("ir/send"));
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

String clean(String s) {
  String out = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c >= 32 && c < 127) out += c;      // printable ASCII only
  }
  if (out.length() > MAX_CHARS) out = out.substring(0, MAX_CHARS);
  return out;
}

// Holds the LED state for one slot. The slot times are kept on a fixed schedule,
// so small delays do not add up.
void sendSlot(bool on) {
  digitalWrite(IR_PIN, on ? HIGH : LOW);
  slotEnd += SLOT_MS;
  while ((long)(millis() - slotEnd) < 0) {
    mqtt.poll();                           // keeps the MQTT connection alive
  }
}

void sendMessage(String msg) {
  msg = clean(msg);
  if (msg.length() == 0) return;
  Serial.print("Sending: ");
  Serial.println(msg);

  String full = msg + "\n";
  slotEnd = millis();

  sendSlot(true);  sendSlot(true);  sendSlot(true);  sendSlot(false);   // start marker
  for (unsigned int i = 0; i < full.length(); i++) {
    uint8_t c = full[i];
    for (int b = 7; b >= 0; b--) sendSlot((c >> b) & 1);
  }
  digitalWrite(IR_PIN, LOW);

  lastTx = millis();
  if (mqtt.connected()) {
    mqtt.beginMessage(topic("ir/sent"));
    mqtt.print(msg);
    mqtt.endMessage();
  }
}

void setup() {
  Serial.begin(9600);
  matrix.begin();
  showIcon(ICON_ALL);      // lamp test
  delay(500);
  pinMode(IR_PIN, OUTPUT);
  digitalWrite(IR_PIN, LOW);
  connectWifi();
  connectMqtt();
  if (strlen(MESSAGE) > 0) sendMessage(MESSAGE);
}

void loop() {
  keepConnected();

  if (pending.length() > 0) {
    String m = pending;
    pending = "";
    sendMessage(m);
  } else if (REPEAT_MS > 0 && strlen(MESSAGE) > 0 && millis() - lastTx >= REPEAT_MS) {
    sendMessage(MESSAGE);
  }
}
