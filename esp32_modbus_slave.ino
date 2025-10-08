#include <ModbusRTU.h>
#include <ModbusMaster.h>

#define SLAVE_ID 1          // unique per ESP32
#define CHILLER_ID 10       // Modbus address of chiller
#define DE_PIN 21           // RS485 driver enable pin
#define RE_PIN 22           // RS485 receiver enable pin

ModbusMaster chiller;       // master to read from chiller
ModbusRTU mb;               // slave to Pi

uint16_t error_code = 0;

// RS485 transmit/receive control for chiller side
void preTransmission() {
  digitalWrite(DE_PIN, HIGH);
  digitalWrite(RE_PIN, HIGH);
}
void postTransmission() {
  digitalWrite(DE_PIN, LOW);
  digitalWrite(RE_PIN, LOW);
}

bool cbReadIreg(Modbus::FunctionCode fc, uint16_t addr, uint16_t num, void* data) {
  if (addr == 0) ((uint16_t*)data)[0] = error_code;
  return true;
}

void setup() {
  Serial.begin(9600);          // To Pi
  Serial2.begin(9600);         // To chiller

  pinMode(DE_PIN, OUTPUT);
  pinMode(RE_PIN, OUTPUT);
  postTransmission();

  // Setup Modbus Master (ESP → chiller)
  chiller.begin(CHILLER_ID, Serial2);
  chiller.preTransmission(preTransmission);
  chiller.postTransmission(postTransmission);

  // Setup Modbus Slave (Pi → ESP)
  mb.begin(&Serial);
  mb.slave(SLAVE_ID);
  mb.addIreg(0); // register 0 = chiller error code
  mb.onGetIreg(cbReadIreg);
}

void loop() {
  // Read chiller error code from register 100 (example address)
  uint8_t result = chiller.readInputRegisters(100, 1);
  if (result == chiller.ku8MBSuccess) {
    error_code = chiller.getResponseBuffer(0);
  }

  mb.task();
  delay(1000);
}
