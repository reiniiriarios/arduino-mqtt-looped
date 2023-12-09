#include "MQTT_Looped.h"

// -------------------------------------- SUBSCRIPTION CLASS ---------------------------------------

MQTTSubscribe::MQTTSubscribe(const char* topic, uint8_t qos) : topic(topic), qos(qos) {};

void MQTTSubscribe::setCallback(mqttcallback_t cb) {
  this->callback = cb;
}

// ------------------------------------------ MAIN CLASS -------------------------------------------

MQTT_Looped::MQTT_Looped(
  WiFiClient* client,
  const char* ssid,
  const char* wifi_pass,
  IPAddress* mqtt_server,
  uint16_t port,
  const char* mqtt_user,
  const char* mqtt_pass,
  const char* mqtt_client_id
) : wifiClient(client),
    ssid(ssid),
    wifi_pass(wifi_pass),
    mqtt_server(mqtt_server),
    port(port),
    mqtt_client_id(mqtt_client_id),
    mqtt_user(mqtt_user),
    mqtt_pass(mqtt_pass)
{
  // Create a pointer to `_sock` private property of wifiClient.
  // @todo Abstract away from WiFi client.
  this->_sock = &(this->wifiClient->*robbed<WiFiClientSock>::ptr);
}

mqtt_looped_status_t MQTT_Looped::getStatus(void) {
  return this->status;
}

// ------------------------------------------- MAIN LOOP -------------------------------------------

void MQTT_Looped::loop(void) {
  // if (this->status != MQTT_LOOPED_STATUS_OKAY) {
  //   // Prints a lot.
  //   DEBUG_PRINT(F("MQTT_Looped: "));
  //   DEBUG_PRINTLN(this->status);
  // }
  switch (this->status) {
    case MQTT_LOOPED_STATUS_INIT:
    case MQTT_LOOPED_STATUS_WIFI_OFFLINE:
      // Basic setup.
      this->wifiSetup();
      return;
    case MQTT_LOOPED_STATUS_WIFI_READY:
      // Connect to local network.
      this->wifiConnect();
      return;
    case MQTT_LOOPED_STATUS_WIFI_CONNECTED:
    case MQTT_LOOPED_STATUS_MQTT_DISCONNECTED:
    case MQTT_LOOPED_STATUS_MQTT_OFFLINE:
      // Connect to server over WiFi.
      this->mqttConnect();
      return;
    case MQTT_LOOPED_STATUS_WIFI_CLOSING_SOCKET:
      this->closeSocket(false);
      return;
    case MQTT_LOOPED_STATUS_MQTT_CLOSING_SOCKET:
      this->closeSocket(true);
      return;
    case MQTT_LOOPED_STATUS_WIFI_ERRORS:
      this->closeConnection(false);
      return;
    case MQTT_LOOPED_STATUS_MQTT_ERRORS:
      this->closeConnection(true);
      return;
    case MQTT_LOOPED_STATUS_MQTT_CONNECTING:
      // Wait for connection to establish.
      this->waitOnConnection();
      return;
    case MQTT_LOOPED_STATUS_MQTT_CONNECTION_WAIT:
      // After connecting to server, delay a few seconds to establish connection.
      this->waitAfterConnection();
      return;
    case MQTT_LOOPED_STATUS_MQTT_CONNECTION_SUCCESS:
    case MQTT_LOOPED_STATUS_MQTT_MISSING_CONACK:
      // After connecting to the server, make the MQTT broker connection.
      this->mqttConnectBroker();
      return;
    case MQTT_LOOPED_STATUS_READING_CONACK_PACKET:
      // Look for conack to confirm connection to the broker.
      this->confirmConnectToBroker();
      return;
    case MQTT_LOOPED_STATUS_MQTT_CONNECTION_CONFIRMED:
    case MQTT_LOOPED_STATUS_MQTT_SUBSCRIPTION_FAIL:
    case MQTT_LOOPED_STATUS_MQTT_SUBSCRIBING:
      // Subscribe to each subscription, one per loop.
      this->mqttSubscribe();
      return;
    case MQTT_LOOPED_STATUS_MQTT_SUBSCRIBED:
      // Send the birth/announcement that we're online.
      this->mqttAnnounce();
      return;
    case MQTT_LOOPED_STATUS_MQTT_ANNOUNCED:
    case MQTT_LOOPED_STATUS_SENDING_DISCOVERY:
      // Send discoveries, if any, one per loop.
      this->sendDiscoveries();
      return;
    case MQTT_LOOPED_STATUS_READING_SUB_PACKET:
      // Look for a subscription packet. If none, go back to OKAY.
      this->lookForSubPacket();
      return;
    case MQTT_LOOPED_STATUS_READING_SUBACK_PACKET:
    case MQTT_LOOPED_STATUS_READING_PUBACK_PACKET:
    case MQTT_LOOPED_STATUS_READING_PING_PACKET:
      // Keep looping until we read a specific packet or we time out.
      this->readFullPacketSearch();
      return;
    case MQTT_LOOPED_STATUS_SUBSCRIPTION_PACKET_READ:
      // If we've read a subscription packet, process the contents.
      this->handleSubscriptionPacket();
      return;
    case MQTT_LOOPED_STATUS_OKAY:
      // Verify connection every so often.
      if (millis() - this->last_con_verify > VERIFY_TIMEOUT) {
        this->verifyConnection();
        return;
      }
      // If there's any read subscription to process, process one and loop.
      if (this->processSubscriptionQueue()) {
        return;
      }
      // If not doing anything else, look for subscription packets next loop.
      this->status = MQTT_LOOPED_STATUS_READING_SUB_PACKET;
      return;
    default:
      LOG_PRINT("Error: Unrecognized MQTT_Looped status: ");
      LOG_PRINTLN(this->status);
      this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
  }
}

// ---------------------------- CONNECTION LOOP - CLOSE SOCKET, RESTART ----------------------------

