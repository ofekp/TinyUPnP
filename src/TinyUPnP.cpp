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

// timeoutMs - timeout in milli seconds for the operations of this class, 0 for blocking operation.
TinyUPnP::TinyUPnP(unsigned long timeoutMs = 20000) {
	_timeoutMs = timeoutMs;
	_lastUpdateTime = 0;
	_consequtiveFails = 0;
	_headRuleNode = NULL;
	clearGatewayInfo(&_gwInfo);
}

TinyUPnP::~TinyUPnP() {
}

void TinyUPnP::setMappingConfig(IPAddress ruleIP, int rulePort, String ruleProtocol, int ruleLeaseDuration, String ruleFriendlyName) {
	static int index = 0;
	_upnpRule *new_rule_ptr = new _upnpRule();
	new_rule_ptr->index = index;
	new_rule_ptr->internalAddr = ruleIP;
	new_rule_ptr->internalPort = rulePort;
	new_rule_ptr->externalPort = rulePort;
	new_rule_ptr->leaseDuration = ruleLeaseDuration;
	new_rule_ptr->protocol = ruleProtocol;
	new_rule_ptr->devFriendlyName = ruleFriendlyName;
	// linked list insert

	_upnpRuleNode *newRuleNode = new _upnpRuleNode();
	newRuleNode->rule_ptr = new_rule_ptr;
	
	// newRuleNode->next_ptr = _headRuleNode;
	// _headRuleNode = newRuleNode;
	
	newRuleNode->next_ptr = NULL;
	if(_headRuleNode == NULL){
		_headRuleNode = newRuleNode;
	}else{
		_upnpRuleNode *cur = _headRuleNode;
		while(cur->next_ptr != NULL){
			cur = cur->next_ptr;
		}
		cur->next_ptr = newRuleNode;
	}
	index++;
}

upnpResult TinyUPnP::addPortMapping() {
	if(!_headRuleNode){
		UPNP_DEBUGln(F("ERROR: Invalid Mapping Rule, cannot continue"));
		return INVALID_PARAMETER;
	}
	unsigned long startTime = millis();

	// verify WiFi is connected
	upnpResult result = testConnectivity(startTime);
	if (result != UPNP_OK) {
		return result;
	}
	// get all the needed IGD information using SSDP if we don't have it already
	while(!isGatewayInfoValid(&_gwInfo)) {
		getGatewayInfo(&_gwInfo, startTime);
		if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
			UPNP_DEBUGln(F("ERROR: Invalid router info, cannot continue"));
			_wifiClient.stop();
			return INVALID_GATEWAY_INFO;
		}
		delay(1000);  // longer delay to allow more time for the router to update its rules
	}


	if (_gwInfo.port != _gwInfo.actionPort) {
		UPNP_DEBUG(F("port ["));
		UPNP_DEBUG(String(_gwInfo.port));
		UPNP_DEBUG(F("] actionPort ["));
		UPNP_DEBUG(String(_gwInfo.actionPort));
		UPNP_DEBUGln(F("]"));
		// in this case we need to connect to a different port
		UPNP_DEBUGln(F("Connection port changed, disconnecting from IGD"));
		_wifiClient.stop();
	}
	bool verify_complete = true;
	_upnpRuleNode *loop_ptr = _headRuleNode;
	UPNP_DEBUGln("Verify port mapping");
	while(loop_ptr != NULL){
		while (!verifyPortMapping(&_gwInfo, loop_ptr->rule_ptr)) {
			// add the port mapping
			UPNP_DEBUGln("Adding port mapping");
			addPortMappingEntry(&_gwInfo, loop_ptr->rule_ptr);
			if (_timeoutMs > 0 && (millis() - startTime > _timeoutMs)) {
				UPNP_DEBUGln(F("Timeout expired while trying to add the port mapping"));
				_wifiClient.stop();
				return PORT_MAPPING_TIMEOUT;
			}
			delay(1000);  // longer delay to allow more time for the router to update its rules
			verify_complete = false;
		}
		loop_ptr = loop_ptr->next_ptr;
	}
	_wifiClient.stop();
	if(verify_complete){
		UPNP_DEBUGln(F("Port mapping was already found in the IGD, not doing anything"));
		return ALREADY_MAPPED;
	}else{
		UPNP_DEBUGln(F("UPnP port mapping was added"));
		return SUCCESS;
	}
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
	UPNP_DEBUG(F("isGatewayInfoValid ?? ["));
	UPNP_DEBUG(deviceInfo->host.toString());
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
	UPNP_DEBUGln(F("Gateway info is valid"));
	return true;
}

