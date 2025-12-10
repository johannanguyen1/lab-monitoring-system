#include <HardwareSerial.h>


// --- PIN DEFINITIONS ---
const int RE_DE_PIN = 4;  // Connect to RE and DE pins on RS485 module
const int RX_PIN = 16;    // Connect to RO (Receiver Output)
const int TX_PIN = 17;    // Connect to DI (Driver Input)

// --- CHILLER SETTINGS ---
// Ensure these match your Chiller's settings (Menu Co01 - Co06)
// Default for HRS series is often 19200 bps, but check your specific unit.
const long BAUD_RATE = 19200; 
const uint8_t SLAVE_ID = 1;   // Default Chiller Address

HardwareSerial ChillerSerial(2); // Use UART2

void setup() {
  Serial.begin(115200); // USB Serial for debugging
  
  // Initialize RS485 Control Pin
  pinMode(RE_DE_PIN, OUTPUT);
  digitalWrite(RE_DE_PIN, LOW); // Start in Receive mode

  // Initialize RS485 Serial Port
  // Note: Modbus ASCII is technically 7 data bits, Even parity (7E1), 
  // but 8N1 often works if the chiller is set to No Parity. 
  // If you get garbage, try: ChillerSerial.begin(BAUD_RATE, SERIAL_8E1, RX_PIN, TX_PIN);
  ChillerSerial.begin(BAUD_RATE, SERIAL_7E1, RX_PIN, TX_PIN);
  
  delay(1000);
  Serial.println("ESP32 Thermo Chiller Monitor Started...");
}

void loop() {
  readAlarms();
  delay(5000); // Check every 5 seconds
}

// Function to calculate Longitudinal Redundancy Check (LRC) for Modbus ASCII
uint8_t calculateLRC(uint8_t *data, uint8_t len) {
  uint8_t sum = 0;
  for (int i = 0; i < len; i++) {
    sum += data[i];
  }
  return (uint8_t)(-(int8_t)sum); // Two's complement
}