bool MQTT_Looped::closeConnection(bool wifi_connected) {
  if (*this->_sock != NO_SOCKET_AVAIL) {
    DEBUG_PRINTLN(F("Closing socket..."));
    ServerDrv::stopClient(*this->_sock);
    // In either loop, the next step is to wait for the socket to close.
    this->status = wifi_connected ? MQTT_LOOPED_STATUS_MQTT_CLOSING_SOCKET : MQTT_LOOPED_STATUS_WIFI_CLOSING_SOCKET;
    return false;
  }
  // In WiFi loop, connection is ready to begin.
  // In MQTT loop, connection was closed and needs to reconnect.
  this->status = wifi_connected ? MQTT_LOOPED_STATUS_MQTT_DISCONNECTED : MQTT_LOOPED_STATUS_INIT;
  return true;
}

bool MQTT_Looped::closeSocket(bool wifi_connected) {
  // @todo Abstract away from WiFi client.
  if (wifiClient->status() != CLOSED) {
    DEBUG_PRINTLN(F("Socket closing..."));
    return false;
  }
  WiFiSocketBuffer.close(*_sock);
  *this->_sock = NO_SOCKET_AVAIL;
  DEBUG_PRINTLN(F("Socket closed"));
  // In WiFi loop, connection is ready to begin.
  // In MQTT loop, connection was closed and needs to reconnect.
  this->status = wifi_connected ? MQTT_LOOPED_STATUS_MQTT_DISCONNECTED : MQTT_LOOPED_STATUS_WIFI_READY;
  return true;
}

// ------------------------------------ CONNECTION LOOP - WIFI -------------------------------------

bool MQTT_Looped::wifiSetup(void) {
  // If the socket isn't closed, close it and wait for the next loop.
  if (!this->closeConnection(false)) {
    return false;
  }
  // Connect
  LOG_PRINT(F("Connecting WiFi... "));
  LOG_PRINT(this->ssid);
  int8_t ret = WiFiDrv::wifiSetPassphrase(this->ssid, strlen(this->ssid), this->wifi_pass, strlen(this->wifi_pass));
  if (ret == WL_SUCCESS) {
    LOG_PRINTLN(F("...ready"));
    this->status = MQTT_LOOPED_STATUS_WIFI_READY;
    this->attempts = 0;
    return true;
  }
  LOG_PRINTLN(F("...failure"));
  // no status update, if we can't get this one, we're not going anywhere...
  return false;
}

bool MQTT_Looped::wifiConnect(void) {
  // Each "attempt" is a single loop of waiting, and we want to wait a few seconds here.
  // If it just never connects, try closing the connection and reopening.
  this->attempts++;
  if (this->attempts > 720) {
    this->status = MQTT_LOOPED_STATUS_WIFI_ERRORS;
    this->attempts = 0;
    return false;
  }
  // Loop and wait here until the WiFi chip connects.
  int8_t wifiStatus = WiFiDrv::getConnectionStatus();
  switch (wifiStatus) {
    case WL_CONNECTED:
      this->status = MQTT_LOOPED_STATUS_WIFI_CONNECTED;
      LOG_PRINTLN(F("WiFi connected"));
      return true; // yay
    case WL_IDLE_STATUS:
    case WL_NO_SSID_AVAIL:
    case WL_SCAN_COMPLETED:
      // do nothing, wait...we might be here a few seconds...
      return false;
    case WL_FAILURE:
    case WL_AP_FAILED:
    case WL_CONNECT_FAILED:
    case WL_CONNECTION_LOST:
      this->status = MQTT_LOOPED_STATUS_WIFI_OFFLINE;
      LOG_PRINTLN(F("Connection failure"));
      return false;
    default:
      this->status = MQTT_LOOPED_STATUS_WIFI_OFFLINE;
      LOG_PRINT(F("Connection failure, unknown failure: "));
      LOG_PRINTLN(String(wifiStatus));
      return false;
  }
}

// ------------------------------------ CONNECTION LOOP - MQTT -------------------------------------

bool MQTT_Looped::mqttConnect(void) {
  // We're reconnecting, so close the old connection first if open.
  if (*this->_sock != NO_SOCKET_AVAIL) {
    if (!this->closeConnection(true)) {
      return false;
    }
  }

  // Get a socket and confirm or restart.
  *this->_sock = ServerDrv::getSocket();
  if (*this->_sock == NO_SOCKET_AVAIL) {
    // failure, flag to start over
    DEBUG_PRINTLN(F("No Socket available"));
    this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
    return false;
  }
  DEBUG_PRINT(F("Connecting on socket "));
  DEBUG_PRINTLN(*this->_sock);

  // Connect to server.
  this->status = MQTT_LOOPED_STATUS_MQTT_CONNECTING;
  DEBUG_PRINTLN(F("Connecting to server..."));
  ServerDrv::startClient(uint32_t(*this->mqtt_server), this->port, *this->_sock);
  this->timer = millis();
  return false;
}

bool MQTT_Looped::waitOnConnection(void) {
  if (this->wifiClient->connected()) {
    LOG_PRINT(F("Connected to MQTT server, status: "));
    // @todo Abstract away from WiFi client.
    LOG_PRINTLN(this->wifiClient->status());
    this->status = MQTT_LOOPED_STATUS_MQTT_CONNECTION_WAIT;
    this->timer = millis(); // start for next wait
    return true;
  }
  // If we've waited long enough, give up and start over.
  if (millis() - this->timer > 4000) {
    LOG_PRINT(F("Connection to MQTT server failed, status: "));
    // @todo Abstract away from WiFi client.
    LOG_PRINTLN(this->wifiClient->status());
    this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
    this->timer = 0;
    return false;
  }
  // Keep waiting...
  return false;
}

bool MQTT_Looped::waitAfterConnection(void) {
  if (millis() - this->timer > 3000) {
    this->status = MQTT_LOOPED_STATUS_MQTT_CONNECTION_SUCCESS;
    this->timer = 0;
    return true;
  }
  return false;
}

