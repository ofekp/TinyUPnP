/*
  Created by Ofek Pearl, September 2017.
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ESP8266SSDP.h>
#include "TinyUPnP.h"

// server config
const char* ssid = "<FILL THIS!>";
const char* password = "<FILL THIS!>";
#define LISTEN_PORT <FILL THIS!>
#define LEASE_DURATION 36000  // seconds
#define FRIENDLY_NAME "<FILL THIS!>"
#define DDNS_USERNAME "<FILL THIS!>"
#define DDNS_PASSWORD "<FILL THIS!>"
#define DDNS_DOMAIN "<FILL THIS!>"
unsigned long lastUpdateTime = 0;

TinyUPnP tinyUPnP(20000);
ESP8266WebServer server(LISTEN_PORT);

const int led = 13;
const int pin = 4;

const int delayval = 5;
int consequtiveFails = 0;

// 0 <= percentage <= 100
void setPower(uint32 percentage) {
  long pwm_val = map(percentage, 0, 100, 0, 1023);
  if (pwm_val > 1023) {
    pwm_val = 1023;
  }
  analogWrite(pin, pwm_val);
}

void handleRoot() {
  String message = "Number of args received: ";
  message += server.args();            //Get number of parameters
  message += "\n";                            //Add a new line
  int percentage = 0;
  for (int i = 0; i < server.args(); i++) {
    message += "Arg n" + (String)i + " => "; //Include the current iteration value
    message += server.argName(i) + ": ";     //Get the name of the parameter
    message += server.arg(i) + "\n";         //Get the value of the parameter
    
    if (server.argName(i).equals("percentage")) {
      percentage = server.arg(i).toInt();
    }
  }

  server.send(200, "text/plain", message);       //Response to the HTTP request

  setPower(percentage);
}

void connectWiFi() {
  Serial.println(F("connectWiFi"));
  //WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // flash twice to know that we are trying to connect to the WiFi
  setPower(50);
  delay(200);
  setPower(0);
  delay(200);
  setPower(50);
  delay(200);
  setPower(0);

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
  String message = "File Not Found\n\n";
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
  pinMode (led, OUTPUT);
  digitalWrite (led, 0);
  Serial.println(F("Starting..."));
  
  WiFi.setAutoConnect(true);
  connectWiFi();

  boolean portMappingAdded = false;
  tinyUPnP.setMappingConfig(WiFi.localIP(), LISTEN_PORT, RULE_PROTOCOL_TCP, LEASE_DURATION, FRIENDLY_NAME);
  while (!portMappingAdded) {
    portMappingAdded = tinyUPnP.addPortMapping();
    Serial.println("");
  
    if (!portMappingAdded) {
      // for debugging, you can see this in your router too under forwarding or UPnP
      tinyUPnP.printAllPortMappings();
      Serial.println(F("This was printed because adding the required port mapping failed"));
      delay(30000);  // 30 seconds before trying again
    }
  }
  
  Serial.println("UPnP done");
  
  
  // DDNS
  EasyDDNS.service("dynu");
  EasyDDNS.client(DDNS_DOMAIN, DDNS_USERNAME, DDNS_PASSWORD);
  
  // server
  if (MDNS.begin("esp8266")) {
    Serial.println(F("MDNS responder started"));
  }

  // fade on and then off to know the device is ready
  for (int i = 0; i < 100; i++) {
    setPower(i);
    delay(delayval);
  }
  for (int i = 100; i >= 0; i--) {
    setPower(i);
    delay(delayval);
  }
  setPower(0);
  for (int i = 0; i < 100; i++) {
    setPower(i);
    delay(delayval);
  }
  for (int i = 100; i >= 0; i--) {
    setPower(i);
    delay(delayval);
  }
  setPower(0);

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
  delay(1);

  EasyDDNS.update(300000);  // check for New IP Every 100 Seconds.

  boolean updateSuccess = tinyUPnP.updatePortMapping(600000);  // 10 minutes
  if (updateSuccess) {
    consequtiveFails = 0;
  } else if (!updateSuccess && consequtiveFails > 0) {
    consequtiveFails++;
    Serial.print(F("Increasing consequtiveFails to ["));
    Serial.print(String(consequtiveFails));
    Serial.println(F("]"));
    if (consequtiveFails % 20 == 0) {
      Serial.print(F("ERROR: Too many times with no effect on updatePortMapping. Current number of fallbacks times ["));
			Serial.print(String(consequtiveFails));
			Serial.println(F("]"));
      tinyUPnP.testConnectivity();
      connectWiFi();
      tinyUPnP.testConnectivity();
    } else if (consequtiveFails == INT_MAX) {
      consequtiveFails = 0;
    }
  }

  server.handleClient();
}