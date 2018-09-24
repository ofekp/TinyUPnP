/*
  TinyUPnP.h - Library for creating UPnP rules automatically in your router.
  Created by Ofek Pearl, September 2017.
*/

#include "Arduino.h"
#include "TinyUPnP.h"
#include "ESP8266Ping.h"

IPAddress ipMulti(239, 255, 255, 250);  // multicast address for SSDP
IPAddress connectivityTestIp(8, 8, 8, 8);  // Google DNS server
char packetBuffer[UDP_TX_PACKET_MAX_SIZE];  // buffer to hold incoming packet UDP_TX_PACKET_MAX_SIZE=8192
char packetBufferLowerCase[UDP_TX_PACKET_MAX_SIZE];  // buffer to hold incoming packet UDP_TX_PACKET_MAX_SIZE=8192

char body_tmp[1200];
char integer_string[32];

// timeoutMs - timeout in milli seconds for the operations of this class, -1 for blocking operation.
TinyUPnP::TinyUPnP(int timeoutMs = 20000) {
	_timeoutMs = timeoutMs;
	_lastUpdateTime = millis();
	_consequtiveFails = 0;
}

TinyUPnP::~TinyUPnP() {
}

void TinyUPnP::setMappingConfig(IPAddress ruleIP, int rulePort, String ruleProtocol, int ruleLeaseDuration, String ruleFriendlyName) {
	this->_ruleIP = ruleIP;
	this->_rulePort = rulePort;
	this->_ruleProtocol = ruleProtocol;
	this->_ruleLeaseDuration = ruleLeaseDuration;
	this->_ruleFriendlyName = ruleFriendlyName;
}

boolean TinyUPnP::addPortMapping() {
	unsigned long startTime = millis();
	
	// verify WiFi is connected
	if (!testConnectivity(startTime)) {
		return false;
	}
	
	// get all the needed IGD information using SSDP if we don't have it already
	if (!isGatewayInfoValid(&_gwInfo)) {
		getGatewayInfo(&_gwInfo, startTime);
	}
	
	// verify gateway information is valid
	if (!isGatewayInfoValid(&_gwInfo)) {
		debugPrintln(F("ERROR: Invalid router info, cannot continue"));
		_wifiClient.stop();
		return false;
	}
	
	debugPrint(F("port ["));
	debugPrint(String(_gwInfo.port));
	debugPrint(F("] actionPort ["));
	debugPrint(String(_gwInfo.actionPort));
	debugPrintln(F("]"));
	if (_gwInfo.port != _gwInfo.actionPort) {
		// in this case we need to connect to a different port
		debugPrintln(F("Connection port changed, disconnecting from IGD"));
		_wifiClient.stop();
	}
	
	// TODO: since verifyPortMapping connects to the IGD then addPortMappingEntry can skip it
	while (!verifyPortMapping(&_gwInfo)) {
		// add the port mapping
		addPortMappingEntry(&_gwInfo);
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			debugPrintln(F("Timeout expired while trying to add the port mapping"));
			_wifiClient.stop();
			return false;
		}
		delay(1000);  // longer delay to allow more time for the router to update its rules
	}
	
	_wifiClient.stop();
	
	return true;
}

boolean TinyUPnP::getGatewayInfo(gatewayInfo *deviceInfo, long startTime) {
	while (!connectUDP()) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			debugPrint(F("Timeout expired while connecting UDP"));
			_udpClient.stop();
			return false;
		}
		delay(500);
		debugPrint(".");
	}
	debugPrintln("");  // \n
	
	broadcastMSearch();
	IPAddress gatewayIP = WiFi.gatewayIP();
	while (!waitForUnicastResponseToMSearch(deviceInfo, gatewayIP)) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			debugPrintln(F("Timeout expired while waiting for the gateway router to respond to M-SEARCH message"));
			_udpClient.stop();
			return false;
		}
		delay(1);
	}

	// close the UDP connection
	_udpClient.stop();

	// connect to IGD (TCP connection)
	while (!connectToIGD(_gwInfo.host, _gwInfo.port)) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			debugPrintln(F("Timeout expired while trying to connect to the IGD"));
			_wifiClient.stop();
			return false;
		}
		delay(500);
	}
	
	// get event urls from the gateway IGD
	while (!getIGDEventURLs(&_gwInfo)) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			debugPrintln(F("Timeout expired while adding a new port mapping"));
			_wifiClient.stop();
			return false;
		}
		delay(500);
	}

	return true;
}

