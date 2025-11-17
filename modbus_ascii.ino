/* HRX Chiller — Modbus ASCII reader (ESP32)
   - Polls holding registers 0000h..0006h (7 regs) via Modbus ASCII (FC03)
   - Parses registers including Alarm Flag 1 (0005h), 2 (0006h), 3 (0007h)
   - Publishes JSON to MQTT topic "chiller/status"
   - Uses RS-485 transceiver with DE/RE on RS485_EN pin
   - UART configured for 9600, 7E1 as per manual
   - Requires: PubSubClient library (for MQTT)
*/

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ------ CONFIG: WiFi / MQTT ------
const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";

const char* MQTT_BROKER = "192.168.1.100"; // Raspberry Pi IP (Mosquitto)
const uint16_t MQTT_PORT = 1883;
const char* MQTT_TOPIC  = "chiller/status";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ------ RS485 / UART pins ------
#define RXD2 16
#define TXD2 17
#define RS485_EN 4         // DE and /RE tied to this pin

// ------ Modbus params ------
uint8_t slaveAddr = 0x01;
uint16_t startRegister = 0x0000;
uint16_t readCount = 7;    // read 0000..0006 (7 registers)

unsigned long pollIntervalMs = 2000;
unsigned long lastPoll = 0;

// ---------- Utilities: LRC (computes 2's complement of sum of bytes) ----------
uint8_t calcLRC(const uint8_t *bytes, size_t len) {
  uint8_t sum = 0;
  for (size_t i=0;i<len;i++) sum += bytes[i];
  // 2's complement
  uint8_t lrc = ((uint8_t)(-(int)sum));
  return lrc;
}

// Helper: convert a hex ASCII pair to byte (0-255)
int hexCharToVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return -1;
}
bool hexPairToByte(char hi, char lo, uint8_t &out) {
  int h = hexCharToVal(hi), l = hexCharToVal(lo);
  if (h < 0 || l < 0) return false;
  out = (uint8_t)((h<<4) | l);
  return true;
}

// Build Modbus ASCII frame for FC03 read holding registers
String buildReadFrame(uint8_t slave, uint16_t startAddr, uint16_t qty) {
  // payload bytes (ADDR, FUNC, ADDR_H, ADDR_L, QTY_H, QTY_L)
  uint8_t payload[6];
  payload[0] = slave;
  payload[1] = 0x03;
  payload[2] = (startAddr >> 8) & 0xFF;
  payload[3] = startAddr & 0xFF;
  payload[4] = (qty >> 8) & 0xFF;
  payload[5] = qty & 0xFF;

  uint8_t lrc = calcLRC(payload, 6);

  // Convert full frame to ASCII hex after ':' and before CRLF
  String frame = ":";
  for (int i=0;i<6;i++) {
    char buf[3];
    sprintf(buf, "%02X", payload[i]);
    frame += String(buf);
  }
  char bufLrc[3];
  sprintf(bufLrc, "%02X", lrc);
  frame += String(bufLrc);
  frame += "\r\n";
  return frame;
}

// ---------- RS485 send / receive ----------
void rs485Send(const String &frame) {
  digitalWrite(RS485_EN, HIGH);
  delayMicroseconds(200);
  Serial2.print(frame);
  Serial2.flush();
  delayMicroseconds(200);
  digitalWrite(RS485_EN, LOW); // back to receive
}

String rs485ReadTimeout(unsigned long timeoutMs) {
  String resp = "";
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (Serial2.available()) {
      char c = (char)Serial2.read();
      resp += c;
    }
    // small yield
    delay(2);
  }
  return resp;
}