bool MQTT_Looped::mqttConnectBroker() {
  LOG_PRINT(F("MQTT connecting to broker..."));
  // Check attempts.
  this->attempts++;
  if (this->attempts >= 5 || !this->wifiClient->connected()) {
    DEBUG_PRINTLN(F("error"));
    this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
    this->attempts = 0;
    return false;
  }
  // Construct and send connect packet.
  uint8_t len = this->connectPacket();
  if (!this->sendPacket(this->buffer, len)) {
    DEBUG_PRINTLN(F("err send packet"));
    // If we err here, we try again and fail after n attempts.
    return false;
  }
  // Read connect response packet and verify it
  this->status = MQTT_LOOPED_STATUS_READING_CONACK_PACKET;
  DEBUG_PRINTLN(F("Reading conack"));
  return true;
}

bool MQTT_Looped::confirmConnectToBroker() {
  this->readFullPacket();
  // Wait until a full packet read attempt is complete,
  if (this->read_packet_jump_to != -1) {
    return false;
  }
  // Then try to process what was read:
  if (this->full_packet_len != 4) {
    DEBUG_PRINT(F("err read packet.."));
    this->status = MQTT_LOOPED_STATUS_MQTT_MISSING_CONACK;
    return false;
  }
  if ((this->buffer[0] != (MQTT_CTRL_CONNECTACK << 4)) || (this->buffer[1] != 2)) {
    DEBUG_PRINT(F("err read buf.."));
    this->status = MQTT_LOOPED_STATUS_MQTT_MISSING_CONACK;
    return false;
  }
  if (this->buffer[3] != 0) {
    DEBUG_PRINT(F("buffer ret: "));
    DEBUG_PRINT(this->buffer[3]);
    DEBUG_PRINT(F(".."));
    if (!this->buffer[3] == 1) {
      this->status = MQTT_LOOPED_STATUS_MQTT_MISSING_CONACK;
      return false;
    }
  }

  LOG_PRINTLN(F("success"));
  this->status = MQTT_LOOPED_STATUS_MQTT_CONNECTION_CONFIRMED;
  return true;
}

bool MQTT_Looped::mqttSubscribe(void) {
  // If already in this loop, move to the next item.
  if (this->status == MQTT_LOOPED_STATUS_MQTT_SUBSCRIBING) {
    this->attempts = 0;
    DEBUG_PRINTLN(F("..subscribed"));
    if (!this->mqttSubscribeInc()) {
      DEBUG_PRINTLN(F("..done"));
      return true;
    }
  }
  this->status = MQTT_LOOPED_STATUS_MQTT_SUBSCRIBING;

  // Check connection & number of attempts.
  // If we're consistently failing, let's... start over?
  if (!this->wifiClient->connected() || this->attempts > 3) {
    DEBUG_PRINTLN(F("..sub failed"));
    this->attempts = 0;
    this->subscription_counter = 0;
    this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
    return false;
  }
  this->attempts++;

  // Find the next subscription to process or return if we finished.
  MQTTSubscribe* sub;
  do {
    sub = this->mqttSubs.at(this->subscription_counter);
    if (sub && sub->topic != nullptr) {
      break; // found
    }
    if (!this->mqttSubscribeInc()) {
      return true; // none left
    }
  } while (true);

  // Subscribe
  LOG_PRINT(F("MQTT subscribing: "));
  LOG_PRINTLN(sub->topic);
  // Construct and send subscription packet.
  uint8_t len = this->subscribePacket(sub->topic, 0);
  if (!this->sendPacket(this->buffer, len)) {
    DEBUG_PRINTLN(F("..error sending packet"));
    this->status = MQTT_LOOPED_STATUS_MQTT_SUBSCRIPTION_FAIL;
    return false;
  }
  this->status = MQTT_LOOPED_STATUS_READING_SUBACK_PACKET;
  return true;
}

bool MQTT_Looped::mqttSubscribeInc(void) {
  this->attempts = 0;
  ++this->subscription_counter;
  if (this->subscription_counter >= this->mqttSubs.size()) {
    this->subscription_counter = 0;
    this->status = MQTT_LOOPED_STATUS_MQTT_SUBSCRIBED;
    return false;
  }
  return true;
}

bool MQTT_Looped::mqttAnnounce(void) {
  if (this->birth_msg.first != "") {
    LOG_PRINTLN(F("Announcing.."));
    // QoS is 0, so we don't wait on a puback.
    if (!this->mqttPublish(this->birth_msg.first, this->birth_msg.second, false, 0)) {
      LOG_PRINTLN(F("failed"));
      if (!this->wifiClient->connected()) {
        DEBUG_PRINTLN(F("offline"));
        this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
      }
      return false;
    }
    DEBUG_PRINTLN(F("announced"));
  }
  this->status = MQTT_LOOPED_STATUS_MQTT_ANNOUNCED;
  return true;
}

bool MQTT_Looped::sendDiscoveries(void) {
  DEBUG_PRINTLN(F("Discoveries..."));
  // If there are none to send at the current counter, do nothing.
  if (this->discovery_counter >= this->discoveries.size()) {
    LOG_PRINTLN(F("Connection okay."));
    this->status = MQTT_LOOPED_STATUS_OKAY;
    this->discovery_counter = 0;
    return true;
  }
  LOG_PRINT(F("Sending discovery: "));
  // Send at current counter, then inc and return, wait for next loop.
  auto d = this->discoveries.at(this->discovery_counter);
  LOG_PRINTLN(d->topic);
  if (!this->mqttPublish(d->topic, d->payload)) {
    LOG_PRINTLN(F("error sending discovery"));
    if (!this->wifiClient->connected()) {
      this->discovery_counter = 0;
      this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
    }
    return false;
  }
  this->status = MQTT_LOOPED_STATUS_SENDING_DISCOVERY;
  this->discovery_counter++;
  return true;
}