void TinyUPnP::clearGatewayInfo() {
	_gwInfo.host = IPAddress(0, 0, 0, 0);
	_gwInfo.port = 0;
	_gwInfo.path = "";
	_gwInfo.actionPort = 0;
	_gwInfo.actionPath = "";
	_gwInfo.serviceTypeName = "";
}

boolean TinyUPnP::isGatewayInfoValid(gatewayInfo *deviceInfo) {
	debugPrint(F("isGatewayInfoValid ["));
	debugPrint(ipAddressToString(deviceInfo->host));
	debugPrint(F("] port ["));
	debugPrint(String(deviceInfo->port));
	debugPrint(F("] path ["));
	debugPrint(deviceInfo->path);
	debugPrint(F("] actionPort ["));
	debugPrint(String(deviceInfo->actionPort));
	debugPrint(F("] actionPath ["));
	debugPrint(deviceInfo->actionPath);
	debugPrint(F("] serviceTypeName ["));
	debugPrint(deviceInfo->serviceTypeName);
	debugPrintln(F("]"));

	if (deviceInfo->host == IPAddress(0, 0, 0, 0)
		|| deviceInfo->port == 0
		|| deviceInfo->path.length() == 0
		|| deviceInfo->actionPort == 0) {
		debugPrintln(F("Gateway info is not valid"));
		return false;
	}
}

UpdateState TinyUPnP::updatePortMapping(unsigned long intervalMs, callback_function fallback) {
    if (millis() - _lastUpdateTime >= intervalMs) {
		debugPrintln(F("Updating port mapping"));

		// fallback
		if (_consequtiveFails >= MAX_NUM_OF_UPDATES_WITH_NO_EFFECT) {
			debugPrint(F("ERROR: Too many times with no effect on updatePortMapping. Current number of fallbacks times ["));
			debugPrint(String(_consequtiveFails));
			debugPrintln(F("]"));

			_consequtiveFails = 0;
			clearGatewayInfo();
			if (fallback != NULL) {
				debugPrintln(F("Executing fallback method"));
				fallback();
			}

			return ERROR;
		}

		// } else if (_consequtiveFails > 300) {
		// 	ESP.restart();  // should test as last resort
		// 	return;
		// }
		
		unsigned long startTime = millis();

		// verify WiFi is and Internet connection
		if (!testConnectivity(startTime)) {
			_lastUpdateTime += intervalMs / 2;  // delay next try
			_consequtiveFails++;
			return ERROR;
		}
		
		if (verifyPortMapping(&_gwInfo)) {
			debugPrintln(F("Port mapping was already found in the IGD, not doing anything"));
			_lastUpdateTime = millis();
			_wifiClient.stop();
			_consequtiveFails = 0;
			return ALREADY_MAPPED;
		}
		
		debugPrintln("Adding port mapping");
		if (addPortMapping()) {
			_lastUpdateTime = millis();
			debugPrintln(F("UPnP port mapping was added"));
			_wifiClient.stop();
			_consequtiveFails = 0;
			return SUCCESS;
		} else {
			_lastUpdateTime += intervalMs / 2;  // delay next try
			debugPrintln(F("ERROR: While updating UPnP port mapping"));
			_wifiClient.stop();
			_consequtiveFails++;
			return ERROR;
		}
	}

	_wifiClient.stop();
	return NOP;
}

