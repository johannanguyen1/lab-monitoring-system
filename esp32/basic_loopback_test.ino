// --- basic hardware loopback test ---
// uses no modbus library. tests serial2 send/receive with de/re control.

#define HW_SERIAL Serial2
#define TX_PIN 17
#define RX_PIN 16
const int DE_RE_PIN = 5;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\n--- basic serial2 loopback test ---");
  Serial.println("adapter a and b should be jumpered.");
  Serial.println("using pins: tx=17, rx=16, de/re=5");

  // configure de/re pin
  pinMode(DE_RE_PIN, OUTPUT);
  digitalWrite(DE_RE_PIN, LOW); // start in receive mode

  // start serial2 port (19200 8e1)
  HW_SERIAL.begin(19200, SERIAL_8E1, RX_PIN, TX_PIN);
  Serial.println("serial2 started.");
}

void loop() {
  String testMessage = "EchoTest";
  Serial.print("sending: '" + testMessage + "'...");

  // enable transmitter
  digitalWrite(DE_RE_PIN, HIGH);
  delay(5); // allow adapter to switch

  // send data
  HW_SERIAL.print(testMessage);
  HW_SERIAL.flush(); // wait for buffer to empty
  delay(5); // allow bits to transmit

  // disable transmitter (enable receiver)
  digitalWrite(DE_RE_PIN, LOW);
  Serial.print(" listening...");

  // listen for echo
  String receivedEcho = "";
  unsigned long startTime = millis();
  bool receivedSomething = false;

  // listen for up to 500ms
  while (millis() - startTime < 500) {
    if (HW_SERIAL.available() > 0) {
       receivedSomething = true;
       receivedEcho += (char)HW_SERIAL.read();
    }
  }

  // report result
  if (receivedEcho == testMessage) {
    Serial.println("\n>>> success! received correct echo: '" + receivedEcho + "'");
    Serial.println(">>> esp32 pins (5, 16, 17) and adapter seem ok!");
    Serial.println(">>> problem likely modbus settings or chiller wiring (a/b swap?).");
  } else if (receivedSomething) {
    Serial.println("\n>>> failed! received garbled echo: '" + receivedEcho + "'");
    Serial.println(">>> check serial format (8e1?) or wiring.");
  } else {
    Serial.println("\n>>> failed! received nothing.");
    Serial.println(">>> esp32 pins (5, 16, 17) or the adapter/wiring is faulty.");
  }

  Serial.println("------------------------------------");
  delay(3000); // wait 3 seconds
}