#ifndef ARDUINO_MQTT_LOOPED_H
#define ARDUINO_MQTT_LOOPED_H

#include <functional>
#include <vector>
using namespace std;

#include <utility/wifi_drv.h>
#include <utility/server_drv.h>
#include <utility/WiFiSocketBuffer.h>

// ---------------------------------------- TIMING CONFIG ------------------------------------------

// Timeout for reading packets.
#define READ_PACKET_TIMEOUT 750

// Timeout for sending packets.
#define SEND_PACKET_TIMEOUT 750

// Keepalive sent with connect packet
#define MQTT_CONN_KEEPALIVE 300

// How long should we go with no packets before verifying the connection via a ping.
#define VERIFY_TIMEOUT 20000

// Interval for sending MQTT status.
#define STATUS_UPDATE_INTERVAL 30000

// --------------------------------------------- DEFS ----------------------------------------------

// Use 3 (MQTT 3.0) or 4 (MQTT 3.1.1).
#define MQTT_PROTOCOL_LEVEL 4

// Packet types.
#define MQTT_CTRL_CONNECT 0x1
#define MQTT_CTRL_CONNECTACK 0x2
#define MQTT_CTRL_PUBLISH 0x3
#define MQTT_CTRL_PUBACK 0x4
#define MQTT_CTRL_PUBREC 0x5
#define MQTT_CTRL_PUBREL 0x6
#define MQTT_CTRL_PUBCOMP 0x7
#define MQTT_CTRL_SUBSCRIBE 0x8
#define MQTT_CTRL_SUBACK 0x9
#define MQTT_CTRL_UNSUBSCRIBE 0xA
#define MQTT_CTRL_UNSUBACK 0xB
#define MQTT_CTRL_PINGREQ 0xC
#define MQTT_CTRL_PINGRESP 0xD
#define MQTT_CTRL_DISCONNECT 0xE

// QOS 2 not supported.
#define MQTT_QOS_1 0x1
#define MQTT_QOS_0 0x0

// Flags for connection packet.
#define MQTT_CONN_USERNAMEFLAG 0x80
#define MQTT_CONN_PASSWORDFLAG 0x40
#define MQTT_CONN_WILLRETAIN 0x20
#define MQTT_CONN_WILLQOS_1 0x08
#define MQTT_CONN_WILLQOS_2 0x18
#define MQTT_CONN_WILLFLAG 0x04
#define MQTT_CONN_CLEANSESSION 0x02

// Largest full packet we're able to send.
// Need to be able to store at least ~90 chars for a connect packet with full 23 char client ID.
// This could be replaced by the ability to dynamically allocate a buffer as needed.
#if defined(ARDUINO_ARCH_ESP8266) || defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_SAMD)
#define MAXBUFFERSIZE (512)
#else
#define MAXBUFFERSIZE (150)
#endif

// If WiFi library differs.
#ifndef WL_SUCCESS
#define WL_SUCCESS 1
#endif
#ifndef WL_FAILURE
#define WL_FAILURE -1
#endif

// ------------------------------------------- DEBUGGERY -------------------------------------------

// Uncomment/comment to turn on/off debug output messages.
#define MQTT_LOG
#define MQTT_DEBUG

// Set where debug messages will be printed.
#define LOG_PRINTER Serial
// If using something like Zero or Due, change the above to SerialUSB

/**
 * @brief Helper function for error and debug printing a buffer.
 * 
 * @param buffer buffer to print
 * @param len length of buffer to print
 *
 * @see https://github.com/adafruit/Adafruit_MQTT_Library
 */
void printBuffer(uint8_t *buffer, uint16_t len);

// Define actual debug output functions when necessary.
#ifdef MQTT_LOG
#define LOG_PRINT(...) { LOG_PRINTER.print(__VA_ARGS__); }
#define LOG_PRINTLN(...) { LOG_PRINTER.println(__VA_ARGS__); }
#define LOG_PRINTBUFFER(buffer, len) { printBuffer(buffer, len); }
#else
#define LOG_PRINT(...) {}
#define LOG_PRINTLN(...) {}
#define LOG_PRINTBUFFER(buffer, len) {}
#endif