bool MQTT_Looped::processSubscriptionQueue(void) {
  for (auto & sub : this->mqttSubs) {
    if (sub->new_message) {
      sub->callback((char *)sub->lastread, sub->datalen);
      sub->new_message = false;
      return true; // only read one
    }
  }
  return false; // none read
}

// --------------------------------------- CONNECTION STATUS ---------------------------------------

bool MQTT_Looped::verifyConnection(void) {
  DEBUG_PRINTLN("Verifying connection...");
  // Construct and send ping packet.
  this->buffer[0] = MQTT_CTRL_PINGREQ << 4;
  this->buffer[1] = 0;
  if (!sendPacket(this->buffer, 2)) {
    this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
    return false;
  }
  this->status = MQTT_LOOPED_STATUS_READING_PING_PACKET;
  return true;
}

bool MQTT_Looped::wifiIsConnected(void) {
  return (int)this->status >= (int)MQTT_LOOPED_STATUS_WIFI_CONNECTED;
}

bool MQTT_Looped::mqttIsConnected(void) {
  return (int)this->status >= (int)MQTT_LOOPED_STATUS_OKAY;
}

bool MQTT_Looped::mqttIsActive(void) {
  return (int)this->status >= (int)MQTT_LOOPED_STATUS_ACTIVE;
}

// ------------------------------------------- MESSAGING -------------------------------------------

void MQTT_Looped::setBirth(const char* topic, const char* payload) {
  this->birth_msg = { topic, payload };
}

bool MQTT_Looped::setWill(const char* topic, const char* payload, uint8_t qos, bool retain) {
  if (this->mqttIsConnected()) {
    DEBUG_PRINTLN(F("Error: will defined after connect"));
    return false;
  }
  this->will = {
    .topic = topic,
    .payload = payload,
    .qos = qos,
    .retain = retain,
  };
  return true;
}

void MQTT_Looped::addDiscovery(const char* topic, const char* payload, uint8_t qos, bool retain) {
  if (this->mqttIsConnected()) {
    DEBUG_PRINTLN(F("Error: discovery added after connect"));
    return;
  }
  this->discoveries.push_back(new mqtt_message_t{
    .topic = topic,
    .payload = payload,
    .qos = qos,
    .retain = retain,
  });
}

void MQTT_Looped::onMqtt(const char* topic, mqttcallback_t callback) {
  MQTTSubscribe* sub = new MQTTSubscribe(topic);
  sub->setCallback(callback);
  this->mqttSubs.push_back(sub);
}

void MQTT_Looped::mqttSendMessage(const char* topic, const char* payload, bool retain, uint8_t qos) {
  // If not connected OR if in the middle of something.
  if (!this->mqttIsConnected() || this->mqttIsActive()) {
    return;
  }
  if (!this->wifiClient->connected()) {
    this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
    return;
  }
  LOG_PRINT(F("MQTT publishing to "));
  LOG_PRINTLN(topic);
  if (!this->mqttPublish(topic, payload, retain, qos)) {
    LOG_PRINTLN(F("Error publishing"));
  }
}

void MQTT_Looped::mqttSendMessage(const char* topic, String payload, bool retain, uint8_t qos) {
  this->mqttSendMessage(topic, payload.c_str(), retain, qos);
}

void MQTT_Looped::mqttSendMessage(const char* topic, float payload, bool retain, uint8_t qos) {
  this->mqttSendMessage(topic, String(payload).c_str(), retain, qos);
}

void MQTT_Looped::mqttSendMessage(const char* topic, uint32_t payload, bool retain, uint8_t qos) {
  this->mqttSendMessage(topic, String(payload).c_str(), retain, qos);
}

bool MQTT_Looped::mqttPublish(const char* topic, const char* payload, bool retain, uint8_t qos) {
  if (this->status != MQTT_LOOPED_STATUS_MQTT_PUBLISHED) {
    uint8_t* data = (uint8_t *)payload;
    uint16_t bLen = strlen(payload);
    // Construct and send publish packet.
    uint16_t len = this->publishPacket(topic, data, bLen, qos, retain);
    if (!this->sendPacket(this->buffer, len)) {
      return false;
    }
    this->attempts = 0;
    // If QoS is 0, skip waiting for puback.
    if (qos == 0) {
      this->status = MQTT_LOOPED_STATUS_OKAY;
      return true;
    }
    this->status = MQTT_LOOPED_STATUS_READING_PUBACK_PACKET;
    return true;
  }
  else if (qos > 0) {
    DEBUG_PRINT(F("Publish QOS1+ reply:\t"));
    DEBUG_PRINTBUFFER(this->buffer, this->full_packet_len);
    if (this->full_packet_len != 4) {
      this->attempts++;
      DEBUG_PRINTLN(F("Error reading puback"));
      if (this->attempts > 3) {
        this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
        this->attempts = 0;
      }
      return false;
    }

    uint16_t packnum = this->buffer[2];
    packnum <<= 8;
    packnum |= this->buffer[3];
    // we increment the packet_id_counter right after publishing so inc here too to match
    packnum = packnum + 1 + (packnum + 1 == 0); // Skip zero
    if (packnum != this->packet_id_counter) {
      DEBUG_PRINTLN(F("Error publishing"));
    }
  }
  this->status = MQTT_LOOPED_STATUS_OKAY;
  return true;
}

// -------------------------------------- PACKET PROCESSING ----------------------------------------

