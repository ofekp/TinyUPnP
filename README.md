# TinyUPnP
A very small UPnP IGD implementation for ESP8266 or ESP32.

Installation
=
Just clone or download as zip, then simply copy the folder TinyUPnP to the Arduino IDE "libraries" folder e.g "D:\arduino-1.8.9\libraries".

Usage and More Information
=

**Dependecies**

Boards:
* ESP8266 - Follow https://randomnerdtutorials.com/how-to-install-esp8266-board-arduino-ide/
* ESP32 - Follow https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/

Libraries (Arduino IDE -> Sketch -> Include Library -> Manage Libraries):
* EasyDDNS - For telling your DDNS server what the IP of your Gateway Router is (optional).

**Include**
```
#include "TinyUPnP.h"
#include <EasyDDNS.h>  // optional
```

**Declare**
```
TinyUPnP *tinyUPnP = new TinyUPnP(20000);  // -1 for blocking (preferably, use a timeout value in [ms])
```
**Setup**
```
// you may repeat 'addPortMappingConfig' more than once
tinyUPnP->addPortMappingConfig(WiFi.localIP(), LISTEN_PORT, RULE_PROTOCOL_TCP, LEASE_DURATION, FRIENDLY_NAME);

// finally, commit the port mappings to the IGD
portMappingAdded = tinyUPnP->commitPortMappings();
```
**Loop**
```
// update UPnP port mapping every ms internal
// you can provide an optional method for reconnecting to the WiFi (otherwise leave as NULL)
tinyUPnP->updatePortMappings(600000, &connectWiFi);  // 10 minutes
```
**API**

This is specific for the example code, you can do what you like here
```
http://<IP or DDNS>:<LISTEN_PORT>/?percentage=<0..100>
```
**Print**
```
// print all the port mappings that were configured using 'addPortMappingConfig' in the setup step
tinyUPnP->printPortMappingConfig();

// print all the current port mappings from the IGD
tinyUPnP->printAllPortMappings();
```
**Debug**

You can turn off debug prints by setting `UPNP_DEBUG` to `false` in [TinyUPnP.h#L16](https://github.com/ofekp/TinyUPnP/blob/master/src/TinyUPnP.h#L15)

Issues
=
When reporting issues, attach full log (i.e `UPNP_DEBUG` is set to `true`) and add the serial output to the issue as a text file attachment.

Donation
=
[![Donate](https://www.paypalobjects.com/en_US/i/btn/btn_donate_LG.gif)](https://www.paypal.me/ofekpearl/1usd) If you like what I got, you can consider donating here, you can change the amount as you like.

:star: If not, starring this project will go a long way to help me too!

For anyone interested in how the library works
=
1. It sends an M_SEARCH message to UPnP UDP multicast address.
2. The gateway router will respond with a message including an HTTP header called Location.
3. `Location` is a link to an XML file containing the IGD (Internet Gateway Device) API in order to create the needed calls which will add the new port mapping to your gateway router.
4. One of the services that is depicted in the XML is `<serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>` which is what the library is looking for.
5. That service will include a `eventSubURL` tag which is a link to your router's IGD API. (The base URL is also depicted in the same file under the tag URLBase)
6. Using the base URL and the WANPPPConnection link you can issue an HTTP query to the router that will add the UPnP rule.
7. As a side note, the service depicted in the XML also includes a SCPDURL tag which is a link to another XML that depicts commands available for the service and their parameters. The package skips this stage as I assumed the query will be similar for many routers, this may very well not be the case, though, so it is up to you to check.
8. From this stage the package will issue the service command using an HTTP query to the router. The actual query can be seen in the code quite clearly but for anyone interested:
Headers:
```
"POST " + <link to service command from XML> + " HTTP/1.1"
"Content-Type: text/xml; charset=\"utf-8\""
"SOAPAction: \"urn:schemas-upnp-org:service:WANPPPConnection:1#AddPortMapping\""
"Content-Length: " + body.length()
Body:

"<?xml version=\"1.0\"?>\r\n"
"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n"
"<s:Body>\r\n"
"<u:AddPortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANPPPConnection:1\">\r\n"
"  <NewRemoteHost></NewRemoteHost>\r\n"
"  <NewExternalPort>" + String(rulePort) + "</NewExternalPort>\r\n"
"  <NewProtocol>" + ruleProtocol + "</NewProtocol>\r\n"
"  <NewInternalPort>" + String(rulePort) + "</NewInternalPort>\r\n"
"  <NewInternalClient>" + ipAddressToString(ruleIP) + "</NewInternalClient>\r\n"
"  <NewEnabled>1</NewEnabled>\r\n"
"  <NewPortMappingDescription>" + ruleFriendlyName + "</NewPortMappingDescription>\r\n"
"  <NewLeaseDuration>" + String(ruleLeaseDuration) + "</NewLeaseDuration>\r\n"
"</u:AddPortMapping>\r\n"
"</s:Body>\r\n"
"</s:Envelope>\r\n";
I hope this helps.
```
Referenced from my answer here:
https://stackoverflow.com/a/46267791/4295037

DDNS
=
You will also need a DDNS update service
I use this https://github.com/ayushsharma82/EasyDDNS
You can also see its usage in my example code [PWM_LEDServer.ino](https://github.com/ofekp/TinyUPnP/blob/master/examples/PWM_LEDServer/PWM_LEDServer.ino)

Special thanks
=
[@ajwtech](https://github.com/ajwtech) - for contributing to the package by noting the need to use `constrolURL` instead of `eventSubURL`

[@Lan-Hekary](https://github.com/Lan-Hekary) - for improving the API

[@GIPdA](https://github.com/GIPdA) - for adding ESP32 support