#ifdef MQTT_DEBUG
#define DEBUG_PRINT(...) { LOG_PRINTER.print(__VA_ARGS__); }
#define DEBUG_PRINTLN(...) { LOG_PRINTER.println(__VA_ARGS__); }
#define DEBUG_PRINTBUFFER(buffer, len) { printBuffer(buffer, len); }
#else
#define DEBUG_PRINT(...) {}
#define DEBUG_PRINTLN(...) {}
#define DEBUG_PRINTBUFFER(buffer, len) {}
#endif

// -------------------------------------------- TYPEDEF --------------------------------------------

/**
 * @brief Arduino_MQTT_Looped connection status.
 */
typedef enum {
  // Startup
  MQTT_LOOPED_STATUS_INIT = 0,
  // WiFi
  MQTT_LOOPED_STATUS_WIFI_READY = 1,
  MQTT_LOOPED_STATUS_WIFI_OFFLINE = 2,
  MQTT_LOOPED_STATUS_WIFI_ERRORS = 3,
  MQTT_LOOPED_STATUS_WIFI_CLOSING_SOCKET = 4,
  // < WiFi offline
  MQTT_LOOPED_STATUS_WIFI_CONNECTED = 10,
  // MQTT
  MQTT_LOOPED_STATUS_MQTT_CONNECTING = 20,
  MQTT_LOOPED_STATUS_MQTT_CONNECTION_WAIT = 21,
  MQTT_LOOPED_STATUS_MQTT_CONNECTION_SUCCESS = 22,
  MQTT_LOOPED_STATUS_MQTT_CONNECTED_TO_BROKER = 23,
  MQTT_LOOPED_STATUS_MQTT_CONNECTION_CONFIRMED = 24,
  MQTT_LOOPED_STATUS_MQTT_CLOSING_SOCKET = 25,
  MQTT_LOOPED_STATUS_MQTT_DISCONNECTED = 28,
  MQTT_LOOPED_STATUS_MQTT_OFFLINE = 29,
  MQTT_LOOPED_STATUS_MQTT_ERRORS = 30,
  // < MQTT offline
  MQTT_LOOPED_STATUS_OKAY = 40,
  // > MQTT is active
  MQTT_LOOPED_STATUS_MQTT_SUBSCRIBED = 66,
  MQTT_LOOPED_STATUS_MQTT_SUBSCRIBING = 67,
  MQTT_LOOPED_STATUS_MQTT_SUBSCRIPTION_FAIL = 68,
  MQTT_LOOPED_STATUS_MQTT_ANNOUNCED = 69,
  MQTT_LOOPED_STATUS_READING_CONACK_PACKET = 50,
  MQTT_LOOPED_STATUS_READING_SUBACK_PACKET = 53,
  MQTT_LOOPED_STATUS_READING_PUBACK_PACKET = 55,
  MQTT_LOOPED_STATUS_READING_SUB_PACKET = 54,
  MQTT_LOOPED_STATUS_READING_PING_PACKET = 51,
  MQTT_LOOPED_STATUS_READING_PACKET = 52,
  MQTT_LOOPED_STATUS_SENDING_DISCOVERY = 56,
  MQTT_LOOPED_STATUS_SUBSCRIPTION_PACKET_READ = 60,
  MQTT_LOOPED_STATUS_SUBSCRIPTION_IN_QUEUE = 61,
  MQTT_LOOPED_STATUS_VERIFY_CONNECTION = 62,
  MQTT_LOOPED_STATUS_CONNECTION_VERIFIED = 63,
  MQTT_LOOPED_STATUS_MQTT_PUBLISHING = 64,
  MQTT_LOOPED_STATUS_MQTT_PUBLISHED = 65,
} mqtt_looped_status_t;

/**
 * @brief MQTT message.
 */
typedef struct mqtt_message_t {
  String topic;
  String payload;
  uint8_t qos;
  bool retain;
} mqtt_message_t;

/**
 * @brief MQTT subscription callback function.
 */
typedef std::function<void(char*,uint16_t)> mqttcallback_t;

// -------------------------------------- SUBSCRIPTION CLASS ---------------------------------------

/**
 * @brief Subscription class.
 */
class MQTTSubscribe {
  public:
    /**
     * @brief Constructor
     * 
     * @param topic 
     * @param qos
     */
    MQTTSubscribe(String topic, uint8_t qos = 0);

    /**
     * @brief Set a callback for the subscription.
     * 
     * @param callb Lambda-compatible callback.
     */
    void setCallback(mqttcallback_t callb);

    /**
     * @brief Lambda-compatible callback function.
     */
    mqttcallback_t callback;

