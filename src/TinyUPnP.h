/*
 * TinyUPnP.h - Library for creating UPnP rules automatically in your router.
 * Created by Ofek Pearl, September 2017.
 * Released into the public domain.
*/

#ifndef TinyUPnP_h
#define TinyUPnP_h

#include <Arduino.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <limits.h>

#define UPNP_DEBUG
#define UPNP_SSDP_PORT 1900
#define TCP_CONNECTION_TIMEOUT_MS 6000
#define PORT_MAPPING_INVALID_INDEX "<errorDescription>SpecifiedArrayIndexInvalid</errorDescription>"
#define PORT_MAPPING_INVALID_ACTION "<errorDescription>Invalid Action</errorDescription>"

static const char * const deviceListUpnp[] = {
    "urn:schemas-upnp-org:service:WANIPConnection:1",
    "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
    "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
    "urn:schemas-upnp-org:service:WANIPConnection:2",
    "urn:schemas-upnp-org:service:WANPPPConnection:1",
    // "upnp:rootdevice",
    0
};

static const char * const deviceListSsdpAll[] = {
    "ssdp:all",
    0
};

#define RULE_PROTOCOL_TCP "TCP"
#define RULE_PROTOCOL_UDP "UDP"

#define MAX_NUM_OF_UPDATES_WITH_NO_EFFECT 6  // after 6 tries of updatePortMappings we will execute the more extensive addPortMapping

#define UDP_TX_PACKET_MAX_SIZE 1000  // reduce max UDP packet size to conserve memory (by default UDP_TX_PACKET_MAX_SIZE=8192)
#define UDP_TX_RESPONSE_MAX_SIZE 8192

const String UPNP_SERVICE_TYPE_TAG_NAME = "serviceType";
const String UPNP_SERVICE_TYPE_TAG_START = "<serviceType>";
const String UPNP_SERVICE_TYPE_TAG_END = "</serviceType>";

// TODO: idealy the SOAP actions should be verified as supported by the IGD before they are used
// 		 a struct can be created for each action and filled when the XML descriptor file is read
/*const String SOAPActions [] = {
    "AddPortMapping",
    "GetSpecificPortMappingEntry",
    "DeletePortMapping",
    "GetGenericPortMappingEntry",
    "GetExternalIPAddress"
};*/

/*
#define SOAP_ERROR_TAG "errorDescription";
const String SOAPErrors [] = {
    "SpecifiedArrayIndexInvalid",
    "Invalid Action"
};*/

/*
enum soapActionResult {
// TODO
}*/

typedef struct _SOAPAction {
    const char *name;
} SOAPAction;

typedef void (*callback_function)(void);

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
    _upnpRule *upnpRule;
    _upnpRuleNode *next;
} upnpRuleNode;

typedef struct _ssdpDevice {
    IPAddress host;
    int port;  // this port is used when getting router capabilities and xml files
    String path;  // this is the path that is used to retrieve router information from xml files
} ssdpDevice;

typedef struct _ssdpDeviceNode {
    _ssdpDevice *ssdpDevice;
    _ssdpDeviceNode *next;
} ssdpDeviceNode;

enum portMappingResult {
    SUCCESS,  // port mapping was added
    ALREADY_MAPPED,  // the port mapping is already found in the IGD
    EMPTY_PORT_MAPPING_CONFIG,
    NETWORK_ERROR,
    TIMEOUT,
    VERIFICATION_FAILED,
    NOP  // the check is delayed
};

class TinyUPnP
{
    public:
        TinyUPnP(unsigned long timeoutMs);
        ~TinyUPnP();
        // when the ruleIP is set to the current device IP, the IP of the rule will change if the device changes its IP
        // this makes sure the traffic will be directed to the device even if the IP chnages
        void addPortMappingConfig(IPAddress ruleIP /* can be NULL */, int rulePort, String ruleProtocol, int ruleLeaseDuration, String ruleFriendlyName);
        portMappingResult commitPortMappings();
        portMappingResult updatePortMappings(unsigned long intervalMs, callback_function fallback = NULL /* optional */);
        boolean printAllPortMappings();
        void printPortMappingConfig();  // prints all the port mappings that were added using `addPortMappingConfig`
        boolean testConnectivity(unsigned long startTime = 0);
        /* API extensions - additional methods to the UPnP API */
        ssdpDeviceNode* listSsdpDevices();  // will create an object with all SSDP devices on the network
        void printSsdpDevices(ssdpDeviceNode* ssdpDeviceNode);  // will print all SSDP devices in teh list
    private:
        boolean connectUDP();
        void broadcastMSearch(bool isSsdpAll = false);
        ssdpDevice* waitForUnicastResponseToMSearch(IPAddress gatewayIP);
        boolean getGatewayInfo(gatewayInfo *deviceInfo, long startTime);
        boolean isGatewayInfoValid(gatewayInfo *deviceInfo);
        void clearGatewayInfo(gatewayInfo *deviceInfo);
        boolean connectToIGD(IPAddress host, int port);
        boolean getIGDEventURLs(gatewayInfo *deviceInfo);
        boolean addPortMappingEntry(gatewayInfo *deviceInfo, upnpRule *rule_ptr);
        boolean verifyPortMapping(gatewayInfo *deviceInfo, upnpRule *rule_ptr);
        boolean deletePortMapping(gatewayInfo *deviceInfo, upnpRule *rule_ptr);
        boolean applyActionOnSpecificPortMapping(SOAPAction *soapAction, gatewayInfo *deviceInfo, upnpRule *rule_ptr);
        void removeAllPortMappingsFromIGD();
        //char* ipAddressToCharArr(IPAddress ipAddress);  // ?? not sure this is needed
        void upnpRuleToString(upnpRule *rule_ptr);
        String getSpacesString(int num);
        IPAddress getHost(String url);
        int getPort(String url);
        String getPath(String url);
        String getTagContent(const String &line, String tagName);
        void ssdpDeviceToString(ssdpDevice* ssdpDevice);

        /* members */
        upnpRuleNode *_headRuleNode;
        unsigned long _lastUpdateTime;
        long _timeoutMs;  // 0 for blocking operation
        WiFiUDP _udpClient;
        WiFiClient _wifiClient;
        gatewayInfo _gwInfo;
        unsigned long _consequtiveFails;
};

#endif
