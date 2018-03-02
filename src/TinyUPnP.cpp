/*
  TinyUPnP.h - Library for creating UPnP rules automatically in your router.
  Created by Ofek Pearl, September 2017.
  Released into the public domain.
*/

#include "Arduino.h"
#include "TinyUPnP.h"


boolean udpConnected = false;
IPAddress ipMulti(239, 255, 255, 250);
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];  // buffer to hold incoming packet
String persistent_uuid;
String device_name;

// timeoutMs - timeout in milli seconds for the operations of this class, -1 for blocking operation.
TinyUPnP::TinyUPnP(int timeoutMs) {
	_timeoutMs = timeoutMs;
}

boolean TinyUPnP::addPortMapping(IPAddress ruleIP, int rulePort, String ruleProtocol, int ruleLeaseDuration, String ruleFriendlyName) {
	unsigned long startTime = millis();
	
	// verify WiFi is connected
	while (WiFi.status() != WL_CONNECTED) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			return false;
			debugPrint("Timeout expired while verifying WiFi connection");
		}
		delay(200);
		debugPrint(".");
	}
	debugPrint("");  // \n
	
	while (!connectUDP()) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			return false;
			debugPrint("Timeout expired while connecting UDP");
		}
		delay(500);
		debugPrint(".");
	}
	debugPrint("");  // \n
	
	broadcastMSearch();
	while (!waitForUnicastResponseToMSearch(&_gwInfo)) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			return false;
			debugPrint("Timeout expired while waiting for the gateway router to respond to M-SEARCH message");
		}
		delay(1);
	}
	
	// verify gateway information is valid
	// TODO: use this _gwInfo to skip the UDP part completely if it is not empty
	if (_gwInfo.host == IPAddress(0, 0, 0, 0) || _gwInfo.port == 0 || _gwInfo.path.length() == 0) {
		debugPrintln("Invalid router info, cannot continue");
		return false;
	}
	
	// close the UDP connection
	_udpClient.stop();
	
	// connect to IGD (TCP connection)
	while (!connectToIGD(_gwInfo.host, _gwInfo.port)) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			debugPrint("Timeout expired while trying to connect to the IGD");
			_wifiClient.stop();
			return false;
		}
		delay(1000);
	}
	
	// get event urls from the gateway IGD
	while (!getIGDEventURLs(&_gwInfo)) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			return false;
			debugPrint("Timeout expired while adding a new port mapping");
		}
		delay(1000);
	}
	
	debugPrintln("port [" + String(_gwInfo.port) + "] actionPort [" + _gwInfo.actionPort + "]");
	if (_gwInfo.port != _gwInfo.actionPort) {
		// in this case we need to connect to a different port
		debugPrintln("Connection port changed, disconneting from IGD");
		_wifiClient.stop();
	}
	
	// connect to IGD (TCP connection) again, if needed, in case we got disconnected after the previous query
	if (!_wifiClient.connected()) {
		while (!connectToIGD(_gwInfo.host, _gwInfo.actionPort)) {
			if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
				debugPrint("Timeout expired while trying to connect to the IGD");
				_wifiClient.stop();
				return false;
			}
			delay(1000);
		}
	}
	
	// add the port mapping
	while (!addPortMappingEntry(ruleIP, rulePort, ruleProtocol, ruleLeaseDuration, ruleFriendlyName, &_gwInfo)) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			debugPrint("Timeout expired while adding a new port mapping");
			return false;
		}
		delay(1000);
	}
	
	_wifiClient.stop();
	
	return true;
}

// a single try to connect UDP multicast address and port of UPnP (239.255.255.250 and 1900 respectively)
// this will enable receiving SSDP packets after the M-SEARCH multicast message will be broadcasted
boolean TinyUPnP::connectUDP() {
	if (_udpClient.beginMulticast(WiFi.localIP(), ipMulti, UPNP_SSDP_PORT)) {
		return true;
	}
	debugPrint("UDP connection failed");
	return false;
}

