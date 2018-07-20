/*
  TinyUPnP.h - Library for creating UPnP rules automatically in your router.
  Created by Ofek Pearl, September 2017.
  Released into the public domain.
*/

#ifndef TinyUPnP_h
#define TinyUPnP_h

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>

#define IS_DEBUG true
#define UPNP_SSDP_PORT 1900
#define TCP_CONNECTION_TIMEOUT_MS 6000
#define INTERNET_GATEWAY_DEVICE "urn:schemas-upnp-org:device:InternetGatewayDevice:1"
#define PORT_MAPPING_INVALID_INDEX "<errorDescription>SpecifiedArrayIndexInvalid</errorDescription>"
#define PORT_MAPPING_INVALID_ACTION "<errorDescription>Invalid Action</errorDescription>"

#define RULE_PROTOCOL_TCP "TCP"
#define RULE_PROTOCOL_UDP "UDP"

#define MAX_NUM_OF_UPDATES_WITH_NO_EFFECT 20  // after 10 tries of updatePortMapping we will execute the more extensive addPortMapping

const String UPNP_SERVICE_TYPE_1 = "urn:schemas-upnp-org:service:WANPPPConnection:1";
const String UPNP_SERVICE_TYPE_2 = "urn:schemas-upnp-org:service:WANIPConnection:1";
const String UPNP_SERVICE_TYPE_TAG_START = "<serviceType>";
const String UPNP_SERVICE_TYPE_TAG_END = "</serviceType>";

typedef struct _gatewayInfo {
	// router info
	IPAddress host;
	int port;  // this port is used when getting router capabilities and xml files
	String path;  // this is the path that is used to retrieve router information from xml files
	
	// info for actions
	int actionPort;  // this port is used when performing SOAP API actions
	String actionPath;  // this is the path used to perform SOAP API actions
	String serviceTypeName;  // i.e "WANPPPConnection:1" or "WANIPConnection:1"
} gatewayInfo;

typedef struct _upnpRule {
	int index;
	String devFriendlyName;
	IPAddress internalAddr;
	int internalPort;
	int externalPort;
	String protocol;
	int leaseDuration;
} upnpRule;

typedef struct _upnpRuleNode {
	_upnpRule *rule_ptr;
	_upnpRuleNode *next_ptr;
} upnpRuleNode;

class TinyUPnP
{
	public:
		TinyUPnP(int timeoutMs);
		~TinyUPnP();
		boolean addPortMapping();
		void setMappingConfig(IPAddress ruleIP, int rulePort, String ruleProtocol, int ruleLeaseDuration, String ruleFriendlyName);
		boolean updatePortMapping(unsigned long intervalMs);
		boolean printAllPortMappings();
		boolean verifyPortMapping(gatewayInfo *deviceInfo);
	private:
		boolean connectUDP();
		void broadcastMSearch();
		boolean waitForUnicastResponseToMSearch(gatewayInfo *deviceInfo, IPAddress gatewayIP);
		void getGatewayInfo(gatewayInfo *deviceInfo, long startTime);
		void clearGatewayInfo(gatewayInfo *deviceInfo);
		boolean isGatewayInfoValid(gatewayInfo *deviceInfo);
		boolean connectToIGD(IPAddress host, int port);
		boolean getIGDEventURLs(gatewayInfo *deviceInfo);
		boolean addPortMappingEntry(gatewayInfo *deviceInfo);
		boolean printAllRules(gatewayInfo *deviceInfo);
		IPAddress ipToAddress(String ip);
		char* ipAddressToCharArr(IPAddress ipAddress);  // ?? not sure this is needed
		String ipAddressToString(IPAddress ipAddress);
		String upnpRuleToString(upnpRule *rule_ptr);
		String getSpacesString(int num);
		IPAddress getHost(String url);
		int getPort(String url);
		String getPath(String url);
		String getTagContent(String line, String tagName);
		void debugPrint(String message);
		void debugPrintln(String message);

		/* members */
		IPAddress _ruleIP;
		int _rulePort;
		String _ruleProtocol;  // _ruleProtocol - either "TCP" or "UDP"
		int _ruleLeaseDuration;
		String _ruleFriendlyName;
		unsigned long _lastUpdateTime;
		int _timeoutMs;  // -1 for blocking operation
		WiFiUDP _udpClient;
		WiFiClient _wifiClient;
		gatewayInfo _gwInfo;
		unsigned long _numOfFallbackTimes;
};

#endif
