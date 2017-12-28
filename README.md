# TinyUPnP
A very small UPnP IGD implementation for your ESP8266

Installation
==
Just clone or download zip, then simply copy the folder TinyUPnP to the Arduino IDE "libraries" folder e.g "D:\arduino-1.6.8\libraries".

Usage and More Information
==
Declare:
```
unsigned long lastUpdateTime = 0;
TinyUPnP *tinyUPnP = new TinyUPnP(-1);  // -1 means blocking, preferably, use a timeout value (ms)
```
Setup:
```
if (tinyUPnP->addPortMapping(WiFi.localIP(), LISTEN_PORT, RULE_PROTOCOL_TCP, LEASE_DURATION, FRIENDLY_NAME)) {
    lastUpdateTime = millis();
}
```
Loop:
```
// update UPnP port mapping rule if needed
if ((millis() - lastUpdateTime) > (long) (0.8D * (double) (LEASE_DURATION * 1000.0))) {
    Serial.print("UPnP rule is about to be revoked, renewing lease");
    if (tinyUPnP->addPortMapping(WiFi.localIP(), LISTEN_PORT, RULE_PROTOCOL_TCP, LEASE_DURATION, FRIENDLY_NAME)) {
        lastUpdateTime = millis();
    }
}
```
I only checked it with my D-Link router.

To anyone interested in how the library works:
--
1. It sends an M_SEARCH message to UPnP UDP multicast address.
1. The gateway router will respond with a message including an HTTP header called Location.
1. `Location` is a link to an XML file containing the IGD (Internet Gateway Device) API in order to create the needed calls which will add the new port mapping to your gateway router.
1. One of the services that is depicted in the XML is `<serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>` which is what the library is looking for.
1. That service will include a `eventSubURL` tag which is a link to your router's IGD API. (The base URL is also depicted in the same file under the tag URLBase)
1. Using the base URL and the WANPPPConnection link you can issue an HTTP query to the router that will add the UPnP rule.
1. As a side note, the service depicted in the XML also includes a SCPDURL tag which is a link to another XML that depicts commands available for the service and their parameters. The package skips this stage as I assumed the query will be similar for many routers, this may very well not be the case, though, so it is up to you to check.
1. From this stage the package will issue the service command using an HTTP query to the router. The actual query can be seen in the code quite clearly but for anyone interested:
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
--
You will also need a DDNS update service
I use this https://github.com/ayushsharma82/EasyDDNS

Beer
==
If you like what I got, support me by buying me a :beer: [Beer](https://www.paypal.me/ofekpearl/5usd) and cheers to you!
