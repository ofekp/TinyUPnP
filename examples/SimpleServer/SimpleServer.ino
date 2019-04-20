/*
  Created by Ofek Pearl, September 2017.

  Note: This is a simple server that does not contain the DDNS update, to have the server available using a simple
  address such as "<username>.dynu.net" instead of an IP please refer to the PWM_LEDServer example of this package.
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include "TinyUPnP.h"

// server config
const char* ssid = "<FILL THIS!>";
const char* password = "<FILL THIS!>";
#define LISTEN_PORT <FILL THIS!>  // http://<IP>:<LISTEN_PORT>/?name=<your string>
#define LEASE_DURATION 36000  // seconds
#define FRIENDLY_NAME "<FILL THIS!>"  // this name will appear in your router port forwarding section

TinyUPnP tinyUPnP(20000);  // -1 means blocking, preferably, use a timeout value (ms)
ESP8266WebServer server(LISTEN_PORT);

void handleRoot() {
  String message = "Number of args received: ";
  message += server.args();  // get number of parameters
  message += "\n";
  String userName = "";
  for (int i = 0; i < server.args(); i++) {
    message += "Arg #" + (String)i + " => ";
    message += server.argName(i) + ": ";  // get the name of the parameter
    message += server.arg(i) + "\n";  // get the value of the parameter
    
    if (server.argName(i).equals("name")) {
      userName = server.arg(i);
    }
  }

  if (userName.length() > 0) {
    message += "\n\nHello " + userName + "!";
  }

  server.send(200, "text/plain", message);  // response to the HTTP request
}

void connectWiFi() {
  WiFi.disconnect();
  delay(1200);
  WiFi.mode(WIFI_STA);
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

void handleNotFound() {
  String message = "Page Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup(void) {
  Serial.begin(115200);
  Serial.println(F("Starting..."));

  connectWiFi();

  boolean portMappingAdded = false;
  tinyUPnP.addPortMappingConfig(WiFi.localIP(), LISTEN_PORT, RULE_PROTOCOL_TCP, LEASE_DURATION, FRIENDLY_NAME);
  while (!portMappingAdded) {
    portMappingAdded = tinyUPnP.commitPortMappings();
    Serial.println("");
  
    if (!portMappingAdded) {
      // for debugging, you can see this in your router too under forwarding or UPnP
      tinyUPnP.printAllPortMappings();
      Serial.println(F("This was printed because adding the required port mapping failed"));
      delay(30000);  // 30 seconds before trying again
    }
  }
  
  Serial.println("UPnP done");
  
  // server
  if (MDNS.begin("esp8266")) {
    Serial.println(F("MDNS responder started"));
  }

  server.on("/", handleRoot);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println(F("HTTP server started"));

  delay(10);

  Serial.print(F("Gateway Address: "));
  Serial.println(WiFi.gatewayIP().toString());
  Serial.print(F("Network Mask: "));
  Serial.println(WiFi.subnetMask().toString());
}

void loop(void) {
  delay(5);

  tinyUPnP.updatePortMappings(600000, &connectWiFi);  // 10 minutes

  server.handleClient();
}