boolean TinyUPnP::testConnectivity(long startTime) {
	IPAddress gatewayIP = WiFi.gatewayIP();
	debugPrint(F("Testing WiFi connection for ["));
	debugPrint(ipAddressToString(WiFi.localIP()));
	debugPrint("]");
	while (WiFi.status() != WL_CONNECTED) {
		if (_timeoutMs > 0 && startTime > 0 && (millis() - startTime > _timeoutMs)) {
			debugPrint(F(" ==> Timeout expired while verifying WiFi connection"));
			_wifiClient.stop();
			return false;
		}
		delay(200);
		debugPrint(".");
	}
	debugPrintln(" ==> GOOD");  // \n

	debugPrint(F("Testing internet connection"));
	if (Ping.ping(connectivityTestIp)) {
		debugPrintln(F(" ==> GOOD"));
		return true;
	} else {
		debugPrintln(F(" ==> BAD"));
		return false;
	}
}

boolean TinyUPnP::verifyPortMapping(gatewayInfo *deviceInfo) {
	debugPrintln(F("Verifying rule in IGD"));

	// connect to IGD (TCP connection) again, if needed, in case we got disconnected after the previous query
	unsigned long timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
	if (!_wifiClient.connected()) {
		while (!connectToIGD(_gwInfo.host, _gwInfo.actionPort)) {
			if (millis() > timeout) {
				debugPrintln(F("Timeout expired while trying to connect to the IGD"));
				_wifiClient.stop();
				return false;
			}
			delay(500);
		}
	}

	strcpy_P(body_tmp, PSTR("<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n<s:Body>\r\n<u:GetSpecificPortMappingEntry xmlns:u=\"urn:schemas-upnp-org:service:WANPPPConnection:1\">\r\n<NewRemoteHost></NewRemoteHost>\r\n<NewExternalPort>"));
	sprintf(integer_string, "%d", _rulePort);
	strcat_P(body_tmp, integer_string);
	strcat_P(body_tmp, PSTR("</NewExternalPort>\r\n<NewProtocol>"));
	strcat_P(body_tmp, _ruleProtocol.c_str());
	strcat_P(body_tmp, PSTR("</NewProtocol>\r\n</u:GetSpecificPortMappingEntry>\r\n</s:Body>\r\n</s:Envelope>\r\n"));

	sprintf(integer_string, "%d", strlen(body_tmp));

	_wifiClient.print(F("POST "));

	_wifiClient.print(deviceInfo->actionPath);
	_wifiClient.println(F(" HTTP/1.1"));
	_wifiClient.println(F("Connection: close"));
	_wifiClient.println(F("Content-Type: text/xml; charset=\"utf-8\""));
	_wifiClient.println("Host: " + ipAddressToString(deviceInfo->host) + ":" + String(deviceInfo->actionPort));
	_wifiClient.println(F("SOAPAction: \"urn:schemas-upnp-org:service:WANPPPConnection:1#GetSpecificPortMappingEntry\""));
	_wifiClient.print(F("Content-Length: "));
	_wifiClient.println(integer_string);
	_wifiClient.println();

	_wifiClient.println(body_tmp);
	_wifiClient.println();
	
	debugPrintln(body_tmp);

	timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
	while (_wifiClient.available() == 0) {
		if (millis() > timeout) {
			debugPrintln(F("TCP connection timeout while retrieving port mappings"));
			_wifiClient.stop();
			// TODO: in this case we might not want to add the ports right away
			// might want to try again or only start adding the ports after we definitely
			// did not see them in the router list
			return false;
		}
	}
	
	// TODO: extract the current lease duration and return it instead of a boolean
	boolean isSuccess = false;
	while (_wifiClient.available()) {
		String line = _wifiClient.readStringUntil('\r');
		if (line.indexOf(F("errorCode")) >= 0) {
			isSuccess = false;
		}
		if (line.indexOf(ipAddressToString(_ruleIP)) >= 0) {
			isSuccess = true;
		}
		debugPrint(line);
	}
	debugPrintln("");  // \n

	_wifiClient.stop();
	
	if (isSuccess) {
		debugPrintln(F("Port mapping found in IGD"));
	} else {
		debugPrintln(F("Could not find port mapping in IGD"));
	}
	
	return isSuccess;
}
// a single try to connect UDP multicast address and port of UPnP (239.255.255.250 and 1900 respectively)
// this will enable receiving SSDP packets after the M-SEARCH multicast message will be broadcasted
boolean TinyUPnP::connectUDP() {
	if (_udpClient.beginMulticast(WiFi.localIP(), ipMulti, UPNP_SSDP_PORT)) {
		return true;
	}
	debugPrintln(F("UDP connection failed"));
	return false;
}