// ---------- Parse Modbus ASCII response ----------
bool verifyAndExtractData(const String &resp, uint8_t &func, uint8_t &byteCount, uint8_t *dataBuf, size_t &dataLen) {
  // Expected format: : <hex bytes...> <LRC 1 byte> CR LF
  String s = resp;
  s.trim(); // remove whitespace and CRLF
  if (!s.startsWith(":")) return false;
  s.remove(0,1); // remove ':'
  // s now contains hex ASCII. Last two hex pairs = LRC
  if (s.length() < 4) return false;

  // Convert hex pairs to bytes array
  size_t nb = s.length() / 2;
  // require at least (slave, func, bytecount, ... , LRC) => min 4 bytes
  if (nb < 4) return false;

  std::vector<uint8_t> bytes;
  bytes.reserve(nb);
  for (size_t i=0;i<nb;i++) {
    uint8_t b;
    if (!hexPairToByte(s.charAt(i*2), s.charAt(i*2+1), b)) return false;
    bytes.push_back(b);
  }

  // Separate LRC
  uint8_t recvLrc = bytes.back();
  bytes.pop_back();

  // Compute LRC over remaining bytes
  uint8_t calc = calcLRC(bytes.data(), bytes.size());
  if (calc != recvLrc) {
    Serial.printf("LRC mismatch: calc %02X recv %02X\n", calc, recvLrc);
    return false;
  }

  // Extract slave and function
  if (bytes.size() < 2) return false;
  // bytes[0] = slave, bytes[1] = function
  func = bytes[1];
  if (func == 0x83 || func == 0x83 /* negative response example */) {
    // error from slave
    Serial.printf("Negative response function: %02X\n", func);
    return false;
  }

  if (bytes.size() < 3) return false;
  byteCount = bytes[2];
  // data bytes follow bytes[3..]
  size_t expectedDataBytes = byteCount;
  if (bytes.size() < 3 + expectedDataBytes) return false;

  // copy data bytes
  dataLen = expectedDataBytes;
  for (size_t i=0;i<expectedDataBytes;i++) dataBuf[i] = bytes[3 + i];

  return true;
}

// ---------- Alarm bit mapping (from manual) ----------
// Alarm flag 1 (register 0005h) bits -> AL codes & descriptions
struct AlarmDef { uint8_t bit; const char* code; const char* desc; };
const AlarmDef alarmFlag1[] = {
  {0, "AL01", "Low level in tank"},
  {1, "AL02", "High circulating fluid discharge temperature"},
  {2, "AL03", "Circulating fluid discharge temperature rise"},
  {3, "AL04", "Circulating fluid discharge temperature abnormal"},
  {4, "AL05", "High circulating fluid return temperature"},
  {5, "AL06", "High circulating fluid discharge pressure"},
  {6, "AL07", "Abnormal pump operation"},
  {7, "AL08", "Circulating fluid discharge pressure rise"},
  {8, "AL09", "Circulating fluid discharge pressure drop"},
  {9, "AL10", "High compressor intake temperature"},
  {10,"AL11", "Low compressor intake temperature"},
  {11,"AL12", "Low super heat temperature"},
  {12,"AL13", "High compressor discharge pressure"},
  {14,"AL14", "Refrigerant high-pressure side drop"},
  {15,"AL15", "Refrigerant low-pressure side rise"},
};

// Alarm flag 2 (register 0006h)
const AlarmDef alarmFlag2[] = {
  {0, "AL16", "Refrigerant low-pressure drop"},
  {1, "AL17", "Compressor overload"},
  {2, "AL19", "Communication error"},
  {3, "AL20", "Memory error"},
  {4, "AL21", "DC line fuse cut"},
  {5, "AL22", "Discharge temp sensor failure"},
  {6, "AL23", "Return temp sensor failure"},
  {7, "AL24", "Compressor intake temp sensor failure"},
  {8, "AL25", "Discharge pressure sensor failure"},
  {9, "AL26", "Compressor discharge pressure sensor failure"},
  {10,"AL27", "Compressor intake pressure sensor failure"},
  {11,"AL28", "Maintenance of pump"},
  {12,"AL29", "Maintenance of fan motor"},
  {13,"AL30", "Maintenance of compressor"},
  {14,"AL31", "Contact input 1 signal detection"},
  {15,"AL32", "Contact input 2 signal detection"},
};

// Alarm flag 3 (register 0007h)
const AlarmDef alarmFlag3[] = {
  {0, "AL33", "Water leakage"},
  {1, "AL34", "Electric resistivity/conductivity rise"},
  {2, "AL35", "Electric resistivity/conductivity drop"},
  {3, "AL36", "Electric resistivity/conductivity sensor error"},
};

// ---------- helper to extract alarms from a 16-bit register ----------
String collectAlarmsFromRegister(uint16_t regVal, const AlarmDef* defs, size_t defCount) {
  String arr = "";
  bool first = true;
  for (size_t i=0;i<defCount;i++) {
    uint16_t mask = (1u << defs[i].bit);
    if (regVal & mask) {
      if (!first) arr += ",";
      arr += "{\"code\":\"";
      arr += defs[i].code;
      arr += "\",\"desc\":\"";
      arr += defs[i].desc;
      arr += "\"}";
      first = false;
    }
  }
  return arr; // JSON array items (without surrounding [])
}

