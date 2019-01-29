/*
  TinyUPnP.h - Library for creating UPnP rules automatically in your router.
  Created by Ofek Pearl, September 2017.
*/

#include "Arduino.h"
#include "TinyUPnP.h"

#ifdef UPNP_DEBUG
#undef UPNP_DEBUG
#define UPNP_DEBUG(...) Serial.print( __VA_ARGS__ )
#define UPNP_DEBUGln(...) Serial.println( __VA_ARGS__ )
#else
#define UPNP_DEBUG(...)
#define UPNP_DEBUGln(...)
#endif

IPAddress ipMulti(239, 255, 255, 250);  // multicast address for SSDP
IPAddress connectivityTestIp(64, 233, 187, 99);  // Google



// timeoutMs - timeout in milli seconds for the operations of this class, -1 for blocking operation.
TinyUPnP::TinyUPnP(int timeoutMs = 20000) {
	_timeoutMs = timeoutMs;
	_lastUpdateTime = millis();
	_consequtiveFails = 0;
}

TinyUPnP::~TinyUPnP() {
	clearGatewayInfo(&_gwInfo);
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
		UPNP_DEBUGln(F("ERROR: Invalid router info, cannot continue"));
		_wifiClient.stop();
		return false;
	}
	
	UPNP_DEBUG(F("port ["));
	UPNP_DEBUG(String(_gwInfo.port));
	UPNP_DEBUG(F("] actionPort ["));
	UPNP_DEBUG(String(_gwInfo.actionPort));
	UPNP_DEBUGln(F("]"));
	if (_gwInfo.port != _gwInfo.actionPort) {
		// in this case we need to connect to a different port
		UPNP_DEBUGln(F("Connection port changed, disconnecting from IGD"));
		_wifiClient.stop();
	}
	
	// TODO: since verifyPortMapping connects to the IGD then addPortMappingEntry can skip it
	while (!verifyPortMapping(&_gwInfo)) {
		// add the port mapping
		addPortMappingEntry(&_gwInfo);
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			UPNP_DEBUGln(F("Timeout expired while trying to add the port mapping"));
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
			UPNP_DEBUG(F("Timeout expired while connecting UDP"));
			_udpClient.stop();
			return false;
		}
		delay(500);
		UPNP_DEBUG(".");
	}
	UPNP_DEBUGln("");  // \n
	
	broadcastMSearch();
	IPAddress gatewayIP = WiFi.gatewayIP();
	while (!waitForUnicastResponseToMSearch(deviceInfo, gatewayIP)) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			UPNP_DEBUGln(F("Timeout expired while waiting for the gateway router to respond to M-SEARCH message"));
			_udpClient.stop();
			return false;
		}
		delay(1);
	}

	// close the UDP connection
	_udpClient.stop();

	// connect to IGD (TCP connection)
	while (!connectToIGD(deviceInfo->host, deviceInfo->port)) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			UPNP_DEBUGln(F("Timeout expired while trying to connect to the IGD"));
			_wifiClient.stop();
			return false;
		}
		delay(500);
	}
	
	// get event urls from the gateway IGD
	while (!getIGDEventURLs(deviceInfo)) {
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			UPNP_DEBUGln(F("Timeout expired while adding a new port mapping"));
			_wifiClient.stop();
			return false;
		}
		delay(500);
	}

	return true;
}

void TinyUPnP::clearGatewayInfo(gatewayInfo *deviceInfo) {
	deviceInfo->host = IPAddress(0, 0, 0, 0);
	deviceInfo->port = 0;
	deviceInfo->path = "";
	deviceInfo->actionPort = 0;
	deviceInfo->actionPath = "";
	deviceInfo->serviceTypeName = "";
}