void MQTT_Looped::readFullPacketSearch(void) {
  // Start timer.
  if (!this->read_packet_search) {
    this->read_packet_search_timer = millis();
    this->read_packet_search = true;
  }
  // Check timeout.
  if (this->read_packet_search && millis() - this->read_packet_search_timer > READ_PACKET_SEARCH_TIMEOUT) {
    DEBUG_PRINTLN(F("Search timed out.."));
    this->read_packet_search = false;
    // If we were trying to subscribe and we got nothing, assume it failed.
    if (this->status == MQTT_LOOPED_STATUS_READING_SUBACK_PACKET) {
      this->status = MQTT_LOOPED_STATUS_MQTT_SUBSCRIPTION_FAIL;
    }
    // If we didn't find a sub packet, that's fine.
    else if (this->status == MQTT_LOOPED_STATUS_READING_SUB_PACKET) {
      this->status = MQTT_LOOPED_STATUS_OKAY;
    }
    // If we published and waited for a response, we didn't get one, go back to that loop.
    else if (this->status == MQTT_LOOPED_STATUS_READING_PUBACK_PACKET) {
      this->status = MQTT_LOOPED_STATUS_MQTT_PUBLISHED;
    }
    // If we were sent a ping and got nothing, assume the connection should be reset.
    else if (this->status == MQTT_LOOPED_STATUS_READING_PING_PACKET) {
      this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
    }
    // The following should never be called.
    else {
      DEBUG_PRINTLN(F("This is a weird place to be."));
      this->status = MQTT_LOOPED_STATUS_OKAY;
    }
    return; // = done
  }
  // Read a full packet.
  this->readFullPacket();
  // If the readFullPacket loop has come back around to -1, the start, we've either finished
  // reading a full packet or something borked and we were kicked out of the loop.
  if (this->read_packet_jump_to == -1) {
    // If we didn't read anything, something might be wrong.
    if (this->full_packet_len == 0) {
      DEBUG_PRINTLN(F("search: no packet len"));
      this->read_packet_search = false;
      if (this->status == MQTT_LOOPED_STATUS_READING_SUBACK_PACKET) {
        this->status = MQTT_LOOPED_STATUS_MQTT_SUBSCRIPTION_FAIL;
      } else if (this->status == MQTT_LOOPED_STATUS_READING_PUBACK_PACKET) {
        this->status = MQTT_LOOPED_STATUS_MQTT_PUBLISHED;
      }
      return;
    }
    uint8_t packetType = this->buffer[0] >> 4;
    switch (packetType) {
      case 0: // no packet, keep trying
        this->full_packet_len = 0;
        DEBUG_PRINT(".");
        return;
      // Looking for the following:
      case MQTT_CTRL_SUBACK:
        if (this->status == MQTT_LOOPED_STATUS_READING_SUBACK_PACKET) {
          this->read_packet_search = false;
          this->status = MQTT_LOOPED_STATUS_MQTT_SUBSCRIBING;
        }
        return;
      case MQTT_CTRL_PUBACK:
        if (this->status == MQTT_LOOPED_STATUS_READING_PUBACK_PACKET) {
          this->read_packet_search = false;
          this->status = MQTT_LOOPED_STATUS_MQTT_PUBLISHED;
        }
        return;
      case MQTT_CTRL_PINGRESP:
        if (this->status == MQTT_LOOPED_STATUS_READING_PING_PACKET) {
          this->read_packet_search = false;
          this->status = MQTT_LOOPED_STATUS_OKAY;
        }
        return;
      // Not looking for the following, but process if we read it:
      case MQTT_CTRL_PUBLISH:
        this->read_packet_search = false;
        this->status = MQTT_LOOPED_STATUS_SUBSCRIPTION_PACKET_READ;
        return;
      // Not looking for these:
      case MQTT_CTRL_CONNECTACK:  // not relevant here
      case MQTT_CTRL_CONNECT:     // send only
      case MQTT_CTRL_DISCONNECT:  // send only
      case MQTT_CTRL_PINGREQ:     // send only
      case MQTT_CTRL_SUBSCRIBE:   // send only
      case MQTT_CTRL_UNSUBSCRIBE: // unused
      case MQTT_CTRL_UNSUBACK:    // unused
      case MQTT_CTRL_PUBREC:      // unused
      case MQTT_CTRL_PUBREL:      // unused
      case MQTT_CTRL_PUBCOMP:     // unused
        DEBUG_PRINT(F("unexpected packet: "));
        DEBUG_PRINTLN(packetType);
        this->full_packet_len = 0;
        break;
      default:
        DEBUG_PRINT(F("unrecognized packet: "));
        DEBUG_PRINTLN(packetType);
        this->full_packet_len = 0;
    }
  }
}

void MQTT_Looped::lookForSubPacket(void) {
  this->readFullPacket(); // loop once...
  // when done,
  if (this->read_packet_jump_to == -1) {
    if (this->full_packet_len > 0) {
      this->status = MQTT_LOOPED_STATUS_SUBSCRIPTION_PACKET_READ;
    } else {
      this->status = MQTT_LOOPED_STATUS_OKAY;
    }
  }
}