// broadcast an M-SEARCH message to initiate messages from SSDP devices
// the router should respond to this message by a packet sent to this device's unicast addresss on the
// same UPnP port (1900)
void TinyUPnP::broadcastMSearch() {
	debugPrint(F("Sending M-SEARCH to ["));
	debugPrint(ipAddressToString(ipMulti));
	debugPrint(F("] Port ["));
	debugPrint(String(UPNP_SSDP_PORT));
	debugPrintln(F("]"));

	_udpClient.beginPacketMulticast(ipMulti, UPNP_SSDP_PORT, WiFi.localIP());

	strcpy_P(body_tmp, PSTR("M-SEARCH * HTTP/1.1\r\n"));
	strcat_P(body_tmp, PSTR("HOST: 239.255.255.250:1900\r\n"));
	strcat_P(body_tmp, PSTR("MAN: \"ssdp:discover\"\r\n"));
	strcat_P(body_tmp, PSTR("MX: 5\r\n"));
	strcat_P(body_tmp, PSTR("ST: ssdp:all\r\n\r\n"));

	_udpClient.write(body_tmp);
	_udpClient.endPacket();

	debugPrintln(F("M-SEARCH sent"));
}

// Assuming an M-SEARCH message was broadcaseted, wait for the response from the IGD (Internet Gateway Device)
// Note: the response from the IGD is sent back as unicast to this device
// Note: only gateway defined IGD response will be considered, the rest will be ignored
boolean TinyUPnP::waitForUnicastResponseToMSearch(gatewayInfo *deviceInfo, IPAddress gatewayIP) {
	int packetSize = _udpClient.parsePacket();
	// only continue is a packet is available
	if (packetSize <= 0) {
		return false;
	}

	IPAddress remoteIP = _udpClient.remoteIP();
	// only continue if the packet was received from the gateway router
	if (remoteIP != gatewayIP) {
		return false;
	}

	debugPrint(F("Received packet of size ["));
	debugPrint(String(packetSize));
	debugPrint(F("]"));
	debugPrint(F(" ip ["));
	for (int i = 0; i < 4; i++) {
		debugPrint(String(remoteIP[i]));  // Decimal
		if (i < 3) {
			debugPrint(F("."));
		}
	}
	debugPrint(F("] port ["));
	debugPrint(String(_udpClient.remotePort()));
	debugPrintln(F("]"));
  
	memset(packetBuffer, 0, UDP_TX_PACKET_MAX_SIZE);
	int len = _udpClient.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
	if (len > 0 && len < UDP_TX_PACKET_MAX_SIZE) {
		debugPrint(F("UDP packet read bytes ["));
		debugPrint(String(len));
		debugPrintln(F("]"));
		packetBuffer[len] = '\0';
	} else {
		return false;
	}

	debugPrintln(F("Gateway packet content (many variations for debug):"));
	debugPrintln(F("char at 0"));
	Serial.println(packetBuffer[0]);
	debugPrintln(F("char at 1"));
	Serial.println(packetBuffer[1]);
	debugPrintln(F("packetBuffer:"));
	Serial.println((char*) packetBuffer);

	// only continue if the packet is a response to M-SEARCH and it originated from a gateway device
	if (strstr(packetBuffer, INTERNET_GATEWAY_DEVICE) == NULL) {
		debugPrintln(F("INTERNET_GATEWAY_DEVICE was not found"));
		return false;
	}

	debugPrintln(F("INTERNET_GATEWAY_DEVICE found"));

	// extract location from message
	int i;
	for (i = 0; i < len; i++) {
		packetBufferLowerCase[i] = tolower(packetBuffer[i]);
	}
	packetBufferLowerCase[i] = '\0';

	String location = "";
	char* location_indexStart = strstr(packetBufferLowerCase, "location:");  // lower case since we look for match in responseLowerCase
	if (location_indexStart != NULL) {
		location_indexStart += 9;  // "location:".length()
		char* location_indexEnd = strstr(location_indexStart, "\r\n");
		if (location_indexEnd != NULL) {
			// when we copy to locationCharArr we are interested in packetBuffer rather than its lower case version
			// this is because some routers are case sensitive with URLs, refer to issue #14.
			int urlLength = location_indexEnd - location_indexStart;
			int arrLength = urlLength + 1;  // + 1 for '\0'
			// converting the start index to be inside the packetBuffer rather than packetBufferLowerCase
			char* startPtrInPacketBuffer = packetBuffer + (location_indexStart - packetBufferLowerCase);
			char locationCharArr[arrLength];
			memcpy(locationCharArr, startPtrInPacketBuffer, urlLength);
			locationCharArr[arrLength - 1] = '\0';
			location = String(locationCharArr);
			location.trim();
		} else {
			return false;
		}
	} else {
		debugPrintln(F("ERROR: LOCATION param was not found"));
		return false;
	}
	
	debugPrint(F("IGD location found ["));
	debugPrint(location);
	debugPrintln(F("]"));
  
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

// a single trial to connect to the IGD (with TCP)
boolean TinyUPnP::connectToIGD(IPAddress host, int port) {
	debugPrint(F("Connecting to IGD with host ["));
	debugPrint(ipAddressToString(host));
	debugPrint(F("] port ["));
	debugPrint(String(port));
	debugPrintln(F("]"));
	if (_wifiClient.connect(host, port)) {
		debugPrintln(F("Connected to IGD"));
		return true;
	}
	return false;
}

// updates deviceInfo with the commands' information of the IGD
boolean TinyUPnP::getIGDEventURLs(gatewayInfo *deviceInfo) {
	debugPrintln("called getIGDEventURLs");
	debugPrint(F("deviceInfo->actionPath ["));
	debugPrint(deviceInfo->actionPath);
	debugPrint(F("] deviceInfo->path ["));
	debugPrint(deviceInfo->path);
	debugPrintln(F("]"));

	// make an HTTP request
	_wifiClient.print(F("GET "));
	_wifiClient.print(deviceInfo->path);
	_wifiClient.println(F(" HTTP/1.1"));
	_wifiClient.println(F("Content-Type: text/xml; charset=\"utf-8\""));
	//_wifiClient.println(F("Connection: close"));
	_wifiClient.println("Host: " + ipAddressToString(deviceInfo->host) + ":" + String(deviceInfo->actionPort));
	_wifiClient.println(F("Content-Length: 0"));
	_wifiClient.println();
	
	// wait for the response
	unsigned long timeout = millis();
	while (_wifiClient.available() == 0) {
		if (millis() - timeout > TCP_CONNECTION_TIMEOUT_MS) {
			debugPrintln(F("TCP connection timeout while executing getIGDEventURLs"));
			_wifiClient.stop();
			return false;
		}
	}
	
	// read all the lines of the reply from server
	boolean upnpServiceFound = false;
	boolean controlURLFound = false;
	boolean urlBaseFound = false;
	while (_wifiClient.available()) {
		String line = _wifiClient.readStringUntil('\r');
		int index_in_line = 0;
		debugPrint(line);
		if (!urlBaseFound && line.indexOf(F("<URLBase>")) >= 0) {
			// e.g. <URLBase>http://192.168.1.1:5432/</URLBase>
			// Note: assuming URL path will only be found in a specific action under the 'controlURL' xml tag
			String baseUrl = getTagContent(line, "URLBase");
			if (baseUrl.length() > 0) {
				baseUrl.trim();
				IPAddress host = getHost(baseUrl);  // this is ignored, assuming router host IP will not change
				int port = getPort(baseUrl);
				deviceInfo->actionPort = port;

				debugPrint(F("URLBase tag found ["));
				debugPrint(baseUrl);
				debugPrintln(F("]"));
				debugPrint(F("Translated to base host ["));
				debugPrint(ipAddressToString(host));
				debugPrint(F("] and base port ["));
				debugPrint(String(port));
				debugPrintln(F("]"));
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
			// will start looking for 'controlURL' now
		} else if (!upnpServiceFound && service_type_2_index >= 0) {
			index_in_line += service_type_2_index;
			debugPrintln(UPNP_SERVICE_TYPE_2 + " service found!");
			upnpServiceFound = true;
			deviceInfo->serviceTypeName = UPNP_SERVICE_TYPE_2;
			// will start looking for 'controlURL' now
		}
		
		if (upnpServiceFound && (index_in_line = line.indexOf("<controlURL>", index_in_line)) >= 0) {
			String controlURLContent = getTagContent(line.substring(index_in_line), "controlURL");
			if (controlURLContent.length() > 0) {
				deviceInfo->actionPath = controlURLContent;
				controlURLFound = true;

				debugPrint(F("controlURL tag found! setting actionPath to ["));
				debugPrint(controlURLContent);
				debugPrintln(F("]"));
				
				// clear buffer
				debugPrintln(F("Flushing the rest of the response"));
				while (_wifiClient.available()) {
					_wifiClient.read();
				}
				
				// now we have (upnpServiceFound && controlURLFound == true)
				return true;
			}
		}
	}

	return false;
}

// assuming a connection to the IGD has been formed
// will add the port mapping to the IGD
boolean TinyUPnP::addPortMappingEntry(gatewayInfo *deviceInfo) {
	debugPrintln(F("called addPortMappingEntry"));

	// connect to IGD (TCP connection) again, if needed, in case we got disconnected after the previous query
	unsigned long timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
	if (!_wifiClient.connected()) {
		while (!connectToIGD(_gwInfo.host, _gwInfo.actionPort)) {
			if (millis() > timeout) {
				debugPrintln(F("Timeout expired while trying to connect to the IGD"));
				_wifiClient.stop();
				return false;
			}
			delay(500);
		}
	}

	debugPrint(F("deviceInfo->actionPath ["));
	debugPrint(deviceInfo->actionPath);
	debugPrintln(F("]"));

	debugPrint(F("deviceInfo->serviceTypeName ["));
	debugPrint(deviceInfo->serviceTypeName);
	debugPrintln(F("]"));

	strcpy_P(body_tmp, PSTR("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:AddPortMapping xmlns:u=\""));
	strcat_P(body_tmp, deviceInfo->serviceTypeName.c_str());
	strcat_P(body_tmp, PSTR("\"><NewRemoteHost></NewRemoteHost><NewExternalPort>"));
	sprintf(integer_string, "%d", _rulePort);
	strcat_P(body_tmp, integer_string);
	strcat_P(body_tmp, PSTR("</NewExternalPort><NewProtocol>"));
	strcat_P(body_tmp, _ruleProtocol.c_str());
	strcat_P(body_tmp, PSTR("</NewProtocol><NewInternalPort>"));
	sprintf(integer_string, "%d", _rulePort);
	strcat_P(body_tmp, integer_string);
	strcat_P(body_tmp, PSTR("</NewInternalPort><NewInternalClient>"));
	strcat_P(body_tmp, ipAddressToString(_ruleIP).c_str());
	strcat_P(body_tmp, PSTR("</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>"));
	strcat_P(body_tmp, _ruleFriendlyName.c_str());
	strcat_P(body_tmp, PSTR("</NewPortMappingDescription><NewLeaseDuration>"));
	sprintf(integer_string, "%d", _ruleLeaseDuration);
	strcat_P(body_tmp, integer_string);
	strcat_P(body_tmp, PSTR("</NewLeaseDuration></u:AddPortMapping></s:Body></s:Envelope>"));

	sprintf(integer_string, "%d", strlen(body_tmp));
	
	_wifiClient.print(F("POST "));
	_wifiClient.print(deviceInfo->actionPath);
	_wifiClient.println(F(" HTTP/1.1"));
	//_wifiClient.println(F("Connection: close"));
	_wifiClient.println(F("Content-Type: text/xml; charset=\"utf-8\""));
	_wifiClient.println("Host: " + ipAddressToString(deviceInfo->host) + ":" + String(deviceInfo->actionPort));
	//_wifiClient.println(F("Accept: */*"));
	//_wifiClient.println(F("Content-Type: application/x-www-form-urlencoded"));
	_wifiClient.print(F("SOAPAction: \""));
	_wifiClient.print(deviceInfo->serviceTypeName);
	_wifiClient.println(F("#AddPortMapping\""));

	_wifiClient.print(F("Content-Length: "));
	_wifiClient.println(integer_string);
	_wifiClient.println();

	_wifiClient.println(body_tmp);
	_wifiClient.println();
	
	debugPrint(F("Content-Length was: "));
	debugPrintln(integer_string);
	
	debugPrintln(body_tmp);
  
	timeout = millis();
	while (_wifiClient.available() == 0) {
		if (millis() - timeout > TCP_CONNECTION_TIMEOUT_MS) {
			debugPrintln(F("TCP connection timeout while adding a port mapping"));
			_wifiClient.stop();
			return false;
		}
	}

	// TODO: verify success
	boolean isSuccess = true;
	while (_wifiClient.available()) {
		String line = _wifiClient.readStringUntil('\r');
		if (line.indexOf(F("errorCode")) >= 0) {
			isSuccess = false;
		}
		debugPrintln(line);
	}
	debugPrintln("");  // \n
	
	if (!isSuccess) {
		_wifiClient.stop();
	}

	return isSuccess;
}

boolean TinyUPnP::printAllPortMappings() {
	debugPrintln(F("Port Mappings:"));
	
	// verify gateway information is valid
	// TODO: use this _gwInfo to skip the UDP part completely if it is not empty
	if (_gwInfo.host == IPAddress(0, 0, 0, 0) || _gwInfo.port == 0 || _gwInfo.path.length() == 0) {
		debugPrintln(F("Invalid router info, cannot continue"));
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
					debugPrint(F("Timeout expired while trying to connect to the IGD"));
					_wifiClient.stop();
					return false;
				}
				delay(1000);
			}
		}
		
		debugPrint(F("Sending query for index ["));
		debugPrint(String(index));
		debugPrintln(F("]"));

		strcpy_P(body_tmp, PSTR("<?xml version=\"1.0\"?>"
			"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
			"<s:Body>"
			"<u:GetGenericPortMappingEntry xmlns:u=\""));
		strcat_P(body_tmp, _gwInfo.serviceTypeName.c_str());
		strcat_P(body_tmp, PSTR("\">"
			"  <NewPortMappingIndex>"));

		sprintf(integer_string, "%d", index);
		strcat_P(body_tmp, integer_string);
		strcat_P(body_tmp, PSTR("</NewPortMappingIndex>"
			"</u:GetGenericPortMappingEntry>"
			"</s:Body>"
			"</s:Envelope>"));
		
		sprintf(integer_string, "%d", strlen(body_tmp));
		
		_wifiClient.print(F("POST "));
		_wifiClient.print(_gwInfo.actionPath);
		_wifiClient.println(F(" HTTP/1.1"));
		_wifiClient.println(F("Connection: keep-alive"));
		_wifiClient.println(F("Content-Type: text/xml; charset=\"utf-8\""));
		_wifiClient.println("Host: " + ipAddressToString(_gwInfo.host) + ":" + String(_gwInfo.actionPort));
		_wifiClient.print(F("SOAPAction: \""));
		_wifiClient.print(_gwInfo.serviceTypeName);
		_wifiClient.println(F("#GetGenericPortMappingEntry\""));

		_wifiClient.print(F("Content-Length: "));
		_wifiClient.println(integer_string);
		_wifiClient.println();

		_wifiClient.println(body_tmp);
		_wifiClient.println();
  
		unsigned long timeout = millis();
		while (_wifiClient.available() == 0) {
			if (millis() - timeout > TCP_CONNECTION_TIMEOUT_MS) {
				debugPrintln(F("TCP connection timeout while retrieving port mappings"));
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
				debugPrint(F("Invalid action while reading port mappings"));
				reachedEnd = true;
			} else if (line.indexOf(F("HTTP/1.1 500 ")) >= 0) {
				debugPrint(F("Internal server error, likely because we have shown all the mappings"));
				reachedEnd = true;
			} else if (line.indexOf(F("GetGenericPortMappingEntryResponse")) >= 0) {
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
	
	// print nicely and free heap memory
	upnpRuleNode *curr_ptr = ruleNodeHead_ptr;
	upnpRuleNode *del_prt = ruleNodeHead_ptr;
	while (curr_ptr != NULL) {
		upnpRuleToString(curr_ptr->rule_ptr);
		del_prt = curr_ptr;
		curr_ptr = curr_ptr->next_ptr;
		delete del_prt;
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
  int parts[4] = {0,0,0,0};
  int part = 0;
  for (int i = 0; i < ip.length(); i++)
  {
    char c = ip[i];
    if ( c == '.' )
    {
      part++;
      continue;
    }
    parts[part] *= 10;
    parts[part] += c - '0';
  }
  return IPAddress(parts[0], parts[1], parts[2], parts[3]);
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
  if (url.indexOf(F("https://")) != -1) {
    url.replace("https://", "");
  }
  if (url.indexOf(F("http://")) != -1) {
    url.replace("http://", "");
  }
  int endIndex = url.indexOf('/');
  if (endIndex != -1) {
    url = url.substring(0, endIndex);
  }
  int colonsIndex = url.indexOf(':');
  if (colonsIndex != -1) {
    url = url.substring(0, colonsIndex);
  }
  return ipToAddress(url);
}

int TinyUPnP::getPort(String url) {
  int port = -1;
  if (url.indexOf(F("https://")) != -1) {
    url.replace("https://", "");
  }
  if (url.indexOf(F("http://")) != -1) {
    url.replace("http://", "");
  }
  int portEndIndex = url.indexOf("/");
  if (portEndIndex == -1) {
	  portEndIndex = url.length();
  }
  url = url.substring(0, portEndIndex);
  int colonsIndex = url.indexOf(":");
  if (colonsIndex != -1) {
    url = url.substring(colonsIndex + 1, portEndIndex);
    port = url.toInt();
  } else {
    port = 80;
  }
  return port;
}

String TinyUPnP::getPath(String url) {
  int port = -1;
  if (url.indexOf(F("https://")) != -1) {
    url.replace("https://", "");
  }
  if (url.indexOf(F("http://")) != -1) {
    url.replace("http://", "");
  }
  int firstSlashIndex = url.indexOf("/");
  if (firstSlashIndex == -1) {
	debugPrintln("ERROR: Cannot find path in url [" + url + "]");
	return "";
  }
  return url.substring(firstSlashIndex, url.length());
}

String TinyUPnP::getTagContent(String line, String tagName) {
  int startIndex = line.indexOf("<" + tagName + ">");
  if (startIndex == -1) {
	debugPrint(F("ERROR: Cannot find tag content in line ["));
	debugPrint(line);
	debugPrint(F("] for start tag [<"));
	debugPrint(tagName);
	debugPrintln(F(">]"));
    return "";
  }
  startIndex += tagName.length() + 2;
  int endIndex = line.indexOf("</" + tagName + ">", startIndex);
  if (endIndex == -1) {
	debugPrint(F("ERROR: Cannot find tag content in line ["));
	debugPrint(line);
	debugPrint(F("] for end tag [</"));
	debugPrint(tagName);
	debugPrintln(F(">]"));
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