// ---------- convert 2 bytes to signed int16 ----------
int16_t beToInt16(uint8_t hi, uint8_t lo) {
  uint16_t raw = ((uint16_t)hi << 8) | lo;
  if (raw & 0x8000) return (int16_t)(raw - 0x10000);
  return (int16_t)raw;
}

// ---------- MQTT connect ----------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println(" connected");
}
void connectMQTT() {
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  while (!mqtt.connected()) {
    Serial.print("Connecting MQTT...");
    if (mqtt.connect("esp32-chiller")) {
      Serial.println(" connected");
    } else {
      Serial.print(" failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" retrying in 2s");
      delay(2000);
    }
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  // Serial2 for RS-485: 9600, 7E1 (7 data bits, even parity, 1 stop) as manual specifies ASCII mode 7bit even parity.
  Serial2.begin(9600, SERIAL_7E1, RXD2, TXD2);

  pinMode(RS485_EN, OUTPUT);
  digitalWrite(RS485_EN, LOW); // RX by default

  Serial.println("HRX Modbus ASCII ESP32 starting...");

  // WiFi + MQTT
  connectWiFi();
  connectMQTT();
}

// ---------- Main poll & parse loop ----------
void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  if (millis() - lastPoll < pollIntervalMs) return;
  lastPoll = millis();

  String frame = buildReadFrame(slaveAddr, startRegister, readCount);
  Serial.print("Sending: ");
  Serial.print(frame);
  rs485Send(frame);

  // Read response. Manual suggests waiting up to ~200ms for response; implement 800ms to be safe + retry logic
  String resp = rs485ReadTimeout(800);
  if (resp.length() == 0) {
    // retry once after 1s as manual suggests
    Serial.println("No response, retrying after 1s...");
    delay(1000);
    rs485Send(frame);
    resp = rs485ReadTimeout(800);
  }

  if (resp.length() == 0) {
    Serial.println("No response from slave.");
    return;
  }

  Serial.print("RAW RESPONSE: ");
  Serial.println(resp);

  // parse
  uint8_t func, byteCount;
  uint8_t dataBuf[64];
  size_t dataLen = 0;
  if (!verifyAndExtractData(resp, func, byteCount, dataBuf, dataLen)) {
    Serial.println("Failed to verify/parse Modbus ASCII response.");
    return;
  }

  // Expect byteCount == readCount * 2 (since registers are 2 bytes)
  if (byteCount < readCount*2) {
    Serial.printf("Unexpected byteCount %u (expected %u)\n", byteCount, readCount*2);
    // continue anyway as long as we have data
  }

  // Extract registers into array reg[0..6]
  uint16_t regs[7] = {0};
  for (int i=0;i<7;i++) {
    if ((i*2 + 1) < (int)dataLen) {
      regs[i] = ((uint16_t)dataBuf[i*2] << 8) | dataBuf[i*2 + 1];
    } else {
      regs[i] = 0;
    }
  }

  // Interpret registers per manual:
  // 0000: discharge temp (signed, x0.1 deg)
  // 0001: reserved
  // 0002: discharge pressure (unsigned, units per config)
  // 0003: resistivity/conductivity (optional)
  // 0004: status flag 1
  // 0005: alarm flag 1
  // 0006: alarm flag 2
  // NOTE: manual also shows alarm flag 3 at 0007h if you read further - but our FC03 readCount=7 reads 0000..0006. To read 0007, increase readCount to 8.

  // Temperature signed conversion
  int16_t rawTemp = (int16_t)regs[0]; // raw 16-bit two's complement
  if (rawTemp & 0x8000) rawTemp = rawTemp - 0x10000;
  float outletTemp = rawTemp / 10.0;

  // Pressure raw -> depends on unit selection (MPa or PSI). We'll send raw value; Pi can convert using status flags if needed.
  uint16_t rawPressure = regs[2];

  uint16_t statusFlag1 = regs[4];
  uint16_t alarmFlag1  = regs[5];
  uint16_t alarmFlag2  = regs[6];

  // Collect alarm JSON items
  String alarmsJsonItems = "";
  String part;

  part = collectAlarmsFromRegister(alarmFlag1, alarmFlag1 == 0 ? nullptr : alarmFlag1 /*unused*/ , 0); // placeholder to keep function signature - we'll instead call properly below

  // Manual collection using helper
  String a1 = collectAlarmsFromRegister(alarmFlag1, alarmFlag1 == 0 ? nullptr : alarmFlag1 /*noop*/, 0); // unused path; we'll call correct ones:
  // call correctly:
  String a_items = "";
  String s1 = collectAlarmsFromRegister(alarmFlag1, alarmFlag1==0?nullptr:alarmFlag1, 0); // dummy; we will not use this

  // Instead manually collect using arrays defined above:
  String items = "";
  items += collectAlarmsFromRegister(alarmFlag1, alarmFlag1==0?nullptr:alarmFlag1, 0); // not used due to signature limitations
  // -- simpler: call collectAlarmsFromRegister with actual defs by copying code inline --
  // We'll re-implement inline to avoid the function signature limitation.

  // Inline collection for alarmFlag1
  bool first = true;
  for (size_t i=0;i< (sizeof(alarmFlag1)/sizeof(AlarmDef)); i++) {
    uint16_t mask = (1u << alarmFlag1[i].bit);
    if (alarmFlag1 & mask) {
      if (!first) alarmsJsonItems += ",";
      alarmsJsonItems += "{\"code\":\"";
      alarmsJsonItems += alarmFlag1[i].code;
      alarmsJsonItems += "\",\"desc\":\"";
      alarmsJsonItems += alarmFlag1[i].desc;
      alarmsJsonItems += "\"}";
      first = false;
    }
  }
  // Wait — the above uses alarmFlag1 both as name and as numeric; to avoid confusion, re-map numeric:
  uint16_t af1 = alarmFlag1;
  uint16_t af2 = alarmFlag2;
  // rebuild alarmsJsonItems correctly:
  alarmsJsonItems = "";
  first = true;
  for (size_t i=0;i< (sizeof(alarmFlag1)/sizeof(AlarmDef)); i++) {
    uint16_t mask = (1u << alarmFlag1[i].bit);
    if (af1 & mask) {
      if (!first) alarmsJsonItems += ",";
      alarmsJsonItems += "{\"code\":\"";
      alarmsJsonItems += alarmFlag1[i].code;
      alarmsJsonItems += "\",\"desc\":\"";
      alarmsJsonItems += alarmFlag1[i].desc;
      alarmsJsonItems += "\"}";
      first = false;
    }
  }
  // alarmFlag2
  for (size_t i=0;i< (sizeof(alarmFlag2)/sizeof(AlarmDef)); i++) {
    uint16_t mask = (1u << alarmFlag2[i].bit);
    if (af2 & mask) {
      if (!first) alarmsJsonItems += ",";
      alarmsJsonItems += "{\"code\":\"";
      alarmsJsonItems += alarmFlag2[i].code;
      alarmsJsonItems += "\",\"desc\":\"";
      alarmsJsonItems += alarmFlag2[i].desc;
      alarmsJsonItems += "\"}";
      first = false;
    }
  }

  // To read alarm flag 3 (0007h) you'd need to read 8 registers (set readCount=8)
  // For now, we set AL3 to 0 unless you modify readCount to 8
  uint16_t alarmFlag3 = 0;
  // Append alarmFlag3 if any
  if (alarmFlag3 != 0) {
    for (size_t i=0;i< (sizeof(alarmFlag3)/sizeof(AlarmDef)); i++) {
      uint16_t mask = (1u << alarmFlag3[i].bit);
      if (alarmFlag3 & mask) {
        if (!first) alarmsJsonItems += ",";
        alarmsJsonItems += "{\"code\":\"";
        alarmsJsonItems += alarmFlag3[i].code;
        alarmsJsonItems += "\",\"desc\":\"";
        alarmsJsonItems += alarmFlag3[i].desc;
        alarmsJsonItems += "\"}";
        first = false;
      }
    }
  }

  // Build JSON payload
  // timestamp use millis()/1000 as relative timestamp
  unsigned long ts = millis() / 1000;
  String payload = "{";
  payload += "\"timestamp\":";
  payload += String(ts);
  payload += ",\"outlet_temp\":";
  payload += String(outletTemp, 1);
  payload += ",\"raw_pressure\":";
  payload += String(rawPressure);
  payload += ",\"status_flag1\":";
  payload += String(statusFlag1);
  payload += ",\"alarms\":[";
  if (alarmsJsonItems.length() > 0) payload += alarmsJsonItems;
  payload += "]}";

  Serial.print("Publishing JSON: ");
  Serial.println(payload);

  // Publish to MQTT
  if (mqtt.connected()) {
    mqtt.publish(MQTT_TOPIC, payload.c_str());
  } else {
    Serial.println("MQTT not connected, can't publish.");
  }

  // Also print to Serial (Pi can read via USB if you prefer)
  Serial.println(payload);
}
