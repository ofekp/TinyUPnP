# TinyUPnP
A very small UPnP IGD implementation for ESP8266 and ESP32.
Your device opens its own ports on the router (port forwarding), so it can be reached from the internet without touching the router settings.

[![Donate with PayPal](https://img.shields.io/badge/Donate-PayPal-00457C?logo=paypal)](https://www.paypal.com/donate/?hosted_button_id=NFFEPLZ8PU7RA)
[![GitHub stars](https://img.shields.io/github/stars/ofekp/TinyUPnP?style=social)](https://github.com/ofekp/TinyUPnP/stargazers)

> :coffee: TinyUPnP is free and maintained in my spare time. If it saved you an evening of digging through router settings, you can [buy me a coffee](https://www.paypal.com/donate/?hosted_button_id=NFFEPLZ8PU7RA), any amount is appreciated.
> Not in a position to donate? A :star: helps other makers find the project, and that helps a lot too!

# Installation

**TinyUPnP**

Arduino IDE -> Sketch -> Include Library -> Manage Libraries -> search for "TinyUPnP" -> Install.

Alternatively, clone or download as zip, then copy the folder TinyUPnP to your Arduino IDE "libraries" folder e.g "D:\Arduino\libraries".
If you are still unable to include the package, go to Arduino IDE preferences and make sure that sketchbook location points to the correct Arduino directory.

**Prerequisites**

Boards:
* ESP8266 - Follow https://randomnerdtutorials.com/how-to-install-esp8266-board-arduino-ide/
* ESP32 - Arduino IDE -> Board Manager -> search for "esp32" -> install esp32 by Espressif Systems (or by Arduino, according to your board version). For older Arduino IDE versions you can follow https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/

USB to UART Bridge Drivers:
* If you connect the ESP32 to your computer and it is not recognized in the Arduino IDE (i.e. it does not appear in Tools -> Port) you might need to install the drivers **CP210x USB to UART Bridge VCP** from https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers?tab=downloads
After extracting the ZIP file, updated the driver (on Win using the device manager), install it by picking the extracted directory manually.

Other Libraries:
* EasyDDNS (optional) - Used to tell your DDNS server what the IP of your Gateway Router is. To install:
  Arduino IDE -> Sketch -> Include Library -> Manage Libraries -> search for "EasyDDNS" -> Install.

# Usage

**Please refer to the examples bundled with the TinyUPnP library.**

**Include**
```cpp
#include "TinyUPnP.h"
#include <EasyDDNS.h>  // optional
```

**Declare**
```cpp
TinyUPnP *tinyUPnP = new TinyUPnP(20000);  // 0 for blocking (preferably, use a timeout value in [ms])
```
**Setup**
```cpp
// you may repeat 'addPortMappingConfig' more than once
tinyUPnP->addPortMappingConfig(WiFi.localIP(), LISTEN_PORT, RULE_PROTOCOL_TCP, LEASE_DURATION, FRIENDLY_NAME);

// finally, commit the port mappings to the IGD
// only the router is needed for this, internet access is not required
portMappingAdded = tinyUPnP->commitPortMappings();

// optional, check that the device can also reach the internet
if (!tinyUPnP->testInternetConnectivity()) {
  Serial.println("No internet, the device is only reachable from the local network");
}
```
**Loop**
```cpp
// update UPnP port mapping every ms interval
// you can provide an optional method for reconnecting to the WiFi (otherwise leave as NULL)
tinyUPnP->updatePortMappings(600000, &connectWiFi);  // 10 minutes
```
**API**

This is specific for the example code, you can do what you like here
```
http://<IP or DDNS>:<LISTEN_PORT>/?percentage=<0..100>
```
**Print**
```cpp
// print all the port mappings that were configured using 'addPortMappingConfig' in the setup step
tinyUPnP->printPortMappingConfig();

// print all the current port mappings from the IGD
tinyUPnP->printAllPortMappings();
```
**Debug**

Debug prints are off by default. To turn them on, uncomment `#define UPNP_DEBUG` in [TinyUPnP.h#L15](https://github.com/ofekp/TinyUPnP/blob/master/src/TinyUPnP.h#L15).
Note that the `print...()` methods above only print when debug is on.

# Issues
When reporting issues, attach the full log (i.e. with `UPNP_DEBUG` turned on) and add the serial output to the issue as a text file attachment.

# For anyone interested in how the library works
1. It sends an M_SEARCH message to UPnP UDP multicast address.
2. The gateway router will respond with a message including an HTTP header called Location.
3. `Location` is a link to an XML file containing the IGD (Internet Gateway Device) API in order to create the needed calls which will add the new port mapping to your gateway router.
4. One of the services that is depicted in the XML is `<serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>` which is what the library is looking for.
5. That service will include a `controlURL` (`eventSubURL`) tag which is a link to your router's IGD API. (The base URL is also depicted in the same file under the tag URLBase)
6. Using the base URL and the `controlURL` (WANPPPConnection) link you can issue an HTTP query to the router that will add the UPnP rule.
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

# Detailed Document Released by UPnP Forum
http://upnp.org/specs/arch/UPnP-arch-DeviceArchitecture-v2.0.pdf

# DDNS
You will also need a DDNS update service
I use this https://github.com/ayushsharma82/EasyDDNS
You can also see its usage in my example code [PWM_LEDServer.ino](https://github.com/ofekp/TinyUPnP/blob/master/examples/PWM_LEDServer/PWM_LEDServer.ino)

# Special thanks
[@ajwtech](https://github.com/ajwtech) - for contributing to the package by noting the need to use `controlURL` instead of `eventSubURL`

[@Lan-Hekary](https://github.com/Lan-Hekary) - for improving the API

[@GIPdA](https://github.com/GIPdA) - for adding ESP32 support

[@nullstalgia](https://github.com/nullstalgia) and [@ArtemPisarenko](https://github.com/ArtemPisarenko) - for memory leak fixes and separate internal/external ports

# Support the project
If TinyUPnP works with your router, it is thanks to bug reports, fixes and support from people like you.
You can help by [opening an issue](https://github.com/ofekp/TinyUPnP/issues) with your router model, sending a PR, [donating](https://www.paypal.com/donate/?hosted_button_id=NFFEPLZ8PU7RA) or simply giving the project a :star:.
