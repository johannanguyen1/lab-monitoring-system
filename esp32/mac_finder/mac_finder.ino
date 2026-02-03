#include <WiFi.h>

void setup(){
  Serial.begin(115200);
  delay(2000); // Wait for serial monitor
  Serial.println();
  Serial.println("-----------------------------");
  Serial.print("ESP32 MAC Address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("-----------------------------");
}

void loop(){}