boolean TinyUPnP::isGatewayInfoValid(gatewayInfo *deviceInfo) {
	UPNP_DEBUG(F("\n\n\nisGatewayInfoValid ["));
	UPNP_DEBUG(ipAddressToString(deviceInfo->host));
	UPNP_DEBUG(F("] port ["));
	UPNP_DEBUG(String(deviceInfo->port));
	UPNP_DEBUG(F("] path ["));
	UPNP_DEBUG(deviceInfo->path);
	UPNP_DEBUG(F("] actionPort ["));
	UPNP_DEBUG(String(deviceInfo->actionPort));
	UPNP_DEBUG(F("] actionPath ["));
	UPNP_DEBUG(deviceInfo->actionPath);
	UPNP_DEBUG(F("] serviceTypeName ["));
	UPNP_DEBUG(deviceInfo->serviceTypeName);
	UPNP_DEBUGln(F("]"));

	if (deviceInfo->host == IPAddress(0, 0, 0, 0)
		|| deviceInfo->port == 0
		|| deviceInfo->path.length() == 0
		|| deviceInfo->actionPort == 0) {
		UPNP_DEBUGln(F("Gateway info is not valid"));
		return false;
	}
	return true;
}

UpdateState TinyUPnP::updatePortMapping(unsigned long intervalMs, callback_function fallback) {
    if (millis() - _lastUpdateTime >= intervalMs) {
		UPNP_DEBUGln(F("Updating port mapping"));

		// fallback
		if (_consequtiveFails >= MAX_NUM_OF_UPDATES_WITH_NO_EFFECT) {
			UPNP_DEBUG(F("ERROR: Too many times with no effect on updatePortMapping. Current number of fallbacks times ["));
			UPNP_DEBUG(String(_consequtiveFails));
			UPNP_DEBUGln(F("]"));

			_consequtiveFails = 0;
			clearGatewayInfo(&_gwInfo);
			if (fallback != NULL) {
				UPNP_DEBUGln(F("Executing fallback method"));
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
			UPNP_DEBUGln(F("Port mapping was already found in the IGD, not doing anything"));
			_lastUpdateTime = millis();
			_wifiClient.stop();
			_consequtiveFails = 0;
			return ALREADY_MAPPED;
		}
		
		UPNP_DEBUGln("Adding port mapping");
		if (addPortMapping()) {
			_lastUpdateTime = millis();
			UPNP_DEBUGln(F("UPnP port mapping was added"));
			_wifiClient.stop();
			_consequtiveFails = 0;
			return SUCCESS;
		} else {
			_lastUpdateTime += intervalMs / 2;  // delay next try
			UPNP_DEBUGln(F("ERROR: While updating UPnP port mapping"));
			_wifiClient.stop();
			_consequtiveFails++;
			return ERROR;
		}
	}

	_wifiClient.stop();
	return NOP;
}

boolean TinyUPnP::testConnectivity(unsigned long startTime) {
	//IPAddress gatewayIP = WiFi.gatewayIP();
	UPNP_DEBUG(F("Testing WiFi connection for ["));
	UPNP_DEBUG(ipAddressToString(WiFi.localIP()));
	UPNP_DEBUG("]");
	while (WiFi.status() != WL_CONNECTED) {
		if (_timeoutMs > 0 && startTime > 0 && (millis() - startTime > _timeoutMs)) {
			UPNP_DEBUG(F(" ==> Timeout expired while verifying WiFi connection"));
			_wifiClient.stop();
			return false;
		}
		delay(200);
		UPNP_DEBUG(".");
	}
	UPNP_DEBUGln(" ==> GOOD");  // \n

	UPNP_DEBUG(F("Testing internet connection"));
	_wifiClient.connect(connectivityTestIp, 80);
	while (!_wifiClient.connected()) {
		if (startTime + TCP_CONNECTION_TIMEOUT_MS > millis()) {
			UPNP_DEBUGln(F(" ==> BAD"));
			_wifiClient.stop();
			return false;
		}
	}

	UPNP_DEBUGln(F(" ==> GOOD"));
	_wifiClient.stop();
	return true;
}

boolean TinyUPnP::verifyPortMapping(gatewayInfo *deviceInfo) {
	UPNP_DEBUGln(F("Verifying rule in IGD"));

	// connect to IGD (TCP connection) again, if needed, in case we got disconnected after the previous query
	unsigned long timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
	if (!_wifiClient.connected()) {
		while (!connectToIGD(_gwInfo.host, _gwInfo.actionPort)) {
			if (millis() > timeout) {
				UPNP_DEBUGln(F("Timeout expired while trying to connect to the IGD"));
				_wifiClient.stop();
				return false;
			}
			delay(500);
		}
	}
	String request = F("<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n<s:Body>\r\n<u:GetSpecificPortMappingEntry xmlns:u=\"urn:schemas-upnp-org:service:WANPPPConnection:1\">\r\n<NewRemoteHost></NewRemoteHost>\r\n<NewExternalPort>");
	request += String(_rulePort);
	request += F("</NewExternalPort>\r\n<NewProtocol>");
	request += _ruleProtocol;
	request += F("</NewProtocol>\r\n</u:GetSpecificPortMappingEntry>\r\n</s:Body>\r\n</s:Envelope>\r\n");


	_wifiClient.print(F("POST "));

	_wifiClient.print(deviceInfo->actionPath);
	_wifiClient.println(F(" HTTP/1.1"));
	_wifiClient.println(F("Connection: close"));
	_wifiClient.println(F("Content-Type: text/xml; charset=\"utf-8\""));
	_wifiClient.println("Host: " + ipAddressToString(deviceInfo->host) + ":" + String(deviceInfo->actionPort));
	_wifiClient.print(F("SOAPAction: \""));
	_wifiClient.print(_gwInfo.serviceTypeName);
	_wifiClient.println(F("#GetSpecificPortMappingEntry\""));
	_wifiClient.print(F("Content-Length: "));
	_wifiClient.println(request.length());
	_wifiClient.println();

	_wifiClient.println(request);
	_wifiClient.println();
	
	UPNP_DEBUG(F("Content-Length was: "));
	UPNP_DEBUGln(String(request.length()));
	UPNP_DEBUG(F("request: "));
	UPNP_DEBUGln(request);
	request = "";

	timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
	while (_wifiClient.available() == 0) {
		if (millis() > timeout) {
			UPNP_DEBUGln(F("TCP connection timeout while retrieving port mappings"));
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
		UPNP_DEBUG(line);
	}
	UPNP_DEBUGln("");  // \n

	_wifiClient.stop();
	
	if (isSuccess) {
		UPNP_DEBUGln(F("Port mapping found in IGD"));
	} else {
		UPNP_DEBUGln(F("Could not find port mapping in IGD"));
	}
	
	return isSuccess;
}
// a single try to connect UDP multicast address and port of UPnP (239.255.255.250 and 1900 respectively)
// this will enable receiving SSDP packets after the M-SEARCH multicast message will be broadcasted
boolean TinyUPnP::connectUDP() {
	if (_udpClient.beginMulticast(WiFi.localIP(), ipMulti, UPNP_SSDP_PORT)) {
		return true;
	}
	UPNP_DEBUGln(F("UDP connection failed"));
	return false;
}

// broadcast an M-SEARCH message to initiate messages from SSDP devices
// the router should respond to this message by a packet sent to this device's unicast addresss on the
// same UPnP port (1900)
void TinyUPnP::broadcastMSearch() {
	UPNP_DEBUG(F("Sending M-SEARCH to ["));
	UPNP_DEBUG(ipAddressToString(ipMulti));
	UPNP_DEBUG(F("] Port ["));
	UPNP_DEBUG(String(UPNP_SSDP_PORT));
	UPNP_DEBUGln(F("]"));

	_udpClient.beginPacketMulticast(ipMulti, UPNP_SSDP_PORT, WiFi.localIP());

	String packet = F("M-SEARCH * HTTP/1.1\r\n");
	packet += F("HOST: 239.255.255.250:1900\r\n");
	packet += F("MAN: \"ssdp:discover\"\r\n");
	packet += F("MX: 5\r\n");
	packet += "ST: " + String(INTERNET_GATEWAY_DEVICE) + "\r\n\r\n";


	_udpClient.print(packet);
	_udpClient.endPacket();

	UPNP_DEBUGln(F("M-SEARCH sent"));
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

	UPNP_DEBUG(F("Received packet of size ["));
	UPNP_DEBUG(String(packetSize));
	UPNP_DEBUG(F("]"));
	UPNP_DEBUG(F(" ip ["));
	for (int i = 0; i < 4; i++) {
		UPNP_DEBUG(String(remoteIP[i]));  // Decimal
		if (i < 3) {
			UPNP_DEBUG(F("."));
		}
	}
	UPNP_DEBUG(F("] port ["));
	UPNP_DEBUG(String(_udpClient.remotePort()));
	UPNP_DEBUGln(F("]"));

	String packetBuffer = "reserve";
	packetBuffer.reserve(packetSize);
	// sanity check
	if (!packetBuffer) {
		UPNP_DEBUG(F("Received packet with size larged than the response buffer, cannot proceed."));
		return false;
	}
  
	packetBuffer = _udpClient.readString();
	UPNP_DEBUG(F("UDP packet read bytes ["));
	UPNP_DEBUG(String(packetSize));
	UPNP_DEBUGln(F("]"));

	UPNP_DEBUGln(F("Gateway packet content (many variations for debug):"));
	UPNP_DEBUGln(F("packetBuffer:"));
	UPNP_DEBUGln(packetBuffer);

	// only continue if the packet is a response to M-SEARCH and it originated from a gateway device
	if (packetBuffer.indexOf(INTERNET_GATEWAY_DEVICE) < 0) {
		UPNP_DEBUGln(F("INTERNET_GATEWAY_DEVICE was not found"));
		return false;
	}

	UPNP_DEBUGln(F("INTERNET_GATEWAY_DEVICE found"));

	String location = "";
	int location_indexStart = packetBuffer.indexOf("location:");
	if (location_indexStart < 0) {
		location_indexStart = packetBuffer.indexOf("Location:");
	}
	if (location_indexStart < 0) {
		location_indexStart = packetBuffer.indexOf("LOCATION:");
	}
	if (location_indexStart > 0) {
		location_indexStart += 9;  // "location:".length()
		int location_indexEnd = packetBuffer.indexOf("\r\n",location_indexStart);
		if (location_indexEnd > location_indexStart) {
			location = packetBuffer.substring(location_indexStart,location_indexEnd);
			location.trim();
		} else {
			UPNP_DEBUGln(F("ERROR: LOCATION param was not found"));
			return false;
		}
	} else {
		
		return false;
	}
	
	UPNP_DEBUG(F("IGD location found ["));
	UPNP_DEBUG(location);
	UPNP_DEBUGln(F("]"));
  
	IPAddress host = getHost(location);
	int port = getPort(location);
	String path = getPath(location);
	
	deviceInfo->host = host;
	deviceInfo->port = port;
	deviceInfo->path = path;
	// the following is the default and may be overridden if URLBase tag is specified
	deviceInfo->actionPort = port;
	
	UPNP_DEBUGln(ipAddressToString(host));
	UPNP_DEBUGln(String(port));
	UPNP_DEBUGln(path);

	return true;
}

// a single trial to connect to the IGD (with TCP)
boolean TinyUPnP::connectToIGD(IPAddress host, int port) {
	UPNP_DEBUG(F("Connecting to IGD with host ["));
	UPNP_DEBUG(ipAddressToString(host));
	UPNP_DEBUG(F("] port ["));
	UPNP_DEBUG(String(port));
	UPNP_DEBUGln(F("]"));
	if (_wifiClient.connect(host, port)) {
		UPNP_DEBUGln(F("Connected to IGD"));
		return true;
	}
	return false;
}

// updates deviceInfo with the commands' information of the IGD
boolean TinyUPnP::getIGDEventURLs(gatewayInfo *deviceInfo) {
	UPNP_DEBUGln("called getIGDEventURLs");
	UPNP_DEBUG(F("deviceInfo->actionPath ["));
	UPNP_DEBUG(deviceInfo->actionPath);
	UPNP_DEBUG(F("] deviceInfo->path ["));
	UPNP_DEBUG(deviceInfo->path);
	UPNP_DEBUGln(F("]"));

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
			UPNP_DEBUGln(F("TCP connection timeout while executing getIGDEventURLs"));
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
		UPNP_DEBUG(line);
		if (!urlBaseFound && line.indexOf(F("<URLBase>")) >= 0) {
			// e.g. <URLBase>http://192.168.1.1:5432/</URLBase>
			// Note: assuming URL path will only be found in a specific action under the 'controlURL' xml tag
			String baseUrl = getTagContent(line, "URLBase");
			if (baseUrl.length() > 0) {
				baseUrl.trim();
				IPAddress host = getHost(baseUrl);  // this is ignored, assuming router host IP will not change
				int port = getPort(baseUrl);
				deviceInfo->actionPort = port;

				UPNP_DEBUG(F("URLBase tag found ["));
				UPNP_DEBUG(baseUrl);
				UPNP_DEBUGln(F("]"));
				UPNP_DEBUG(F("Translated to base host ["));
				UPNP_DEBUG(ipAddressToString(host));
				UPNP_DEBUG(F("] and base port ["));
				UPNP_DEBUG(String(port));
				UPNP_DEBUGln(F("]"));
				if (deviceInfo->host == host){
					urlBaseFound = true;
				}
			}
		}
		
		
		int service_type_1_index = line.indexOf(UPNP_SERVICE_TYPE_TAG_START + UPNP_SERVICE_TYPE_1 + UPNP_SERVICE_TYPE_TAG_END);
		int service_type_2_index = line.indexOf(UPNP_SERVICE_TYPE_TAG_START + UPNP_SERVICE_TYPE_2 + UPNP_SERVICE_TYPE_TAG_END);

		if (!upnpServiceFound && urlBaseFound && service_type_1_index >= 0) {
			index_in_line += service_type_1_index;
			UPNP_DEBUGln(UPNP_SERVICE_TYPE_1 + " service found!");
			upnpServiceFound = true;
			deviceInfo->serviceTypeName = UPNP_SERVICE_TYPE_1;
			// will start looking for 'controlURL' now
		} else if (!upnpServiceFound && urlBaseFound && service_type_2_index >= 0) {
			index_in_line += service_type_2_index;
			UPNP_DEBUGln(UPNP_SERVICE_TYPE_2 + " service found!");
			upnpServiceFound = true;
			deviceInfo->serviceTypeName = UPNP_SERVICE_TYPE_2;
			// will start looking for 'controlURL' now
		}
		
		if (!controlURLFound && upnpServiceFound && (index_in_line = line.indexOf("<controlURL>", index_in_line)) >= 0) {
			String controlURLContent = getTagContent(line.substring(index_in_line), "controlURL");
			if (controlURLContent.length() > 0) {
				deviceInfo->actionPath = controlURLContent;
				controlURLFound = true;

				UPNP_DEBUG(F("controlURL tag found! setting actionPath to ["));
				UPNP_DEBUG(controlURLContent);
				UPNP_DEBUGln(F("]"));
			}
		}

		if(urlBaseFound && upnpServiceFound && controlURLFound){
			// clear buffer
			UPNP_DEBUGln(F("Flushing the rest of the response"));
			while (_wifiClient.available()) {
				_wifiClient.read();
			}
			return true;
		}
	}
	return false;
}

// assuming a connection to the IGD has been formed
// will add the port mapping to the IGD
boolean TinyUPnP::addPortMappingEntry(gatewayInfo *deviceInfo) {
	UPNP_DEBUGln(F("called addPortMappingEntry"));

	// connect to IGD (TCP connection) again, if needed, in case we got disconnected after the previous query
	unsigned long timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
	if (!_wifiClient.connected()) {
		while (!connectToIGD(_gwInfo.host, _gwInfo.actionPort)) {
			if (millis() > timeout) {
				UPNP_DEBUGln(F("Timeout expired while trying to connect to the IGD"));
				_wifiClient.stop();
				return false;
			}
			delay(500);
		}
	}

	UPNP_DEBUG(F("deviceInfo->actionPath ["));
	UPNP_DEBUG(deviceInfo->actionPath);
	UPNP_DEBUGln(F("]"));

	UPNP_DEBUG(F("deviceInfo->serviceTypeName ["));
	UPNP_DEBUG(deviceInfo->serviceTypeName);
	UPNP_DEBUGln(F("]"));
	
	String request = F("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:AddPortMapping xmlns:u=\"");
	request += deviceInfo->serviceTypeName;
	request += F("\"><NewRemoteHost></NewRemoteHost><NewExternalPort>");
	request += String(_rulePort);
	request += F("</NewExternalPort><NewProtocol>");
	request += _ruleProtocol;
	request += F("</NewProtocol><NewInternalPort>");
	request += String(_rulePort);
	request += F("</NewInternalPort><NewInternalClient>");
	request += ipAddressToString(_ruleIP);
	request += F("</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>");
	request += _ruleFriendlyName;
	request += F("</NewPortMappingDescription><NewLeaseDuration>");
	request += String(_ruleLeaseDuration);
	request += F("</NewLeaseDuration></u:AddPortMapping></s:Body></s:Envelope>");

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
	_wifiClient.println(request.length());
	_wifiClient.println();

	_wifiClient.println(request);
	_wifiClient.println();
	
	UPNP_DEBUG(F("Content-Length was: "));
	UPNP_DEBUGln(String(request.length()));
	UPNP_DEBUG(F("request: "));
	UPNP_DEBUGln(request);
	request = "";
  
	timeout = millis();
	while (_wifiClient.available() == 0) {
		if (millis() - timeout > TCP_CONNECTION_TIMEOUT_MS) {
			UPNP_DEBUGln(F("TCP connection timeout while adding a port mapping"));
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
		UPNP_DEBUG(line);
	}
	UPNP_DEBUGln("");  // \n
	
	if (!isSuccess) {
		_wifiClient.stop();
	}

	return isSuccess;
}

boolean TinyUPnP::printAllPortMappings() {
	UPNP_DEBUGln(F("Port Mappings:"));
	
	// verify gateway information is valid
	// TODO: use this _gwInfo to skip the UDP part completely if it is not empty
	if (_gwInfo.host == IPAddress(0, 0, 0, 0) || _gwInfo.port == 0 || _gwInfo.path.length() == 0) {
		UPNP_DEBUGln(F("Invalid router info, cannot continue"));
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
					UPNP_DEBUG(F("Timeout expired while trying to connect to the IGD"));
					_wifiClient.stop();
					return false;
				}
				delay(1000);
			}
		}

		UPNP_DEBUG(F("Sending query for index ["));
		UPNP_DEBUG(String(index));
		UPNP_DEBUGln(F("]"));

		String request = F("<?xml version=\"1.0\"?>"
			"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
			"<s:Body>"
			"<u:GetGenericPortMappingEntry xmlns:u=\"");
		request += _gwInfo.serviceTypeName;
		request += F("\">"
			"  <NewPortMappingIndex>");

		request += String(index);
		request += F("</NewPortMappingIndex>"
			"</u:GetGenericPortMappingEntry>"
			"</s:Body>"
			"</s:Envelope>");

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
		_wifiClient.println(request.length());
		_wifiClient.println();

		_wifiClient.println(request);
		_wifiClient.println();

		UPNP_DEBUG(F("Content-Length was: "));
		UPNP_DEBUGln(String(request.length()));
		UPNP_DEBUG(F("request: "));
		UPNP_DEBUGln(request);
		request = "";

		unsigned long timeout = millis();
		while (_wifiClient.available() == 0) {
			if (millis() - timeout > TCP_CONNECTION_TIMEOUT_MS) {
				UPNP_DEBUGln(F("TCP connection timeout while retrieving port mappings"));
				_wifiClient.stop();
				return false;
			}
		}

		while (_wifiClient.available()) {
			String line = _wifiClient.readStringUntil('\r');
			UPNP_DEBUG(line);
			if (line.indexOf(PORT_MAPPING_INVALID_INDEX) >= 0) {
				reachedEnd = true;
			} else if (line.indexOf(PORT_MAPPING_INVALID_ACTION) >= 0) {
				UPNP_DEBUG(F("Invalid action while reading port mappings"));
				reachedEnd = true;
			} else if (line.indexOf(F("HTTP/1.1 500 ")) >= 0) {
				UPNP_DEBUG(F("Internal server error, likely because we have shown all the mappings"));
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
		delete del_prt->rule_ptr;
		delete del_prt;
	}
	
	_wifiClient.stop();
	
	return true;
}

String TinyUPnP::upnpRuleToString(upnpRule *rule_ptr) {
	String upnpRule = String(rule_ptr->index);
	upnpRule += ".";
	upnpRule += getSpacesString(4 - upnpRule.length());

	upnpRule += rule_ptr->devFriendlyName;
	upnpRule += getSpacesString(35 - upnpRule.length());

	upnpRule += ipAddressToString(rule_ptr->internalAddr);
	upnpRule += getSpacesString(53 - upnpRule.length());
	
	upnpRule += String(rule_ptr->internalPort);
	upnpRule += getSpacesString(60 - upnpRule.length());

	upnpRule += String(rule_ptr->externalPort);
	upnpRule += getSpacesString(67 - upnpRule.length());

	upnpRule += rule_ptr->protocol;
	upnpRule += getSpacesString(73 - upnpRule.length());

	upnpRule += String(rule_ptr->leaseDuration);
	upnpRule += getSpacesString(80 - upnpRule.length());
	
	Serial.println(upnpRule);
	return upnpRule;
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
  for (unsigned int i = 0; i < ip.length(); i++)
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
/*
char* TinyUPnP::ipAddressToCharArr(IPAddress ipAddress) {
	char s[17];
	sprintf(s, "%d.%d.%d.%d", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
	s[16] = '\0';
	return s;
}*/

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
  //int port = -1;
  if (url.indexOf(F("https://")) != -1) {
    url.replace("https://", "");
  }
  if (url.indexOf(F("http://")) != -1) {
    url.replace("http://", "");
  }
  int firstSlashIndex = url.indexOf("/");
  if (firstSlashIndex == -1) {
	UPNP_DEBUGln("ERROR: Cannot find path in url [" + url + "]");
	return "";
  }
  return url.substring(firstSlashIndex, url.length());
}

String TinyUPnP::getTagContent(String line, String tagName) {
  int startIndex = line.indexOf("<" + tagName + ">");
  if (startIndex == -1) {
	UPNP_DEBUG(F("ERROR: Cannot find tag content in line ["));
	UPNP_DEBUG(line);
	UPNP_DEBUG(F("] for start tag [<"));
	UPNP_DEBUG(tagName);
	UPNP_DEBUGln(F(">]"));
    return "";
  }
  startIndex += tagName.length() + 2;
  int endIndex = line.indexOf("</" + tagName + ">", startIndex);
  if (endIndex == -1) {
	UPNP_DEBUG(F("ERROR: Cannot find tag content in line ["));
	UPNP_DEBUG(line);
	UPNP_DEBUG(F("] for end tag [</"));
	UPNP_DEBUG(tagName);
	UPNP_DEBUGln(F(">]"));
    return "";
  }
  return line.substring(startIndex, endIndex);
}
/*
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
*/