void MQTT_Looped::readFullPacket(void) {
  // Check we haven't timed out.
  if (this->read_packet_jump_to > 0 && millis() - this->read_packet_timer > READ_PACKET_TIMEOUT) {
    this->read_packet_jump_to = -1; // giving up, reset timer next time
    this->reading_packet = false; // reset individual read
    // If we didn't find a sub packet, that's fine.
    if (this->status == MQTT_LOOPED_STATUS_READING_SUB_PACKET) {
      // no debug lines here pls, we do this a lot
      return;
    }
    DEBUG_PRINTLN();
    DEBUG_PRINT(F("reading packet timed out at step "));
    DEBUG_PRINT(this->read_packet_jump_to);
    DEBUG_PRINT(F(", status: "));
    DEBUG_PRINTLN(this->status);
    // If offline, flag to connect; if connected, flag to reset connection.
    // if (!this->wifiClient->connected()) {
    //   DEBUG_PRINT(F("offline.."));
    //   this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
    // }
    // else {
    //   DEBUG_PRINT(F("errors?.."));
    //   this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
    // }
    return;
  }
  // Run through each step incrementally, intermittently waiting on a packet read
  // before moving to the next step.
  switch (this->read_packet_jump_to) {
    case -1:
      // start timer
      this->read_packet_timer = millis();
      this->read_packet_jump_to++;
    case 0:
      // Save input
      this->read_packet_buf = this->buffer;
      this->read_packet_pbuf = this->buffer;
      // init
      this->full_packet_len = 0;
      this->read_packet_maxlen = 1;
      this->read_packet_jump_to++;
    case 1:
      if (readPacket()) {
        this->read_packet_jump_to++;
      }
      return;
    case 2: // do...(while)
      if (this->read_packet_len != 1) {
        DEBUG_PRINTLN(F("bad pkt len 1"));
        this->full_packet_len = 0;
        this->read_packet_jump_to = -1; // reset loop
        return;
      }
      DEBUG_PRINT(F("Packet Type:\t"));
      DEBUG_PRINTBUFFER(this->read_packet_pbuf, this->read_packet_len);
      this->read_packet_pbuf++;
      this->read_packet_jump_to++;
      this->read_packet_value = 0;
      this->read_packet_multiplier = 1;
    case 3:
      if (readPacket()) {
        this->read_packet_jump_to++;
      }
      break;
    case 4:
      if (this->read_packet_len != 1) {
        DEBUG_PRINTLN(F("bad pkt len 2"));
        this->full_packet_len = 0;
        this->read_packet_jump_to = -1; // reset loop
        return;
      }
      {
        uint8_t encodedByte = this->read_packet_pbuf[0]; // save the last read val
        this->read_packet_pbuf++; // get ready for reading the next byte
        uint32_t intermediate = (encodedByte & 0x7F) * this->read_packet_multiplier;
        this->read_packet_value += intermediate;
        this->read_packet_multiplier *= 128;
        if (this->read_packet_multiplier > (128UL * 128UL * 128UL)) {
          DEBUG_PRINT(F("Malformed packet len\n"));
          this->read_packet_jump_to = -1; // reset loop
          return;
        }
        if (encodedByte & 0x80) {
          this->read_packet_jump_to = 3; // (do...) while
          return;
        }
        DEBUG_PRINT(F("Packet Length:\t"));
        DEBUG_PRINTLN(this->read_packet_value);
        // maxsize is limited to 65536 by 16-bit unsigned
        uint16_t sizediff = (MAXBUFFERSIZE - (this->read_packet_pbuf - this->read_packet_buf) - 1);
        if (this->read_packet_value > uint32_t(sizediff)) {
          DEBUG_PRINTLN(F("Packet too big for buffer"));
          this->read_packet_maxlen = sizediff;
        } else {
          this->read_packet_maxlen = this->read_packet_value;
        }
      }
      this->read_packet_jump_to++;
    case 5:
      if (readPacket()) {
        this->read_packet_jump_to++;
      }
      return;
    case 6:
      // done
      this->full_packet_len = (this->read_packet_pbuf - this->read_packet_buf) + this->read_packet_len;
      this->last_con_verify = millis();
      this->read_packet_jump_to = -1; // read, reset timer next time
      return;
    default:
      DEBUG_PRINT(F("Fell out of packet loop: "));
      DEBUG_PRINTLN(String(this->read_packet_jump_to));
      this->read_packet_jump_to = -1; // read, reset timer next time
      this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS; //??
  }
}

bool MQTT_Looped::readPacket(void) {
  // If we're out of read time, call it and move on.
  if (this->reading_packet && millis() - this->read_packet_timer > READ_PACKET_TIMEOUT) {
    DEBUG_PRINTLN();
    this->reading_packet = false;
    return true; // done! <<< success?
  }
  // start engines
  if (!this->reading_packet) {
    this->read_packet_len = 0;
    this->reading_packet = true;
    this->read_packet_timer = millis();
  }
  // handle zero-length packets
  if (this->read_packet_maxlen == 0) {
    this->reading_packet = false;
    return true;
  }
  // see if there is any data pending
  if (!this->wifiClient->available()) {
    DEBUG_PRINT("-");
    return false; // wait for it...
  }
  // there's data still coming in, reset the timer
  this->read_packet_timer = millis();
  // read packet
  char c = this->wifiClient->read();
  this->read_packet_pbuf[this->read_packet_len] = c;
  this->read_packet_len++;
  if (this->read_packet_len < this->read_packet_maxlen) {
    DEBUG_PRINT(".");
    return false;
  }
  // finished
  DEBUG_PRINT(F("Read packet:\t"));
  DEBUG_PRINTBUFFER(this->read_packet_pbuf, this->read_packet_len);
  this->reading_packet = false;
  return true; // we hit maxlen, done! <<< success
}

