#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiUdp.h>

// ================= CONFIGURATION =================
// --- WIFI SETTINGS ---

// currently connected to geo's hotspot
const char* ssid = "Pixel_8116";          // <--- ENTER WIFI NAME
const char* password = "4naj7tj6u8rqksc";  // <--- ENTER WIFI PASSWORD

// --- UDP SETTINGS ---
// The IP address of your Raspberry Pi

// currently my mac ip
const char* udpAddress = "172.16.115.113"; 
const int udpPort = 4210;

// --- CHILLER SETTINGS ---
const int RE_DE_PIN = 4;   // RE/DE on RS485 module
const int RX_PIN = 16;     // RO
const int TX_PIN = 17;     // DI
const long BAUD_RATE = 19200;
const uint8_t SLAVE_ID = 1;

// ================= GLOBALS =================
WiFiUDP udp;
HardwareSerial ChillerSerial(2); // UART2

unsigned long lastChillerCheck = 0;
const long CHECK_INTERVAL = 5000; // Check every 5 seconds

// ================= WIFI & UDP FUNCTIONS =================

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int c = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    c++;
    if (c > 20) ESP.restart();
  }

  Serial.println("\nWiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// Function to send a UDP packet
void sendUDP(String message) {
  udp.beginPacket(udpAddress, udpPort);
  udp.print(message);
  udp.endPacket();
  // Also print to USB for debugging
  Serial.print("UDP Sent: ");
  Serial.println(message);
}

// ================= CHILLER LOGIC =================

uint8_t calculateLRC(uint8_t *data, uint8_t len) {
  uint8_t sum = 0;
  for (int i = 0; i < len; i++) {
    sum += data[i];
  }
  return (uint8_t)(-(int8_t)sum);
}

void parseResponse(String msg) {
  // Basic validation
  if (msg.charAt(0) != ':') return;

  // Extract Hex Strings
  String val1Hex = msg.substring(7, 11);
  String val2Hex = msg.substring(11, 15);
  String val3Hex = msg.substring(15, 19);

  long alarm1 = strtol(val1Hex.c_str(), NULL, 16);
  long alarm2 = strtol(val2Hex.c_str(), NULL, 16);
  long alarm3 = strtol(val3Hex.c_str(), NULL, 16);

  if (alarm1 == 0 && alarm2 == 0 && alarm3 == 0) {
    Serial.println(" -> System Normal");
    // Optional: Send heartbeat via UDP so Pi knows we are alive
    sendUDP("STATUS:NORMAL"); 
  } else {
    Serial.println(" -> [!] WARNING: ALARMS DETECTED");
    
    // Construct a composite message or send individual packets
    // Here we send individual packets for each active alarm
    
    // --- ALARM FLAG 1 ---
    if (bitRead(alarm1, 0)) sendUDP("ALARM:[AL01] Low Level in Tank");
    if (bitRead(alarm1, 1)) sendUDP("ALARM:[AL02] High Circ Fluid Discharge Temp");
    if (bitRead(alarm1, 2)) sendUDP("ALARM:[AL03] Circ Fluid Discharge Temp Rise");
    if (bitRead(alarm1, 3)) sendUDP("ALARM:[AL04] Circ Fluid Discharge Temp Drop");
    if (bitRead(alarm1, 4)) sendUDP("ALARM:[AL06] Fan Failure"); 
    if (bitRead(alarm1, 6)) sendUDP("ALARM:[AL09] High Circ Fluid Discharge Pressure");
    if (bitRead(alarm1, 9)) sendUDP("ALARM:[AL12] Pressure Sensor Error (Lower)");
    if (bitRead(alarm1, 10)) sendUDP("ALARM:[AL13] Temp Sensor Error (Return)");
    if (bitRead(alarm1, 11)) sendUDP("ALARM:[AL14] Temp Sensor Error (Discharge)");

    // --- ALARM FLAG 2 ---
    if (bitRead(alarm2, 0)) sendUDP("ALARM:[AL17] Internal Unit Fan Failure");
    if (bitRead(alarm2, 1)) sendUDP("ALARM:[AL18] Compressor Overcurrent");
    if (bitRead(alarm2, 2)) sendUDP("ALARM:[AL19] Pump Failure");
    if (bitRead(alarm2, 4)) sendUDP("ALARM:[AL21] Pump Overcurrent");
    if (bitRead(alarm2, 9)) sendUDP("ALARM:[AL26] DC Line Fuse Cut");
    if (bitRead(alarm2, 11)) sendUDP("ALARM:[AL28] Pump Maintenance");

    // --- ALARM FLAG 3 ---
    if (bitRead(alarm3, 0)) sendUDP("ALARM:[AL30] Water Leakage Detected!");
    if (bitRead(alarm3, 1)) sendUDP("ALARM:[AL31] Contact Input 1 Error");
    if (bitRead(alarm3, 2)) sendUDP("ALARM:[AL32] Contact Input 2 Error");
    if (bitRead(alarm3, 3)) sendUDP("ALARM:[AL34] Resistivity/Conductivity Error");
  }
}

void readAlarms() {
  uint8_t targetRegister = 0x05; 
  uint8_t numRegisters = 0x03;
  
  uint8_t rawCmd[] = {SLAVE_ID, 0x03, 0x00, targetRegister, 0x00, numRegisters};
  uint8_t lrc = calculateLRC(rawCmd, 6);

  char asciiCmd[20];
  sprintf(asciiCmd, ":%02X%02X%04X%04X%02X\r\n", 
          SLAVE_ID, 0x03, targetRegister, numRegisters, lrc);

  // Send Command
  digitalWrite(RE_DE_PIN, HIGH); 
  ChillerSerial.print(asciiCmd);
  ChillerSerial.flush();
  digitalWrite(RE_DE_PIN, LOW);

  // Receive Response
  unsigned long startTime = millis();
  String response = "";
  
  while (millis() - startTime < 1000) {
    if (ChillerSerial.available()) {
      char c = ChillerSerial.read();
      response += c;
      if (c == '\n') break; 
    }
  }

  if (response.length() > 0) {
    parseResponse(response);
  } else {
    Serial.println("Error: Timeout - No response from chiller.");
    // sendUDP("ERROR:Timeout"); // Optional
  }
}

// ================= MAIN SETUP & LOOP =================

void setup() {
  Serial.begin(115200); 

  // Setup RS485
  pinMode(RE_DE_PIN, OUTPUT);
  digitalWrite(RE_DE_PIN, LOW);
  ChillerSerial.begin(BAUD_RATE, SERIAL_7E1, RX_PIN, TX_PIN);

  // Setup WiFi
  setup_wifi();
  
  // Start UDP (Listening on local port, though we mainly transmit)
  udp.begin(udpPort);

  Serial.println("ESP32 Chiller UDP Monitor Started...");
}

void loop() {
  // If WiFi drops, try to reconnect
  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }

  // Check Chiller periodically
  unsigned long now = millis();
  if (now - lastChillerCheck > CHECK_INTERVAL) {
    lastChillerCheck = now;
    readAlarms();
  }
}