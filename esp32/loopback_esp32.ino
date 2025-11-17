#include <ModbusMaster.h>

// --- our esp32 pin settings ---
#define RS485_HW_SERIAL Serial2
#define RS485_TXD_PIN 17
#define RS485_RXD_PIN 16
const int DE_RE_PIN = 4; // using your pin 4

// --- our chiller settings ---
#define CHILLER_SLAVE_ID 1
#define ALARM_REGISTER_ADDR 0x0005
#define NUM_ALARM_REGISTERS 4

// instantiate the modbusmaster object
ModbusMaster node;

// called just before a modbus message is sent
void preTransmission() {
  Serial.print(" [TX] "); // <-- print statement added
  digitalWrite(DE_RE_PIN, HIGH);
}

// called just after a modbus message is sent
void postTransmission() {
  digitalWrite(DE_RE_PIN, LOW);
  Serial.print(" [RX] "); // <-- print statement added
}

void setup() {
  // set de/re pin as output and initialize to low (receive mode)
  pinMode(DE_RE_PIN, OUTPUT);
  digitalWrite(DE_RE_PIN, LOW);

  // start serial for laptop communication (serial monitor)
  Serial.begin(115200);
  while (!Serial);
  Serial.println("--- modbusmaster diagnostic test (pin 4) ---");

  // --- starting with 8e1 (even parity) ---
  Serial.println("testing 19200 baud, 8e1 (even parity)...");
  RS485_HW_SERIAL.begin(19200, SERIAL_8E1, RS485_RXD_PIN, RS485_TXD_PIN);

  // initialize modbusmaster node
  node.begin(CHILLER_SLAVE_ID, RS485_HW_SERIAL);

  // set the transmission callbacks
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
}

void loop() {
  Serial.print("polling chiller (id 1)..."); // <-- 1. print before request

  // send a request to read holding registers (function code 03)
  uint8_t result = node.readHoldingRegisters(ALARM_REGISTER_ADDR, NUM_ALARM_REGISTERS);

  // check if the poll was successful
  if (result == node.ku8MBSuccess) {
    Serial.println("...success!");

    uint16_t alarmFlag1 = node.getResponseBuffer(0);
    uint16_t alarmFlag2 = node.getResponseBuffer(1);
    uint16_t alarmFlag3 = node.getResponseBuffer(2);
    uint16_t alarmFlag4 = node.getResponseBuffer(3);

    Serial.print("  alarm flag 1 (0005h): ");
    Serial.println(alarmFlag1, BIN);
    Serial.print("  alarm flag 2 (0006h): ");
    Serial.println(alarmFlag2, BIN);
    Serial.print("  alarm flag 3 (0007h): ");
    Serial.println(alarmFlag3, BIN);
    Serial.print("  alarm flag 4 (0008h): ");
    Serial.println(alarmFlag4, BIN);

  } else {
    // if the poll fails, print the error
    Serial.print("...failed! modbusmaster error code: 0x");
    Serial.println(result, HEX);
  }

  // wait 2 seconds before polling again
  delay(2000);
}