// broadcast an M-SEARCH message to initiate messages from SSDP devices
// the router should respond to this message by a packat sent to this device's unicast addresss on the
// same UPnP port (1900)
void TinyUPnP::broadcastMSearch() {
	debugPrint("Sending M-SEARCH to [");
	debugPrint(String(ipMulti));
	debugPrint("] Port [");
	debugPrint(String(UPNP_SSDP_PORT));
	debugPrintln("]");

	String message = 
		"M-SEARCH * HTTP/1.1\r\n"
		"HOST: 239.255.255.250:1900\r\n"
		"MAN: \"ssdp:discover\"\r\n"
		"MX: 5\r\n"
		//"ST: urn:schemas-upnp-org:service:WANPPPConnection:1\r\n\r\n";
		//"ST: urn:schemas-upnp-org:device:WANConnectionDevice:1\r\n\r\n";
		"ST: ssdp:all\r\n\r\n";

	_udpClient.beginPacketMulticast(ipMulti, UPNP_SSDP_PORT, WiFi.localIP());
	_udpClient.write(message.c_str());
	_udpClient.endPacket();

	debugPrintln("M-SEARCH sent");
}

// Asuuming an M-SEARCH message was braodcaseted, wait for the response from the IGD (Internet Gateway Devide)
// Note: the response from the IGD is sent back as unicast to this device
// Note: only gateway defined IGD response will be considered, the rest will be ignored
boolean TinyUPnP::waitForUnicastResponseToMSearch(gatewayInfo *deviceInfo) {
	int packetSize = _udpClient.parsePacket();
	IPAddress remoteIP = _udpClient.remoteIP();
	IPAddress gatewayIP = WiFi.gatewayIP();
  
	// only continue if the packet was received from the gateway router
	if (remoteIP != gatewayIP || packetSize <= 0) {
		return false;
	}

	debugPrint("Received packet of size [");
	debugPrint(String(packetSize));
	debugPrint("]");
	debugPrint(" ip [");
	for (int i = 0; i < 4; i++) {
		debugPrint(String(remoteIP[i]));  // Decimal
		if (i < 3) {
			debugPrint(".");
		}
	}
	debugPrint("] port [");
	debugPrint(String(_udpClient.remotePort()));
	debugPrintln("]");
  
	int len = _udpClient.read(packetBuffer, 255);
	if (len > 0) {
		packetBuffer[len] = 0;
	} else {
		return false;
	}

	String response = packetBuffer;
	String responseLowerCase = packetBuffer;
	debugPrintln("Gateway packet content:");
	debugPrintln(response);

	// only continue if the packet is a response to M-SEARCH and it originated from a gateway device
	if (response.indexOf(INTERNET_GATEWAY_DEVICE) == -1) {
		return false;
	}

	// extract location from message
	responseLowerCase.toLowerCase();
	String location = "";
	String location_searchStringStart = "location: ";  // lower case since we look for match in responseLowerCase
	String location_searchStringEnd = "\r\n";
	int location_indexStart = responseLowerCase.indexOf(location_searchStringStart);
	if (location_indexStart != -1) {
		location_indexStart += location_searchStringStart.length();
		int location_indexEnd = response.indexOf(location_searchStringEnd, location_indexStart);
		if (location_indexEnd != -1) {
			location = response.substring(location_indexStart, location_indexEnd);
			location.trim();
		} else {
			return false;
		}
	} else {
		return false;
	}
	
	debugPrint("IGD location found [");
	debugPrint(location);
	debugPrintln("]");
  
	IPAddress host = getHost(location);
	int port = getPort(location);
	String path = getPath(location);
	
	deviceInfo->host = host;
	deviceInfo->port = port;	
	deviceInfo->path = path;
	// the following is the default and may be overridden if URLBase tag is specified
	deviceInfo->actionPort = port;
	
	debugPrintln(ipAddressToString(host));
	debugPrintln(String(port));
	debugPrintln(path);

	return true;
}

