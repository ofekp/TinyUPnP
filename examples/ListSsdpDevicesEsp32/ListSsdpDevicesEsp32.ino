/*  
  Note: This example will list all the SSDP devices in the local network.
*/

#include <WiFi.h>
#include <TinyUPnP.h>
#include "esp_wifi.h"

const char* ssid = "<FILL THIS!>";
const char* password = "<FILL THIS!>";

TinyUPnP tinyUPnP(12000);  // when using the library for listing SSDP devices, a timeout must be set

void connectWiFi() {
  WiFi.disconnect();
  delay(1200);
  WiFi.mode(WIFI_STA);
  //WiFi.setAutoConnect(true);
  Serial.println(F("connectWiFi"));
  WiFi.begin(ssid, password);

  // wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }

  Serial.println(F(""));
  Serial.print(F("Connected to "));
  Serial.println(ssid);
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
}

void setup(void) {
  Serial.begin(115200);
  Serial.println(F("Starting..."));

  connectWiFi();

  Serial.print(F("Gateway Address: "));
  Serial.println(WiFi.gatewayIP().toString());
  Serial.print(F("Network Mask: "));
  Serial.println(WiFi.subnetMask().toString());

  ssdpDeviceNode* ssdpDeviceNodeList = tinyUPnP.listSsdpDevices();
  tinyUPnP.printSsdpDevices(ssdpDeviceNodeList);
}

void loop(void) {
  delay(100000);
}