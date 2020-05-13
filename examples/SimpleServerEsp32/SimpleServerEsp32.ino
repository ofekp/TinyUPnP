/*  
  Note: This example includes the library EasyDDNS. You'll have to add this package using your Arduino Library Manager.
        The purpose of this package is to publish your dynamic IP to a DDNS service that will allocate a human readable
        address to your current IP. If you do not need that, you can remove this dependency.
*/

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <TinyUPnP.h>
#include <EasyDDNS.h>  // see note above

const char* ssid = "<FILL THIS!>";
const char* password = "<FILL THIS!>";
#define LISTEN_PORT <FILL THIS!>  // http://<IP>:<LISTEN_PORT>/?name=<your string>
#define LEASE_DURATION 36000  // seconds
#define FRIENDLY_NAME "<FILL THIS!>"  // this name will appear in your router port forwarding section
#define DDNS_USERNAME "<FILL THIS!>"
#define DDNS_PASSWORD "<FILL THIS!>"
#define DDNS_DOMAIN "<FILL THIS!>"

TinyUPnP tinyUPnP(20000);  // -1 means blocking, preferably, use a timeout value (ms)
WebServer server(LISTEN_PORT);

int ledChannel = 0;
const int pin = 2;  // not all ESP32 versions have an on board LED
const int delayval = 5;
int freq = 5000;
int resolution = 8;  // in bits

// 0 <= percentage <= 100
void setPower(int percentage) {
  long pwm_val = map(percentage, 0, 100, 0, 255);
  if (pwm_val > 255) {
    pwm_val = 255;
  }
  ledcWrite(ledChannel, (int) pwm_val);
}

void handleRoot() {
  String message = "Number of args received: ";
  message += server.args();  // get number of parameters
  message += "\n";
  int percentage = 0;
  for (int i = 0; i < server.args(); i++) {
    message += "Arg #" + (String)i + " => ";
    message += server.argName(i) + ": ";  // get the name of the parameter
    message += server.arg(i) + "\n";  // get the value of the parameter

    if (server.argName(i).equals("percentage")) {
      percentage = server.arg(i).toInt();
    }
  }

  server.send(200, "text/plain", message);       //Response to the HTTP request

  setPower(percentage);
}

void connectWiFi() {
  WiFi.disconnect();
  delay(1200);
  WiFi.mode(WIFI_STA);
  //WiFi.setAutoConnect(true);
  Serial.println(F("connectWiFi"));
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
  Serial.println(F("Starting..."));

  pinMode(pin, OUTPUT);
  ledcSetup(ledChannel, freq, resolution);
  ledcAttachPin(pin, ledChannel);

  connectWiFi();

  portMappingResult portMappingAdded;
  tinyUPnP.addPortMappingConfig(WiFi.localIP(), LISTEN_PORT, RULE_PROTOCOL_TCP, LEASE_DURATION, FRIENDLY_NAME);
  while (portMappingAdded != SUCCESS && portMappingAdded != ALREADY_MAPPED) {
    portMappingAdded = tinyUPnP.commitPortMappings();
    Serial.println("");
  
    if (portMappingAdded != SUCCESS && portMappingAdded != ALREADY_MAPPED) {
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
  if (!MDNS.begin("esp32")) {
    Serial.println("Error while setting up DDNS service!");
    while(1) {
      delay(1000);
    }
  }

  Serial.println("DDNS service started");

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
  Serial.println("HTTP server started");

  delay(10);

  Serial.print(F("Gateway Address: "));
  Serial.println(WiFi.gatewayIP().toString());
  Serial.print(F("Network Mask: "));
  Serial.println(WiFi.subnetMask().toString());
}

void loop(void) {
  delay(5);
	
  EasyDDNS.update(300000);  // check for New IP
    
  tinyUPnP.updatePortMappings(600000, &connectWiFi);  // 10 minutes
  
  server.handleClient();
}