// a singly trial to connect to the IGD (with TCP)
boolean TinyUPnP::connectToIGD(IPAddress host, int port) {
	debugPrintln("Connecting to IGD with host [" + ipAddressToString(host) + "] port [" + port + "]");
	if (_wifiClient.connect(host, port)) {
		debugPrintln("Connected to IGD");
		return true;
	}
	return false;
}

// updates deviceInfo with the commands' information of the IGD
boolean TinyUPnP::getIGDEventURLs(gatewayInfo *deviceInfo) {
	// make an HTTP request
	_wifiClient.println("GET " + deviceInfo->path + " HTTP/1.1");
	_wifiClient.println("Content-Type: text/xml; charset=\"utf-8\"");
	//_wifiClient.println("Connection: close");
	_wifiClient.println("Content-Length: 0");
	_wifiClient.println();
	
	// wait for the response
	unsigned long timeout = millis();
	while (_wifiClient.available() == 0) {
		if (millis() - timeout > TCP_CONNECTION_TIMEOUT_MS) {
			debugPrintln("TCP connection timeout while connecting to the IGD");
			//_wifiClient.stop();
			return false;
		}
	}
	
	// read all the lines of the reply from server
	boolean upnpServiceFound = false;
	boolean eventSubURLFound = false;
	boolean urlBaseFound = false;
	while (_wifiClient.available()) {
		String line = _wifiClient.readStringUntil('\r');
		int index_in_line = 0;
		debugPrint(line);
		if (!urlBaseFound && line.indexOf("<URLBase>") >= 0) {
			// e.g. <URLBase>http://192.168.1.1:5432/</URLBase>
			// Note: assuming URL path will only be found in a specific action under the 'eventSubURL' xml tag
			String baseUrl = getTagContent(line, "URLBase");
			if (baseUrl != NULL && baseUrl.length() > 0) {
				baseUrl.trim();
				IPAddress host = getHost(baseUrl);  // this is ignored, assuming router host IP will not change
				int port = getPort(baseUrl);
				deviceInfo->actionPort = port;

				debugPrint("URLBase tag found [");
				debugPrint(baseUrl);
				debugPrintln("]");
				debugPrint("Translated to base host [");
				debugPrint(ipAddressToString(host));
				debugPrint("] and base port [");
				debugPrint(String(port));
				debugPrintln("]");
				urlBaseFound = true;
			}
		}
		
		int service_type_1_index = line.indexOf(UPNP_SERVICE_TYPE_TAG_START + UPNP_SERVICE_TYPE_1 + UPNP_SERVICE_TYPE_TAG_END);
		int service_type_2_index = line.indexOf(UPNP_SERVICE_TYPE_TAG_START + UPNP_SERVICE_TYPE_2 + UPNP_SERVICE_TYPE_TAG_END);
		if (!upnpServiceFound && service_type_1_index >= 0) {
			index_in_line += service_type_1_index;
			debugPrintln(UPNP_SERVICE_TYPE_1 + " service found!");
			upnpServiceFound = true;
			deviceInfo->serviceTypeName = UPNP_SERVICE_TYPE_1;
			// will start looking for 'eventSubURL' now
		} else if (!upnpServiceFound && service_type_2_index >= 0) {
			index_in_line += service_type_2_index;
			debugPrintln(UPNP_SERVICE_TYPE_2 + " service found!");
			upnpServiceFound = true;
			deviceInfo->serviceTypeName = UPNP_SERVICE_TYPE_2;
			// will start looking for 'eventSubURL' now
		}
		
		if (upnpServiceFound && (index_in_line = line.indexOf("<eventSubURL>", index_in_line)) >= 0) {
			String eventSubURLContent = getTagContent(line.substring(index_in_line), "eventSubURL");
			deviceInfo->actionPath = eventSubURLContent;
			eventSubURLFound = true;

			debugPrint("eventSubURL tag found! setting actionPath to [");
			debugPrint(eventSubURLContent);
			debugPrintln("]");
			
			// clear buffer
			while (_wifiClient.available()) {
				String line = _wifiClient.readStringUntil('\r');
				debugPrint(line);
			}
			
			return true;
		}
	}

	return false;
}

