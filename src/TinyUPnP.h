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
#define UPNP_SERVICE_TYPE "<serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType>"
#define UPNP_SERVICE_TYPE_2 "<serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
#define PORT_MAPPING_INVALID_INDEX "<errorDescription>SpecifiedArrayIndexInvalid</errorDescription>"

#define RULE_PROTOCOL_TCP "TCP"
#define RULE_PROTOCOL_UDP "UDP"

typedef struct _gatewayInfo {
	IPAddress host;
	int port;
	String path;
	IPAddress baseUrlHost;
	String baseUrlPort;
	String addPortMappingEventUrl;
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
		boolean addPortMapping(IPAddress ruleIP, int rulePort, String ruleProtocol, int ruleLeaseDuration, String ruleFriendlyName);
		boolean printAllPortMappings();
	private:
		boolean connectUDP();
		void broadcastMSearch();
		boolean waitForUnicastResponseToMSearch(gatewayInfo *deviceInfo);
		boolean connectToIGD(gatewayInfo *deviceInfo);
		boolean getIGDEventURLs(gatewayInfo *deviceInfo);
		boolean addPortMappingEntry(IPAddress ruleIP, int rulePort, String ruleProtocol, int ruleLeaseDuration, String ruleFriendlyName, gatewayInfo *deviceInfo);
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
		int _timeoutMs;  // -1 for blocking operation
		WiFiUDP _udpClient;
		WiFiClient _wifiClient;
		gatewayInfo _gwInfo;
};

#endif