upnpResult TinyUPnP::updatePortMapping(unsigned long intervalMs, callback_function fallback) {
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

			return PORT_MAPPING_TIMEOUT;
		}

		upnpResult result = addPortMapping();

		if (result == SUCCESS || result == ALREADY_MAPPED) {
			_lastUpdateTime = millis();
			_wifiClient.stop();
			_consequtiveFails = 0;
			return result;
		} else {
			_lastUpdateTime += intervalMs / 2;  // delay next try
			UPNP_DEBUGln(F("ERROR: While updating UPnP port mapping"));
			_wifiClient.stop();
			_consequtiveFails++;
			return result;
		}
	}

	_wifiClient.stop();
	return NOP;
}

upnpResult TinyUPnP::testConnectivity(unsigned long startTime) {
	//IPAddress gatewayIP = WiFi.gatewayIP();
	UPNP_DEBUG(F("Testing WiFi connection for ["));
	UPNP_DEBUG(WiFi.localIP().toString());
	UPNP_DEBUG("]");
	while (WiFi.status() != WL_CONNECTED) {
		if (_timeoutMs > 0 && startTime > 0 && (millis() - startTime > _timeoutMs)) {
			UPNP_DEBUG(F(" ==> Timeout expired while verifying WiFi connection"));
			_wifiClient.stop();
			return WIFI_TIMOUT;
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
			return TCP_TIMOUT;
		}
	}

	UPNP_DEBUGln(F(" ==> GOOD"));
	_wifiClient.stop();
	return UPNP_OK;
}

boolean TinyUPnP::verifyPortMapping(gatewayInfo *deviceInfo, _upnpRule *rule_ptr) {
	UPNP_DEBUGln(F("Verifying rule in IGD"));
	_upnpRule *result_ptr = new _upnpRule();
	result_ptr->externalPort = rule_ptr->externalPort;
	result_ptr->protocol = rule_ptr->protocol;
	upnpResult result = postSOAPAction(deviceInfo, result_ptr, GetSpecificPortMappingEntry);
	if (result != UPNP_OK){
		UPNP_DEBUG("Error:");
		UPNP_DEBUGln(String(result));
		delete result_ptr;
		return false;
	}
	if(result_ptr->internalAddr == rule_ptr->internalAddr){
		// Time lease extracted too
		UPNP_DEBUGln("Verifying OK");
		UPNP_DEBUG("Remaining Lease Duration : ");
		UPNP_DEBUGln(String(result_ptr->leaseDuration));		
		delete result_ptr;
		return true;
	}else if(result_ptr->internalAddr != rule_ptr->internalAddr && result_ptr->devFriendlyName == rule_ptr->devFriendlyName){
		UPNP_DEBUGln("ESP IP Changed but the UPnP is Not updated");
		result = postSOAPAction(deviceInfo, result_ptr, DeletePortMapping);
		if (result != UPNP_OK){
			UPNP_DEBUG("Error:");
			UPNP_DEBUGln(String(result));
		}else{
			UPNP_DEBUGln("UPnP Rules Deleted");
		}
	}
	delete result_ptr;
	return false;
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
	UPNP_DEBUG(ipMulti.toString());
	UPNP_DEBUG(F("] Port ["));
	UPNP_DEBUG(String(UPNP_SSDP_PORT));
	UPNP_DEBUGln(F("]"));

	_udpClient.beginPacketMulticast(ipMulti, UPNP_SSDP_PORT, WiFi.localIP());

	String packet = F("M-SEARCH * HTTP/1.1\r\n");
	packet += F("HOST: 239.255.255.250:1900\r\n");
	packet += F("MAN: \"ssdp:discover\"\r\n");
	packet += F("MX: 2\r\n");
	packet += "ST: " + String(INTERNET_GATEWAY_DEVICE) + "\r\n";	
	packet += F("USER-AGENT: NO-OS/2.4.0 UPnP/1.1 ESP8266/1.0\r\n\r\n");


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
	
	UPNP_DEBUGln(host.toString());
	UPNP_DEBUGln(String(port));
	UPNP_DEBUGln(path);

	return true;
}