// assuming a connection to the IGD has been formed
// will add the port mapping to the IGD
// ruleProtocol - either "TCP" or "UDP"
boolean TinyUPnP::addPortMappingEntry(IPAddress ruleIP, int rulePort, String ruleProtocol, int ruleLeaseDuration, String ruleFriendlyName, gatewayInfo *deviceInfo) {  
	debugPrintln("called addPortMappingEntry");
	debugPrintln("deviceInfo->actionPath [" + deviceInfo->actionPath + "]");
	debugPrintln("deviceInfo->serviceTypeName [" + deviceInfo->serviceTypeName + "]");
	
	_wifiClient.println("POST " + deviceInfo->actionPath + " HTTP/1.1");
	//_wifiClient.println("Connection: close");
	_wifiClient.println("Content-Type: text/xml; charset=\"utf-8\"");
	//_wifiClient.println("Host: " + ipAddressToString(deviceInfo->host) + ":" + String(deviceInfo->actionPort));
	//_wifiClient.println("Accept: */*");
	//_wifiClient.println("Content-Type: application/x-www-form-urlencoded");
	_wifiClient.println("SOAPAction: \"" + deviceInfo->serviceTypeName + "#AddPortMapping\"");
	String body = "<?xml version=\"1.0\"?>"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body>"
		"<u:AddPortMapping xmlns:u=\"" + deviceInfo->serviceTypeName + "\">"
		"<NewRemoteHost></NewRemoteHost>"
		"<NewExternalPort>" + String(rulePort) + "</NewExternalPort>"
		"<NewProtocol>" + ruleProtocol + "</NewProtocol>"
		"<NewInternalPort>" + String(rulePort) + "</NewInternalPort>"
		"<NewInternalClient>" + ipAddressToString(ruleIP) + "</NewInternalClient>"
		"<NewEnabled>1</NewEnabled>"
		"<NewPortMappingDescription>" + ruleFriendlyName + "</NewPortMappingDescription>"
		"<NewLeaseDuration>" + String(ruleLeaseDuration) + "</NewLeaseDuration>"
		"</u:AddPortMapping>"
		"</s:Body>"
		"</s:Envelope>";
	_wifiClient.println("Content-Length: " + String(body.length()));
	_wifiClient.println();
	_wifiClient.println(body);
	_wifiClient.println();
	
	debugPrintln("Content-Length was: " + String(body.length()));
	
	debugPrintln(body);
  
	unsigned long timeout = millis();
	while (_wifiClient.available() == 0) {
		if (millis() - timeout > TCP_CONNECTION_TIMEOUT_MS) {
			debugPrintln("TCP connection timeout while adding a port mapping");
			//_wifiClient.stop();
			return false;
		}
	}

	// TODO: verify success
	boolean isSuccess = true;
	while (_wifiClient.available()) {
		String line = _wifiClient.readStringUntil('\r');
		if (line.indexOf("errorCode") >= 0) {
			isSuccess = false;
		}
		debugPrintln(line);
	}
	debugPrintln("");  // \n\r\n

	return isSuccess;
}