void readAlarms() {
  // --- BUILD COMMAND ---
  // We want to Read Holding Registers (Function 03)
  // Register 0005h is usually Alarm Flag 1, 0006h is Alarm Flag 2.
  // Let's read 3 registers starting at 0005h to capture all alarms.
  
  uint8_t targetRegister = 0x05; 
  uint8_t numRegisters = 0x03;
  
  // Raw bytes before ASCII encoding
  uint8_t rawCmd[] = {SLAVE_ID, 0x03, 0x00, targetRegister, 0x00, numRegisters};
  uint8_t lrc = calculateLRC(rawCmd, 6);

  // Buffer for the final ASCII string
  // Format: : AA FF RRRR NNNN CC \r\n
  char asciiCmd[20];
  sprintf(asciiCmd, ":%02X%02X%04X%04X%02X\r\n", 
          SLAVE_ID, 0x03, targetRegister, numRegisters, lrc);

  // --- SEND COMMAND ---
  Serial.print("Sending: ");
  Serial.print(asciiCmd);
  
  digitalWrite(RE_DE_PIN, HIGH); // Enable TX
  ChillerSerial.print(asciiCmd);
  ChillerSerial.flush();         // Wait for transmission to finish
  digitalWrite(RE_DE_PIN, LOW);  // Disable TX (Switch to RX)

  // --- RECEIVE RESPONSE ---
  // Response format: : AA 03 BB DDDD... CC \r\n
  // Wait for data (timeout 1000ms)
  unsigned long startTime = millis();
  String response = "";
  
  while (millis() - startTime < 1000) {
    if (ChillerSerial.available()) {
      char c = ChillerSerial.read();
      response += c;
      if (c == '\n') break; // End of message
    }
  }

  if (response.length() > 0) {
    Serial.print("Received: ");
    Serial.print(response); // Debug raw response
    parseResponse(response);
  } else {
    Serial.println("Error: Timeout - No response from chiller.");
  }
}
void parseResponse(String msg) {
  // Basic validation (Must start with :)
  if (msg.charAt(0) != ':') return;

  // Response example: :010306 0000 0000 0000 F6 \r\n
  // Indices:
  // : (0)
  // 01 (1,2) -> Slave ID
  // 03 (3,4) -> Function
  // 06 (5,6) -> Byte Count (3 registers * 2 bytes = 6 bytes)
  // Alarm Flag 1: chars 7,8,9,10
  // Alarm Flag 2: chars 11,12,13,14
  // Alarm Flag 3: chars 15,16,17,18
  // LRC: chars 19,20

  // --- EXTRACT DATA ---
  String val1Hex = msg.substring(7, 11);
  String val2Hex = msg.substring(11, 15);
  String val3Hex = msg.substring(15, 19);

  long alarm1 = strtol(val1Hex.c_str(), NULL, 16);
  long alarm2 = strtol(val2Hex.c_str(), NULL, 16);
  long alarm3 = strtol(val3Hex.c_str(), NULL, 16);

  // --- DISPLAY STATUS ---
  if (alarm1 == 0 && alarm2 == 0 && alarm3 == 0) {
    Serial.println(" -> Status: System Normal (No Alarms Active)");
  } else {
    Serial.println(" -> [!] WARNING: ALARMS DETECTED");

    // --- ALARM FLAG 1 (Register 0005h) ---
    // Ref: Manual HRX-OM-M091 Section 4.10.5
    if (bitRead(alarm1, 0)) Serial.println("    [AL01] Low Level in Tank");
    if (bitRead(alarm1, 1)) Serial.println("    [AL02] High Circulating Fluid Discharge Temp");
    if (bitRead(alarm1, 2)) Serial.println("    [AL03] Circulating Fluid Discharge Temp Rise");
    if (bitRead(alarm1, 3)) Serial.println("    [AL04] Circulating Fluid Discharge Temp Drop");
    if (bitRead(alarm1, 4)) Serial.println("    [AL06] Fan Failure"); 
    if (bitRead(alarm1, 6)) Serial.println("    [AL09] High Circulating Fluid Discharge Pressure");
    if (bitRead(alarm1, 9)) Serial.println("    [AL12] Pressure Sensor Error (Lower)");
    if (bitRead(alarm1, 10)) Serial.println("   [AL13] Temperature Sensor Error (Return)");
    if (bitRead(alarm1, 11)) Serial.println("   [AL14] Temperature Sensor Error (Discharge)");

    // --- ALARM FLAG 2 (Register 0006h) ---
    if (bitRead(alarm2, 0)) Serial.println("    [AL17] Internal Unit Fan Failure");
    if (bitRead(alarm2, 1)) Serial.println("    [AL18] Compressor Overcurrent");
    if (bitRead(alarm2, 2)) Serial.println("    [AL19] Pump Failure");
    if (bitRead(alarm2, 4)) Serial.println("    [AL21] Pump Overcurrent");
    if (bitRead(alarm2, 9)) Serial.println("    [AL26] DC Line Fuse Cut");
    if (bitRead(alarm2, 11)) Serial.println("   [AL28] Pump Maintenance");

    // --- ALARM FLAG 3 (Register 0007h) ---
    // Critical safety alarms often live here
    if (bitRead(alarm3, 0)) Serial.println("    [AL30] Water Leakage Detected!");
    if (bitRead(alarm3, 1)) Serial.println("    [AL31] Contact Input 1 Error");
    if (bitRead(alarm3, 2)) Serial.println("    [AL32] Contact Input 2 Error");
    if (bitRead(alarm3, 3)) Serial.println("    [AL34] Electric Resistivity/Conductivity Sensor Error");
    
    // Debug raw values if needed to find missing bits
    // Serial.print("Raw Hex: "); Serial.print(val1Hex); Serial.print(" "); Serial.print(val2Hex); Serial.print(" "); Serial.println(val3Hex);
  }
}