    /**
     * @brief Topic.
     */
    String topic;

    /**
     * @brief Quality of Service level.
     */
    uint8_t qos;

    /**
     * @brief Last data read from this subscription.
     */
    uint8_t lastread[MAXBUFFERSIZE];

    /**
     * @brief Number valid bytes in lastread.
     *        Limited to MAXBUFFERSIZE-1 to ensure null terminating lastread.
     */
    uint16_t datalen;

    /**
     * @brief Whether this subscription has a new message that should be processed.
     */
    bool new_message;
};

// -------------------------------------------- ROBBERY --------------------------------------------
// Rob a pointer to a private property of an object.

// Struct to hold the stolen pointer (ptr).
template<typename Tag>
struct robbed {
  typedef typename Tag::type type;
  static type ptr;
};
template<typename Tag>
typename robbed<Tag>::type robbed<Tag>::ptr;

// Struct to rob a pointer.
template<typename Tag, typename Tag::type p>
struct rob : robbed<Tag> {
  struct filler {
    filler() { robbed<Tag>::ptr = p; }
  };
  static filler filler_obj;
};
template<typename Tag, typename Tag::type p>
typename rob<Tag, p>::filler rob<Tag, p>::filler_obj;

// (uint8_t) &WiFiClient::_sock
// wifiClientObj->_sock <=> &(wifiClientObj->*robbed<WiFiClientSock>::ptr);
struct WiFiClientSock { typedef uint8_t WiFiClient::*type; };
template class rob<WiFiClientSock, &WiFiClient::_sock>;

// ------------------------------------------ MAIN CLASS -------------------------------------------

/**
 * @brief This class manages WiFi connection and MQTT broker connection as well
 *        as handles MQTT subscription callbacks.
 */
class Arduino_MQTT_Looped {
  public:
    /**
     * @brief Constructor.
     */
    Arduino_MQTT_Looped(WiFiClient* client, const char* ssid, const char* wifi_pass, IPAddress* mqtt_server,
      const char* mqtt_client_id, const char* mqtt_user, const char* mqtt_pass);