boolean TinyUPnP::printAllPortMappings() {
	debugPrintln("Port Mappings:");
	
	// verify gateway information is valid
	// TODO: use this _gwInfo to skip the UDP part completely if it is not empty
	if (_gwInfo.host == IPAddress(0, 0, 0, 0) || _gwInfo.port == 0 || _gwInfo.path.length() == 0) {
		debugPrintln("Invalid router info, cannot continue");
		return false;
	}
	
	upnpRuleNode *ruleNodeHead_ptr = NULL;
	upnpRuleNode *ruleNodeTail_ptr = NULL;
	
	unsigned long startTime = millis();
	boolean reachedEnd = false;
	int index = 0;
	while (!reachedEnd) {
		// connect to IGD (TCP connection) again, if needed, in case we got disconnected after the previous query
		if (!_wifiClient.connected()) {
			while (!connectToIGD(_gwInfo.host, _gwInfo.actionPort)) {
				if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
					debugPrint("Timeout expired while trying to connect to the IGD");
					_wifiClient.stop();
					return false;
				}
				delay(1000);
			}
		}
		
		debugPrint("Sending query for index [");
		debugPrint(String(index));
		debugPrintln("]");
		
		_wifiClient.println("POST " + _gwInfo.actionPath + " HTTP/1.1");
		_wifiClient.println("Connection: keep-alive");
		_wifiClient.println("Content-Type: text/xml; charset=\"utf-8\"");
		_wifiClient.println("SOAPAction: \"" + _gwInfo.serviceTypeName + "#GetGenericPortMappingEntry\"");
		String body = "<?xml version=\"1.0\"?>"
			"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
			"<s:Body>"
			"<u:GetGenericPortMappingEntry xmlns:u=\"" + _gwInfo.serviceTypeName + "\">"
			"  <NewPortMappingIndex>" + String(index) + "</NewPortMappingIndex>"
			"</u:GetGenericPortMappingEntry>"
			"</s:Body>"
			"</s:Envelope>";
		_wifiClient.println("Content-Length: " + String(body.length()));
		_wifiClient.println();
		_wifiClient.println(body);
		_wifiClient.println();
  
		unsigned long timeout = millis();
		while (_wifiClient.available() == 0) {
			if (millis() - timeout > TCP_CONNECTION_TIMEOUT_MS) {
				debugPrintln("TCP connection timeout while retrieving port mappings");
				_wifiClient.stop();
				return false;
			}
		}
		
		while (_wifiClient.available()) {
			String line = _wifiClient.readStringUntil('\r');
			debugPrint(line);
			if (line.indexOf(PORT_MAPPING_INVALID_INDEX) >= 0) {
				reachedEnd = true;
			} else if (line.indexOf(PORT_MAPPING_INVALID_ACTION) >= 0) {
				debugPrint("Invalid action while reading port mappings");
				reachedEnd = true;
			} else if (line.indexOf("GetGenericPortMappingEntryResponse") >= 0) {
				upnpRule *rule_ptr = new upnpRule();
				rule_ptr->index = index;
				rule_ptr->devFriendlyName = getTagContent(line, "NewPortMappingDescription");
				String newInternalClient = getTagContent(line, "NewInternalClient");
				if (newInternalClient == "") {
					continue;
				}
				rule_ptr->internalAddr.fromString(newInternalClient);
				rule_ptr->internalPort = getTagContent(line, "NewInternalPort").toInt();
				rule_ptr->externalPort = getTagContent(line, "NewExternalPort").toInt();
				rule_ptr->protocol = getTagContent(line, "NewProtocol");
				rule_ptr->leaseDuration = getTagContent(line, "NewLeaseDuration").toInt();
						
				upnpRuleNode *currRuleNode_ptr = new upnpRuleNode();
				currRuleNode_ptr->rule_ptr = rule_ptr;
				currRuleNode_ptr->next_ptr = NULL;
				if (ruleNodeHead_ptr == NULL) {
					ruleNodeHead_ptr = currRuleNode_ptr;
					ruleNodeTail_ptr = currRuleNode_ptr;
				} else {
					ruleNodeTail_ptr->next_ptr = currRuleNode_ptr;
					ruleNodeTail_ptr = currRuleNode_ptr;
				}
			}
		}
		
		index++;
		delay(500);
	}
	
	// print nicely
	upnpRuleNode *curr_ptr = ruleNodeHead_ptr;
	while (curr_ptr != NULL) {
		upnpRuleToString(curr_ptr->rule_ptr);
		curr_ptr = curr_ptr->next_ptr;
	}
	
	_wifiClient.stop();
	
	return true;
}