bool MQTT_Looped::handleSubscriptionPacket() {
  uint16_t i, topiclen, datalen;
  uint16_t len = this->full_packet_len;
  this->full_packet_len = 0;

  // Whatever happens, happens. If this method fails, we still want to go
  // back to the normal loop, and we shouldn't have gotten here unless the
  // previous status is OKAY.
  this->status = MQTT_LOOPED_STATUS_OKAY;

  if (!len) {
    return false; // No data available, just quit.
  }
  DEBUG_PRINT("Packet len: ");
  DEBUG_PRINTLN(len);
  DEBUG_PRINTBUFFER(this->buffer, len);

  if (len < 3) {
    return false;
  }
  if ((this->buffer[0] & 0xF0) != (MQTT_CTRL_PUBLISH) << 4) {
    return false;
  }

  // Parse out length of packet.
  // NOTE: This includes data in the variable header and the payload.
  uint16_t remainingLen = len - 4; // subtract the 4 header bytes
  uint16_t const topicoffset = packetAdditionalLen(remainingLen);
  uint16_t const topicstart = topicoffset + 4;

  topiclen = int((this->buffer[2 + topicoffset]) << 8 | this->buffer[3 + topicoffset]);
  DEBUG_PRINT(F("Looking for subscription len "));
  DEBUG_PRINTLN(topiclen);

  // Find subscription associated with this packet.
  MQTTSubscribe* thisSub = nullptr;
  for (auto & sub : mqttSubs) {
    // Skip this subscription if its name length isn't the same as the received topic name.
    if (strlen(sub->topic) != topiclen)
      continue;
    // Stop if the subscription topic matches the received topic. Be careful
    // to make comparison case insensitive.
    if (strncasecmp((char *)this->buffer + topicstart, sub->topic, topiclen) == 0) {
      DEBUG_PRINT(F("Found sub #"));
      DEBUG_PRINTLN(i);
      if (sub->new_message) {
        DEBUG_PRINTLN(F("Lost previous message"));
      } else {
        sub->new_message = true;
      }
      thisSub = sub;
      break;
    }
  }
  if (thisSub == nullptr) {
    return false; // matching sub not found ???
  }

  uint8_t packet_id_len = 0;
  uint16_t packetid = 0;
  // Check if it is QoS 1. QoS 2 is unsupported.
  if ((this->buffer[0] & 0x6) == 0x2) {
    packet_id_len = 2;
    packetid = this->buffer[topiclen + topicstart];
    packetid <<= 8;
    packetid |= this->buffer[topiclen + topicstart + 1];
  }

  // zero out the old data
  memset(thisSub->lastread, 0, MAXBUFFERSIZE);

  datalen = len - topiclen - packet_id_len - topicstart;
  if (datalen > MAXBUFFERSIZE) {
    datalen = MAXBUFFERSIZE - 1; // cut it off
  }
  // extract out just the data, into the subscription object itself
  memmove(thisSub->lastread, this->buffer + topicstart + topiclen + packet_id_len, datalen);
  thisSub->datalen = datalen;
  DEBUG_PRINT(F("Data len: "));
  DEBUG_PRINTLN(datalen);
  DEBUG_PRINT(F("Data: "));
  DEBUG_PRINTLN((char *)thisSub->lastread);

  if ((MQTT_PROTOCOL_LEVEL > 3) && (this->buffer[0] & 0x6) == 0x2) {
    uint8_t ackpacket[4];

    // Construct and send puback packet.
    ackpacket[0] = MQTT_CTRL_PUBACK << 4;
    ackpacket[1] = 2;
    ackpacket[2] = packetid >> 8;
    ackpacket[3] = packetid;
    if (!this->sendPacket(ackpacket, 4)) {
      DEBUG_PRINT(F("Failed"));
    }
  }

  return true;
}

bool MQTT_Looped::sendPacket(uint8_t *buf, uint16_t len) {
  uint16_t ret = 0;
  uint16_t offset = 0;
  DEBUG_PRINTLN(F("Sending packet"));
  while (len > 0) {
    // Check we haven't timed out.
    this->send_packet_timer = millis();
    if (millis() - this->send_packet_timer > SEND_PACKET_TIMEOUT) {
      DEBUG_PRINT(F("sending packet timed out.."));
      // If offline, flag to connect; if connected, flag to reset connection.
      if (!this->wifiClient->connected()) {
        DEBUG_PRINT(F("offline.."));
        this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
      } else {
        DEBUG_PRINT(F("errors?.."));
        this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
      }
      return false;
    }
    // send 250 bytes at most at a time, can adjust this later based on Client
    uint16_t sendlen = len > 250 ? 250 : len;
    ret = this->wifiClient->write(buf + offset, sendlen);
    DEBUG_PRINT(F("Client sendPacket returned: "));
    DEBUG_PRINTLN(ret);
    if (ret != sendlen) {
      DEBUG_PRINTLN(F("Failed to send packet."));
      return false;
    }
    len -= ret;
    offset += ret;
  }
  this->last_con_verify = millis();
  return true;
}

uint8_t MQTT_Looped::connectPacket(void) {
  uint8_t *p = this->buffer;
  uint16_t len;

  // fixed header, connection messsage no flags
  p[0] = (MQTT_CTRL_CONNECT << 4) | 0x0;
  p += 2;
  // fill in packet[1] last
  p = stringprint(p, "MQTT");

  p[0] = MQTT_PROTOCOL_LEVEL;
  p++;

  // always clean the session
  p[0] = MQTT_CONN_CLEANSESSION;

  // set the will flags if needed
  if (this->will.topic) {

    p[0] |= MQTT_CONN_WILLFLAG;

    if (this->will.qos == 1)
      p[0] |= MQTT_CONN_WILLQOS_1;
    else if (this->will.qos == 2)
      p[0] |= MQTT_CONN_WILLQOS_2;

    if (this->will.retain == 1)
      p[0] |= MQTT_CONN_WILLRETAIN;
  }

  p[0] |= MQTT_CONN_USERNAMEFLAG;
  p[0] |= MQTT_CONN_PASSWORDFLAG;
  p++;

  p[0] = MQTT_CONN_KEEPALIVE >> 8;
  p++;
  p[0] = MQTT_CONN_KEEPALIVE & 0xFF;
  p++;

  if (MQTT_PROTOCOL_LEVEL == 3) {
    p = stringprint(p, this->mqtt_client_id, 23); // Limit client ID to first 23 characters.
  } else {
    if (pgm_read_byte(this->mqtt_client_id) != 0) {
      p = stringprint(p, this->mqtt_client_id);
    } else {
      p[0] = 0x0;
      p++;
      p[0] = 0x0;
      p++;
      DEBUG_PRINTLN(F("SERVER GENERATING CLIENT ID"));
    }
  }

  if (this->will.topic) {
    p = stringprint(p, this->will.topic);
    p = stringprint(p, this->will.payload);
  }

  p = stringprint(p, this->mqtt_user);
  p = stringprint(p, this->mqtt_pass);

  len = p - this->buffer;

  this->buffer[1] = len - 2; // don't include the 2 bytes of fixed header data
  DEBUG_PRINTLN(F("MQTT connect packet:"));
  DEBUG_PRINTBUFFER(this->buffer, len);
  return len;
}