    /**
     * @brief Set the Airlift onboard LED to a specific color.
     * 
     * @param r 
     * @param g 
     * @param b 
     */
    void setLED(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief Get status.
     * 
     * @return status enum
     */
    mqtt_looped_status_t getStatus(void);

    // ------------------------------------- CONNECTION STATUS -------------------------------------

    /**
     * @brief Return WiFi status without querying anything new.
     *
     * @return connected
     */
    bool wifiIsConnected(void);

    /**
     * @brief Return MQTT status without querying anything new.
     *
     * @return connected
     */
    bool mqttIsConnected(void);

    /**
     * @brief Return whether MQTT is connected and currently doing...something.
     *
     * @return actively doing a thing
     */
    bool mqttIsActive(void);

    /**
     * @brief Verify MQTT WiFi connection is stable by pinging the MQTT server.
     *
     * @return connected
     */
    bool verifyConnection(void);

    // ----------------------------------------- MAIN LOOP -----------------------------------------

    /**
     * @brief Main loop.
     */
    void loop(void);

    // ----------------------------------------- MESSAGING -----------------------------------------

    /**
     * @brief Set the birth topic.
     *        Set before connecting.
     * 
     * @param topic
     * @param payload
     */
    void setBirth(String topic, String payload);

    /**
     * @brief Set the last will and testament.
     *        Set before connecting.
     * 
     * @param topic 
     * @param payload 
     * @param qos 
     * @param retain 
     * @return success
     */
    bool setWill(String topic, String payload, uint8_t qos = 0, bool retain = false);

    /**
     * @brief Add a discovery message to send when connected or reconnected to the MQTT broker.
     *        Set before connecting.
     * 
     * @param topic 
     * @param payload 
     * @param qos 
     * @param retain 
     * @return success
     */
    void addDiscovery(String topic, String payload, uint8_t qos = 0, bool retain = false);

    /**
     * @brief MQTT hook.
     *        Set before connecting.
     *
     * @param topic
     * @param callback
     */
    void onMqtt(String topic, mqttcallback_t callback);

    /**
     * @brief Send MQTT message. Verifies connection before sending.
     *
     * @param topic
     * @param payload
     * @param retain
     * @param qos
     */
    void mqttSendMessage(String topic, String payload, bool retain = false, uint8_t qos = 0);

  // --------------------##-------------------- PRIVATE ---------------------##---------------------

  private:
    // ------------------------------------- GENERAL PROPS -----------------------------------------

    /**
     * @brief Current status of Arduino_MQTT_Looped connections.
     */
    mqtt_looped_status_t status = MQTT_LOOPED_STATUS_INIT;

    /**
     * @brief Timer for waiting.
     */
    uint32_t timer = 0;

    /**
     * @brief Counter for attempting tasks.
     */
    uint32_t attempts = 0;

    // --------------------------------------- WIFI PROPS ------------------------------------------

    /**
     * @brief The WiFi client.
     */
    WiFiClient* wifiClient;

    /**
     * @brief WiFi network name.
     */
    const char* ssid;

    /**
     * @brief Password for WiFi.
     */
    const char* wifi_pass;

    /**
     * @brief Pointer to socket (private) of wifiClient.
     */
    uint8_t* _sock;

    // --------------------------------------- MQTT PROPS ------------------------------------------

    /**
     * @brief MQTT server address.
     */
    IPAddress* mqtt_server;

    /**
     * @brief Client ID for MQTT.
     */
    const char* mqtt_client_id;

    /**
     * @brief Username for MQTT broker.
     */
    const char* mqtt_user;

    /**
     * @brief Password for MQTT broker.
     */
    const char* mqtt_pass;

    /**
     * @brief Last will and testament.
     */
    mqtt_message_t will;

    /**
     * @brief General buffer used for MQTT in/out.
     */
    uint8_t buffer[MAXBUFFERSIZE];

    /**
     * @brief Flag for whether we are currently reading a full packet.
     */
    bool reading_full_packet = false;

    /**
     * @brief Flag for whether we are currently reading part of a packet.
     */
    bool reading_packet = false;

    /**
     * @brief Iterator for process of reading full packet.
     *        -1 sets a timer for a new loop.
     *        0 is the start of the loop.
     */
    int8_t read_packet_jump_to = -1;

    /**
     * @brief Timer for reading a packet. Controls timeout.
     */
    uint32_t read_packet_timer;

    /**
     * @brief ID for counting packets.
     */
    uint16_t packet_id_counter;

    /**
     * @brief Buffer for processing packet.
     */
    uint8_t* read_packet_pbuf;

    /**
     * @brief Buffer for reading packet.
     */
    uint8_t* read_packet_buf;

    /**
     * @brief Max length of packet being read.
     */
    uint16_t read_packet_maxlen;

    /**
     * @brief Length of packet being read.
     */
    uint16_t read_packet_len;

    /**
     * @brief Length of full packet read.
     */
    uint16_t full_packet_len;

    /**
     * @brief Timer for sending a packet. Controls timeout.
     */
    uint32_t send_packet_timer;

    /**
     * @brief Time the last packet was successfully received.
     */
    uint32_t last_con_verify;

    /**
     * @brief Vector of pointers for subscriptions.
     */
    std::vector<MQTTSubscribe*> mqttSubs;

    /**
     * @brief Vector of pointers for discovery messages.
     */
    std::vector<mqtt_message_t*> discoveries;

    /**
     * @brief Count up the number of subscriptions.
     */
    uint8_t subscription_counter = 0;

    /**
     * @brief Count up the number of discovery messages.
     */
    uint8_t discovery_counter = 0;

    /**
     * @brief Birth topic.
     */
    std::pair<String, String> birth_msg;

    // ----------------------------------------- MESSAGING -----------------------------------------

    /**
     * @brief Publish a MQTT message.
     * 
     * @param topic
     * @param payload
     * @param retain
     * @param qos
     * @return success
     */
    bool mqttPublish(String topic, String payload, bool retain = false, uint8_t qos = 0);

    /**
     * @brief Process a single subscription flagged as having a new message.
     *
     * @return subscription processed
     */
    bool processSubscriptionQueue(void);

    // -------------------------- CONNECTION LOOP - CLOSE SOCKET, RESTART --------------------------

    /**
     * @brief Close connection.
     *
     * @param wifi_connected
     * @return socket is closing
     */
    bool closeConnection(bool wifi_connected);

    /**
     * @brief Finish closing socket.
     * 
     * @param wifi_connected 
     * @return socket closed
     */
    bool closeSocket(bool wifi_connected);

    // ---------------------------------- CONNECTION LOOP - WIFI -----------------------------------

    /**
     * @brief Set WiFi connection up.
     * 
     * @return success
     */
    bool wifiSetup(void);

    /**
     * @brief Connect to WiFi.
     * 
     * @return success
     */
    bool wifiConnect(void);

    // ---------------------------------- CONNECTION LOOP - MQTT -----------------------------------

    /**
     * @brief Connect to MQTT server.
     * 
     * @return success
     */
    bool mqttConnect(void);

    /**
     * @brief Wait on connection to server.
     *
     * @return success
     */
    bool waitOnConnection(void);

    /**
     * @brief Wait after connection to server.
     *
     * @return finished waiting
     */
    bool waitAfterConnection(void);

    /**
     * @brief Connect to MQTT broker.
     * 
     * @return success
     */
    bool mqttConnectBroker(void);

    /**
     * @brief Confirm connection to broker after sending connection packet.
     *
     * @return success
     */
    bool confirmConnectToBroker();

    /**
     * @brief Connect MQTT subscriptions.
     *
     * @return success
     */
    bool mqttSubscribe(void);

    /**
     * @brief Move to next subscription.
     *
     * @return there is another to process
     */
    bool mqttSubscribeInc(void);

    /**
     * @brief Send MQTT announcement.
     * 
     * @return success
     */
    bool mqttAnnounce(void);

    /**
     * @brief Loop for sending MQTT discovery messages.
     * 
     * @return success
     */
    bool sendDiscoveries(void);

    // ------------------------------------ PACKET PROCESSING --------------------------------------

    /**
     * @brief Stepped loop for reading a full packet.
     */
    void readFullPacketUntilComplete(void);

    /**
     * @brief Read MQTT packet from the server. Will read up to maxlen bytes and store
     *        the data in the provided buffer. Waits up to the specified timeout (in
     *        milliseconds) for data to be available. Called multiple times in
     *        readFullPacketUntilComplete().
     *
     * @return finished reading packet or timed out
     */
    bool readPacketUntilComplete(void);

    /**
     * @brief Send data to the server specified by the buffer and length of data.
     * 
     * @param buffer 
     * @param len 
     * @return success
     *
     * @see https://github.com/adafruit/Adafruit_MQTT_Library
     */
    bool sendPacket(uint8_t *buffer, uint16_t len);

    /**
     * @brief Handles a single subscription packet received.
     *
     * @param len 
     * @return success
     *
     * @see https://github.com/adafruit/Adafruit_MQTT_Library
     */
    bool handleSubscriptionPacket(uint16_t len);

    /**
     * @brief Generate a connection packet.
     *        This connect packet and code follows the MQTT 3.1 spec (some small differences
     *        in the protocol).
     * 
     * @return packet length
     *
     * @see https://github.com/adafruit/Adafruit_MQTT_Library
     * @see http://public.dhe.ibm.com/software/dw/webservices/ws-mqtt/mqtt-v3r1.html#connect
     */
    uint8_t connectPacket(void);

    /**
     * @brief Generate a publish packet.
     * 
     * @param topic 
     * @param data 
     * @param bLen 
     * @param qos 
     * @param maxPacketLen 
     * @param retain 
     * @return packet length
     *
     * @see https://github.com/adafruit/Adafruit_MQTT_Library
     * @see http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Toc398718040
     */
    uint16_t publishPacket(const char *topic, uint8_t *data, uint16_t bLen, uint8_t qos, bool retain);

    /**
     * @brief Generate a subscriptiong packet.
     *
     * @param topic 
     * @param qos 
     * @return packet length
     *
     * @see https://github.com/adafruit/Adafruit_MQTT_Library
     */
    uint8_t subscribePacket(const char *topic, uint8_t qos);
};


/**
 * @brief Helper function to only print as much of a string as possible to a buffer.
 * 
 * @param p 
 * @param s 
 * @param maxlen 
 * @return pointer to copy of data, possibly shortened
 *
 * @see https://github.com/adafruit/Adafruit_MQTT_Library
 */
static uint8_t* stringprint(uint8_t *p, const char *s, uint16_t maxlen = 0);

/**
 * @brief Helper function used to figure out how much bigger the payload needs to be
 *        in order to account for its variable length field.
 * 
 * @param currLen 
 * @return additional length
 *
 * @see https://github.com/adafruit/Adafruit_MQTT_Library
 * @see http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html#_Table_2.4_Size
 */
static uint16_t packetAdditionalLen(uint32_t currLen);

#endif