// a single trial to connect to the IGD (with TCP)
boolean TinyUPnP::connectToIGD(IPAddress host, int port) {
	UPNP_DEBUG(F("Connecting to IGD with host ["));
	UPNP_DEBUG(host.toString());
	UPNP_DEBUG(F("] port ["));
	UPNP_DEBUG(String(port));
	UPNP_DEBUGln(F("]"));
	if (_wifiClient.connect(host, port)) {
		UPNP_DEBUGln(F("Connected to IGD"));
		return true;
	}
	return false;
}

upnpResult TinyUPnP::postSOAPAction(gatewayInfo *deviceInfo, _upnpRule *rule_ptr, e_SOAPActions SOAPAction){
	UPNP_DEBUGln(F("called post SOAPAction"));
	UPNP_DEBUG(F("SOAPAction ["));
	UPNP_DEBUG(SOAPActions[SOAPAction]);
	UPNP_DEBUGln(F("]"));

	if(!rule_ptr){
		return INVALID_MAPPING_RULE;
	}
	if(!deviceInfo){
		return INVALID_GATEWAY_INFO;
	}
	// connect to IGD (TCP connection) again, if needed, in case we got disconnected after the previous query
	unsigned long timeout = millis() + TCP_CONNECTION_TIMEOUT_MS;
	if (!_wifiClient.connected()) {
		while (!connectToIGD(deviceInfo->host, deviceInfo->actionPort)) {
			if (millis() > timeout) {
				UPNP_DEBUGln(F("Timeout expired while trying to connect to the IGD"));
				_wifiClient.stop();
				return TCP_TIMOUT;
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

	String request = F("<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n\t<s:Body>\r\n");
	request += F("\t\t<u:");
	request += SOAPActions[SOAPAction];
	request += F(" xmlns:u=\"");
	request += deviceInfo->serviceTypeName;
	request += F("\">\r\n");
	
	if (SOAPAction == GetGenericPortMappingEntry){
		request += "\t\t\t<NewPortMappingIndex>" + String(rule_ptr->index) + "</NewPortMappingIndex>\r\n";
	}else if(SOAPAction == GetSpecificPortMappingEntry || SOAPAction == DeletePortMapping || SOAPAction == AddPortMapping){
		request += F("\t\t\t<NewRemoteHost></NewRemoteHost>\r\n");
		request += "\t\t\t<NewExternalPort>" + String(rule_ptr->externalPort) + "</NewExternalPort>\r\n";
		request += "\t\t\t<NewProtocol>" + rule_ptr->protocol + "</NewProtocol>\r\n";
	}
	if (SOAPAction == AddPortMapping){
		request += "\t\t\t<NewInternalPort>" + String(rule_ptr->internalPort) + "</NewInternalPort>\r\n";
		request += "\t\t\t<NewInternalClient>" + rule_ptr->internalAddr.toString() + "</NewInternalClient>\r\n";
		request += F("\t\t\t<NewEnabled>1</NewEnabled>\r\n");
		request += "\t\t\t<NewPortMappingDescription>" + rule_ptr->devFriendlyName + "</NewPortMappingDescription>\r\n";
		request += "\t\t\t<NewLeaseDuration>" + String(rule_ptr->leaseDuration) + "</NewLeaseDuration>\r\n";
	}

	request += F("\t\t</u:");
	request += SOAPActions[SOAPAction];
	request += F(">\r\n\t</s:Body>\r\n</s:Envelope>\r\n");

	_wifiClient.print(F("POST "));
	_wifiClient.print(deviceInfo->actionPath);
	_wifiClient.println(F(" HTTP/1.1"));
	_wifiClient.println(F("Connection: keep-alive"));
	_wifiClient.println(F("Content-Type: text/xml; charset=\"utf-8\""));
	_wifiClient.println("Host: " + deviceInfo->host.toString() + ":" + String(deviceInfo->actionPort));
	_wifiClient.print(F("SOAPAction: \""));
	_wifiClient.print(deviceInfo->serviceTypeName);
	_wifiClient.println("#" + SOAPActions[SOAPAction] + "\"");

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
			UPNP_DEBUGln(F("TCP connection timeout"));
			_wifiClient.stop();
			return TCP_TIMOUT;
		}
	}
	upnpResult response = UNDEFINED_ERROR;
	int paramFlag = 0;
	String tagContent = "";

	while (_wifiClient.available()) {
		String line = _wifiClient.readStringUntil('\r');
		UPNP_DEBUG(line);
		if (line.indexOf(F("<errorCode>")) >= 0){
			response = (upnpResult)getTagContent(line, "errorCode").toInt();
		}else if (SOAPAction == GetGenericPortMappingEntry && response != UPNP_OK){
			if (line.indexOf(F("HTTP/1.1 500 ")) >= 0) {
				UPNP_DEBUG(F(" >>>> likely because we have shown all the mappings"));
				response = INVALID_INDEX;
				paramFlag |= 0x80;
			} else if( !(paramFlag & 0x01) && line.indexOf((SOAPActions[GetGenericPortMappingEntry]+String("Response")))  >= 0){
				paramFlag |= 0x01;	// Response OK
				UPNP_DEBUGln("Response recived");
			}
			if( !(paramFlag & 0x02) && (paramFlag & 0x01) ){	//Check for NewPortMappingDescription
				tagContent = getTagContent(line, "NewPortMappingDescription");
				if(tagContent != ""){
					rule_ptr->devFriendlyName = tagContent;
					tagContent = "";
					paramFlag |= 0x02;
					UPNP_DEBUGln("Found NewPortMappingDescription");
				}
			}
			if( !(paramFlag & 0x04) && (paramFlag & 0x01) ){	//Check for InternallIPAddress
				tagContent = getTagContent(line, "NewInternalClient");
				if(tagContent != ""){
					rule_ptr->internalAddr.fromString(tagContent);
					tagContent = "";
					paramFlag |= 0x04;
					UPNP_DEBUGln("Found NewInternalClient");
				}
			}
			if( !(paramFlag & 0x08) && (paramFlag & 0x01) ){	//Check for NewInternalPort
				tagContent =  getTagContent(line, "NewInternalPort");
				if(tagContent != ""){
					rule_ptr->internalPort = tagContent.toInt();
					tagContent = "";
					paramFlag |= 0x08;
					UPNP_DEBUGln("Found NewInternalPort");
				}
			}
			if( !(paramFlag & 0x10) && (paramFlag & 0x01) ){	//Check for NewExternalPort
				tagContent =  getTagContent(line, "NewExternalPort");
				if(tagContent != ""){
					rule_ptr->externalPort = tagContent.toInt();
					tagContent = "";
					paramFlag |= 0x10;
					UPNP_DEBUGln("Found NewExternalPort");
				}
			}
			if( !(paramFlag & 0x20) && (paramFlag & 0x01) ){	//Check for NewProtocol
				tagContent =  getTagContent(line, "NewProtocol");
				if(tagContent != ""){
					rule_ptr->protocol = tagContent;
					tagContent = "";
					paramFlag |= 0x20;
					UPNP_DEBUGln("Found NewProtocol");
				}
			}
			if( !(paramFlag & 0x40) && (paramFlag & 0x01) ){	//Check for NewLeaseDuration
				tagContent =  getTagContent(line, "NewLeaseDuration");
				if(tagContent != ""){
					rule_ptr->leaseDuration = tagContent.toInt();
					tagContent = "";
					paramFlag |= 0x40;
					UPNP_DEBUGln("Found NewLeaseDuration");
				}
			}
			if(paramFlag == 0x7F){
				UPNP_DEBUGln("Response OK");
				response = UPNP_OK;
			}

			/*if (line.indexOf((SOAPActions[GetGenericPortMappingEntry]+String("Response")))  >= 0) {
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
				response = UPNP_OK;
			}*/
		}else if (SOAPAction == AddPortMapping && response != UPNP_OK){
			if (line.indexOf((SOAPActions[AddPortMapping]+String("Response"))) >= 0){
				UPNP_DEBUGln("AddPortMappingResponse OK");
				response = UPNP_OK;
			}
		}else if (SOAPAction == GetSpecificPortMappingEntry && response != UPNP_OK){
			if (line.indexOf(F("HTTP/1.1 500 ")) >= 0) {
				UPNP_DEBUG(F(" >>>> likely because verification failed"));
				response = INVALID_INDEX;
				paramFlag |= 0x80;
			} else if ( !(paramFlag & 0x01) && line.indexOf((SOAPActions[GetSpecificPortMappingEntry]+String("Response"))) >= 0){
				paramFlag |= 0x01;	// Response OK
				UPNP_DEBUGln("Response recived");
			}
			if( !(paramFlag & 0x02) && (paramFlag & 0x01) ){	//Check for NewPortMappingDescription
				tagContent = getTagContent(line, "NewPortMappingDescription");
				if(tagContent != ""){
					rule_ptr->devFriendlyName = tagContent;
					tagContent = "";
					paramFlag |= 0x02;
					UPNP_DEBUGln("Found NewPortMappingDescription");
				}
			}
			if( !(paramFlag & 0x04) && (paramFlag & 0x01) ){	//Check for InternallIPAddress
				tagContent = getTagContent(line, "NewInternalClient");
				if(tagContent != ""){
					rule_ptr->internalAddr.fromString(tagContent);
					tagContent = "";
					paramFlag |= 0x04;
					UPNP_DEBUGln("Found NewInternalClient");
				}
			}
			if( !(paramFlag & 0x08) && (paramFlag & 0x01) ){	//Check for NewInternalPort
				tagContent =  getTagContent(line, "NewInternalPort");
				if(tagContent != ""){
					rule_ptr->internalPort = tagContent.toInt();
					tagContent = "";
					paramFlag |= 0x08;
					UPNP_DEBUGln("Found NewInternalPort");
				}
			}
			if( !(paramFlag & 0x10) && (paramFlag & 0x01) ){	//Check for NewLeaseDuration
				tagContent =  getTagContent(line, "NewLeaseDuration");
				if(tagContent != ""){
					rule_ptr->leaseDuration = tagContent.toInt();
					tagContent = "";
					paramFlag |= 0x10;
					UPNP_DEBUGln("Found NewLeaseDuration");
				}
			}
			if(paramFlag == 0x1F){
				UPNP_DEBUGln("Response OK");
				response = UPNP_OK;
			}
			/*if (line.indexOf((SOAPActions[GetSpecificPortMappingEntry]+String("Response"))) >= 0){
				rule_ptr->devFriendlyName = getTagContent(line, "NewPortMappingDescription");
				String newInternalClient = getTagContent(line, "NewInternalClient");
				rule_ptr->internalAddr.fromString(newInternalClient);
				rule_ptr->internalPort = getTagContent(line, "NewInternalPort").toInt();
				rule_ptr->leaseDuration = getTagContent(line, "NewLeaseDuration").toInt();
				response = UPNP_OK;
			}*/
		}else if (SOAPAction == DeletePortMapping && response != UPNP_OK){
			if (line.indexOf((SOAPActions[DeletePortMapping]+String("Response"))) >= 0){
				response = UPNP_OK;
			}
		}else if(SOAPAction == GetExternalIPAddress && response != UPNP_OK){
			if ( !(paramFlag & 0x01) && line.indexOf((SOAPActions[GetExternalIPAddress]+String("Response"))) >= 0){
				paramFlag |= 0x01;	// Response OK
				UPNP_DEBUGln("Response recived");
			}
			if( !(paramFlag & 0x02) && (paramFlag & 0x01) ){	//Check for NewExternalIPAddress
				tagContent = getTagContent(line, "NewExternalIPAddress");
				if(tagContent != ""){
					rule_ptr->internalAddr.fromString(tagContent);
					paramFlag |= 0x02;
					UPNP_DEBUGln("Found NewExternalIPAddress");
				}
			}
			if(paramFlag == 0x03){
				UPNP_DEBUGln("Response OK");
				response = UPNP_OK;
			}
			/*if (line.indexOf((SOAPActions[GetExternalIPAddress]+String("Response"))) >= 0){
				String NewExternalIPAddress = getTagContent(line, "NewExternalIPAddress");
				rule_ptr->internalAddr.fromString(NewExternalIPAddress);
				response = UPNP_OK;
			}*/
		}
		if (response == UPNP_OK || response == INVALID_INDEX || response == 714 || line.indexOf(F("</s:Envelope>")) >= 0){
			UPNP_DEBUG("\nEnd of Response or Flush due to Response:");
			UPNP_DEBUGln(response);
			while (_wifiClient.available()) {
				UPNP_DEBUG((char)_wifiClient.read());
			}
			break;
		}
	}
	return response;
}

IPAddress TinyUPnP::getExternalIP(){
	IPAddress myIP; 
	_upnpRule *result_ptr = new _upnpRule();
	upnpResult result = postSOAPAction(&_gwInfo, result_ptr, GetExternalIPAddress);
	if (result != UPNP_OK){
		UPNP_DEBUG("Error:");
		UPNP_DEBUGln(String(result));
		delete result_ptr;
		return false;
	}
	myIP = result_ptr->internalAddr;
	delete result_ptr;
	return myIP;
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
	_wifiClient.println("Host: " + deviceInfo->host.toString() + ":" + String(deviceInfo->actionPort));
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
	boolean urlBaseFound = true;		// don't look for it
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
				IPAddress host = getHost(baseUrl);  // this was previously ignored, assuming router host IP will not change
				int port = getPort(baseUrl);
				deviceInfo->actionPort = port;

				UPNP_DEBUG(F("URLBase tag found ["));
				UPNP_DEBUG(baseUrl);
				UPNP_DEBUGln(F("]"));
				UPNP_DEBUG(F("Translated to base host ["));
				UPNP_DEBUG(host.toString());
				UPNP_DEBUG(F("] and base port ["));
				UPNP_DEBUG(String(port));
				UPNP_DEBUGln(F("]"));
				if (deviceInfo->host == host){ // check for multiple routers
					urlBaseFound = true;
				}
			}
		}
		
		/*
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
		}*/
		if (!upnpServiceFound && urlBaseFound){
			int service_type_index = line.indexOf(UPNP_SERVICE_TYPE_TAG_START + UPNP_SERVICE_TYPE_1 + UPNP_SERVICE_TYPE_TAG_END);
			if(service_type_index >= 0){
				index_in_line += service_type_index;
				UPNP_DEBUGln(UPNP_SERVICE_TYPE_1 + " service found!");
				upnpServiceFound = true;
				deviceInfo->serviceTypeName = UPNP_SERVICE_TYPE_1;
				// will start looking for 'controlURL' now
			}else{
				service_type_index =line.indexOf(UPNP_SERVICE_TYPE_TAG_START + UPNP_SERVICE_TYPE_2 + UPNP_SERVICE_TYPE_TAG_END);
				if(service_type_index >= 0){
					index_in_line += service_type_index;
					UPNP_DEBUGln(UPNP_SERVICE_TYPE_2 + " service found!");
					upnpServiceFound = true;
					deviceInfo->serviceTypeName = UPNP_SERVICE_TYPE_2;
					// will start looking for 'controlURL' now
				}
			}
		}


		
		if (!controlURLFound && upnpServiceFound && (index_in_line = line.indexOf("<controlURL>", index_in_line)) >= 0) {
			String controlURLContent = getTagContent(line/*.substring(index_in_line)*/, "controlURL", index_in_line);
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

// will add the port mapping to the IGD
boolean TinyUPnP::addPortMappingEntry(gatewayInfo *deviceInfo, _upnpRule *rule_ptr) {
	UPNP_DEBUGln(F("called addPortMappingEntry"));
	upnpResult result = postSOAPAction(deviceInfo, rule_ptr, AddPortMapping);
	//_wifiClient.stop();
	if (result != UPNP_OK){
		UPNP_DEBUG("Error:");
		UPNP_DEBUGln(String(result));
		return false;
	}
	UPNP_DEBUGln("addPortMappingEntry OK");
	return true;
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
	
	boolean reachedEnd = false;
	int index = 0;
	upnpRule *result_ptr;
	while (!reachedEnd) {
			UPNP_DEBUG(F("Sending query for index ["));
			UPNP_DEBUG(String(index));
			UPNP_DEBUGln(F("]"));
			result_ptr = new upnpRule();
			result_ptr->index = index;
			upnpResult result = postSOAPAction(&_gwInfo,result_ptr,GetGenericPortMappingEntry);
			if (result != UPNP_OK){
				UPNP_DEBUG("Error:");
				UPNP_DEBUGln(String(result));
				UPNP_DEBUGln(F("End the Search"));
				delete result_ptr;
				_wifiClient.stop();
				reachedEnd = true;
				continue;
			}
			upnpRuleNode *currRuleNode_ptr = new upnpRuleNode();
			currRuleNode_ptr->rule_ptr = result_ptr;
			currRuleNode_ptr->next_ptr = NULL;
			if (ruleNodeHead_ptr == NULL) {
				ruleNodeHead_ptr = currRuleNode_ptr;
				ruleNodeTail_ptr = currRuleNode_ptr;
			} else {
				ruleNodeTail_ptr->next_ptr = currRuleNode_ptr;
				ruleNodeTail_ptr = currRuleNode_ptr;
			}
		index++;
		delay(100);
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
	
	return true;
}

void TinyUPnP::printAllRules(){
	upnpRuleNode *loop_ptr = _headRuleNode;
	while (loop_ptr != NULL) {
		upnpRuleToString(loop_ptr->rule_ptr);
		loop_ptr = loop_ptr->next_ptr;
	}
}

String TinyUPnP::upnpRuleToString(upnpRule *rule_ptr) {
	String upnpRule = String(rule_ptr->index);
	upnpRule += ".";
	upnpRule += getSpacesString(4 - upnpRule.length());

	upnpRule += rule_ptr->devFriendlyName;
	upnpRule += getSpacesString(35 - upnpRule.length());

	upnpRule += rule_ptr->internalAddr.toString();
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
/*
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
}*/
/*
char* TinyUPnP::ipAddressToCharArr(IPAddress ipAddress) {
	char s[17];
	sprintf(s, "%d.%d.%d.%d", ipAddress[0], ipAddress[1], ipAddress[2], ipAddress[3]);
	s[16] = '\0';
	return s;
}*/
/*
String TinyUPnP::ipAddressToString(IPAddress ipAddress) {
	return String(ipAddress[0]) + "." + String(ipAddress[1]) + "." + String(ipAddress[2]) + "." + String(ipAddress[3]);
}*/

IPAddress TinyUPnP::getHost(String url) {
	IPAddress result(0,0,0,0);
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
	result.fromString(url);
  return result;
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

String TinyUPnP::getTagContent(String &line, String tagName, int startIndex) {
  if (startIndex == -1) {
		startIndex = line.indexOf("<" + tagName + ">");
	}
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