uint16_t MQTT_Looped::publishPacket(const char *topic, uint8_t *data, uint16_t bLen, uint8_t qos, bool retain) {
  uint8_t *p = this->buffer;
  uint16_t len = 0;
  uint16_t maxPacketLen = (uint16_t)sizeof(this->buffer);

  // calc length of non-header data
  len += 2;             // two bytes to set the topic size
  len += strlen(topic); // topic length
  if (qos > 0) {
    len += 2; // qos packet id
  }
  // Calculate additional bytes for length field (if any)
  uint16_t additionalLen = packetAdditionalLen(len + bLen);

  // Payload remaining length. When maxPacketLen provided is 0, let's
  // assume buffer is big enough. Fingers crossed.
  // 2 + additionalLen: header byte + remaining length field (from 1 to 4 bytes)
  // len = topic size field + value (string)
  // bLen = buffer size
  if (!(maxPacketLen == 0 || (len + bLen + 2 + additionalLen <= maxPacketLen))) {
    // If we make it here, we got a pickle: the payload is not going
    // to fit in the packet buffer. Instead of corrupting memory, let's
    // do something less damaging by reducing the bLen to what we are
    // able to accomodate. Alternatively, consider using a bigger
    // maxPacketLen.
    bLen = maxPacketLen - (len + 2 + packetAdditionalLen(maxPacketLen));
  }
  len += bLen; // remaining len excludes header byte & length field

  // Now you can start generating the packet!
  p[0] = MQTT_CTRL_PUBLISH << 4 | qos << 1 | (retain ? 1 : 0);
  p++;

  // fill in packet[1] last
  do {
    uint8_t encodedByte = len % 128;
    len /= 128;
    // if there are more data to encode, set the top bit of this byte
    if (len > 0) {
      encodedByte |= 0x80;
    }
    p[0] = encodedByte;
    p++;
  } while (len > 0);

  // topic comes before packet identifier
  p = stringprint(p, topic);

  // add packet identifier. used for checking PUBACK in QOS > 0
  if (qos > 0) {
    p[0] = (this->packet_id_counter >> 8) & 0xFF;
    p[1] = this->packet_id_counter & 0xFF;
    p += 2;

    // increment the packet id, skipping 0
    this->packet_id_counter = this->packet_id_counter + 1 + (this->packet_id_counter + 1 == 0);
  }

  memmove(p, data, bLen);
  p += bLen;
  len = p - this->buffer;
  DEBUG_PRINTLN(F("MQTT publish packet:"));
  DEBUG_PRINTBUFFER(this->buffer, len);
  return len;
}

uint8_t MQTT_Looped::subscribePacket(const char *topic, uint8_t qos) {
  uint8_t *p = this->buffer;
  uint16_t len;

  p[0] = MQTT_CTRL_SUBSCRIBE << 4 | MQTT_QOS_1 << 1;
  // fill in packet[1] last
  p += 2;

  // packet identifier. used for checking SUBACK
  p[0] = (this->packet_id_counter >> 8) & 0xFF;
  p[1] = this->packet_id_counter & 0xFF;
  p += 2;

  // increment the packet id, skipping 0
  this->packet_id_counter = this->packet_id_counter + 1 + (this->packet_id_counter + 1 == 0);

  p = stringprint(p, topic);

  p[0] = qos;
  p++;

  len = p - this->buffer;
  this->buffer[1] = len - 2; // don't include the 2 bytes of fixed header data
  DEBUG_PRINTLN(F("..subscription packet:"));
  DEBUG_PRINTBUFFER(this->buffer, len);
  return len;
}

static uint8_t *stringprint(uint8_t *p, const char *s, uint16_t maxlen) {
  // If maxlen is specified (has a non-zero value) then use it as the maximum
  // length of the source string to write to the buffer.  Otherwise write
  // the entire source string.
  uint16_t len = strlen(s);
  if (maxlen > 0 && len > maxlen) {
    len = maxlen;
  }
  p[0] = len >> 8;
  p++;
  p[0] = len & 0xFF;
  p++;
  strncpy((char *)p, s, len);
  return p + len;
}

static uint16_t packetAdditionalLen(uint32_t currLen) {
  /* Increase length field based on current length */
  if (currLen < 128) // 7-bits
    return 0;
  if (currLen < 16384) // 14-bits
    return 1;
  if (currLen < 2097152) // 21-bits
    return 2;
  return 3;
}

void printBuffer(uint8_t *buffer, uint16_t len) {
  LOG_PRINTER.print('\t');
  for (uint16_t i = 0; i < len; i++) {
    if (isprint(buffer[i]))
      LOG_PRINTER.write(buffer[i]);
    else
      LOG_PRINTER.print(" ");
    LOG_PRINTER.print(F(" [0x"));
    if (buffer[i] < 0x10)
      LOG_PRINTER.print("0");
    LOG_PRINTER.print(buffer[i], HEX);
    LOG_PRINTER.print("], ");
    if (i % 8 == 7) {
      LOG_PRINTER.print("\n\t");
    }
  }
  LOG_PRINTER.println();
}

#if defined(ARDUINO_SAMD_ZERO) || defined(ARDUINO_SAMD_MKR1000) || defined(ARDUINO_SAMD_MKR1010) || defined(ARDUINO_ARCH_SAMD)
static char *dtostrf(double val, signed char width, unsigned char prec, char *sout) {
  char fmt[20];
  sprintf(fmt, "%%%d.%df", width, prec);
  sprintf(sout, fmt, val);
  return sout;
}
#endif

#if defined(ESP8266)
int strncasecmp(const char *str1, const char *str2, int len) {
  int d = 0;
  while (len--) {
    int c1 = tolower(*str1++);
    int c2 = tolower(*str2++);
    if (((d = c1 - c2) != 0) || (c2 == '\0')) {
      return d;
    }
  }
  return 0;
}
#endif