String TinyUPnP::upnpRuleToString(upnpRule *rule_ptr) {
	String index = String(rule_ptr->index);
	Serial.print(index);
	Serial.print(".");
	Serial.print(getSpacesString(5 - (index.length() + 1)));  // considering the '.' too
	
	String devFriendlyName = rule_ptr->devFriendlyName;
	Serial.print(devFriendlyName);
	Serial.print(getSpacesString(30	- devFriendlyName.length()));

	String internalAddr = ipAddressToString(rule_ptr->internalAddr);
	Serial.print(internalAddr);
    Serial.print(getSpacesString(18 - internalAddr.length()));
	
	String internalPort = String(rule_ptr->internalPort);
	Serial.print(internalPort);
	Serial.print(getSpacesString(7 - internalPort.length()));

	String externalPort = String(rule_ptr->externalPort);
	Serial.print(externalPort);
	Serial.print(getSpacesString(7 - externalPort.length()));

	String protocol = rule_ptr->protocol;
	Serial.print(protocol);
	Serial.print(getSpacesString(7 - protocol.length()));

	String leaseDuration = String(rule_ptr->leaseDuration);
	Serial.print(leaseDuration);
	Serial.print(getSpacesString(7 - leaseDuration.length()));
	
	Serial.println();
}

String TinyUPnP::getSpacesString(int num) {
	if (num < 0) {
		num = 1;
	}
	String spaces = "";
	for (int i = 0; i < num; i++) {
		spaces += " ";
	}
	return spaces;
}

IPAddress TinyUPnP::ipToAddress(String ip) {
  int Parts[4] = {0,0,0,0};
  int Part = 0;
  for (int i = 0; i < ip.length(); i++)
  {
    char c = ip[i];
    if ( c == '.' )
    {
      Part++;
      continue;
    }
    Parts[Part] *= 10;
    Parts[Part] += c - '0';
  }
  return IPAddress(Parts[0], Parts[1], Parts[2], Parts[3]);
}

char* TinyUPnP::ipAddressToCharArr(IPAddress ipAddress) {
	char s[17];
	sprintf(s, "%d.%d.%d.%d", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
	s[16] = '\0';
	return s;
}

String TinyUPnP::ipAddressToString(IPAddress ipAddress) {
	return String(ipAddress[0]) + "." + String(ipAddress[1]) + "." + String(ipAddress[2]) + "." + String(ipAddress[3]);
}

IPAddress TinyUPnP::getHost(String url) {
  if (url.indexOf("https://") != -1) {
    url.replace("https://", "");
  }
  if (url.indexOf("http://") != -1) {
    url.replace("http://", "");
  }
  int endIndex = url.indexOf('/');
  if (endIndex != -1) {
    url = url.substring(0, endIndex);
  }
  if (url.indexOf(':') != -1) {
    url = url.substring(0, url.indexOf(':'));
  }
  return ipToAddress(url);
}

int TinyUPnP::getPort(String url) {
  int port = -1;
  if (url.indexOf("https://") != -1) {
    url.replace("https://", "");
  }
  if (url.indexOf("http://") != -1) {
    url.replace("http://", "");
  }
  url = url.substring(0, url.indexOf("/"));
  if (url.indexOf(":") != -1) {
    url = url.substring(url.indexOf(":") + 1, url.length());
    port = url.toInt();
  } else {
    port = 80;
  }
  return port;
}

String TinyUPnP::getPath(String url) {
  int port = -1;
  if (url.indexOf("https://") != -1) {
    url.replace("https://", "");
  }
  if (url.indexOf("http://") != -1) {
    url.replace("http://", "");
  }
  return url.substring(url.indexOf("/"), url.length());
}

String TinyUPnP::getTagContent(String line, String tagName) {
  int startIndex = line.indexOf("<" + tagName + ">");
  if (startIndex == -1) {
    return "";
  }
  startIndex += tagName.length() + 2;
  int endIndex = line.indexOf("</" + tagName + ">");
  if (endIndex == -1) {
    return "";
  }
  return line.substring(startIndex, endIndex);
}

// prints a message to Serial only if IS_DEBUG is set to true
void TinyUPnP::debugPrint(String message) {
	if (IS_DEBUG) {
		Serial.print(message);
	}
}

// println-s a message to Serial only if IS_DEBUG is set to true
void TinyUPnP::debugPrintln(String message) {
	if (IS_DEBUG) {
		Serial.println(message);
	}
}
