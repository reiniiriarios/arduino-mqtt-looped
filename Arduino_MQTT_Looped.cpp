#include "Arduino_MQTT_Looped.h"

// -------------------------------------- SUBSCRIPTION CLASS ---------------------------------------

MQTTSubscribe::MQTTSubscribe(String topic, uint8_t qos) : topic(topic), qos(qos) {};

void MQTTSubscribe::setCallback(mqttcallback_t cb) {
  this->callback = cb;
}

// ------------------------------------------ MAIN CLASS -------------------------------------------

Arduino_MQTT_Looped::Arduino_MQTT_Looped(WiFiClient* client, const char* ssid, const char* wifi_pass,
  IPAddress* mqtt_server, const char* mqtt_client_id, const char* mqtt_user, const char* mqtt_pass)
  : wifiClient(client), ssid(ssid), wifi_pass(wifi_pass), mqtt_server(mqtt_server),
    mqtt_client_id(mqtt_client_id), mqtt_user(mqtt_user), mqtt_pass(mqtt_pass) {
  // Create a pointer to `_sock` private property of wifiClient.
  // @todo Abstract away from WiFi client.
  this->_sock = &(this->wifiClient->*robbed<WiFiClientSock>::ptr);
}

mqtt_looped_status_t Arduino_MQTT_Looped::getStatus(void) {
  return this->status;
}

// ------------------------------------------- MAIN LOOP -------------------------------------------

void Arduino_MQTT_Looped::loop(void) {
  // if (this->status != MQTT_LOOPED_STATUS_OKAY) {
  //   // Prints a lot.
  //   DEBUG_PRINT(F("Arduino_MQTT_Looped: "));
  //   DEBUG_PRINTLN(this->status);
  // }
  switch (this->status) {
    case MQTT_LOOPED_STATUS_INIT:
    case MQTT_LOOPED_STATUS_WIFI_OFFLINE:
      this->wifiSetup();
      return;
    case MQTT_LOOPED_STATUS_WIFI_READY:
      this->wifiConnect();
      return;
    case MQTT_LOOPED_STATUS_WIFI_CONNECTED:
    case MQTT_LOOPED_STATUS_MQTT_DISCONNECTED:
    case MQTT_LOOPED_STATUS_MQTT_OFFLINE:
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
      this->waitOnConnection();
      return;
    case MQTT_LOOPED_STATUS_MQTT_CONNECTION_WAIT:
      this->waitAfterConnection();
      return;
    case MQTT_LOOPED_STATUS_MQTT_CONNECTION_SUCCESS:
      this->mqttConnectBroker();
      return;
    case MQTT_LOOPED_STATUS_MQTT_CONNECTED_TO_BROKER:
      this->confirmConnectToBroker();
      return;
    case MQTT_LOOPED_STATUS_MQTT_CONNECTION_CONFIRMED:
    case MQTT_LOOPED_STATUS_MQTT_SUBSCRIPTION_FAIL:
    case MQTT_LOOPED_STATUS_MQTT_SUBSCRIBING:
      this->mqttSubscribe();
      return;
    case MQTT_LOOPED_STATUS_MQTT_SUBSCRIBED:
      this->mqttAnnounce();
      return;
    case MQTT_LOOPED_STATUS_MQTT_ANNOUNCED:
    case MQTT_LOOPED_STATUS_SENDING_DISCOVERY:
      this->sendDiscoveries();
      return;
    case MQTT_LOOPED_STATUS_READING_CONACK_PACKET:
    case MQTT_LOOPED_STATUS_READING_SUBACK_PACKET:
    case MQTT_LOOPED_STATUS_READING_SUB_PACKET:
    case MQTT_LOOPED_STATUS_READING_PING_PACKET:
    case MQTT_LOOPED_STATUS_READING_PACKET:
      this->readFullPacketUntilComplete();
      return;
    case MQTT_LOOPED_STATUS_SUBSCRIPTION_PACKET_READ:
      this->handleSubscriptionPacket(this->full_packet_len); // @todo remove param
      this->full_packet_len = 0; // @todo move this into method
      return;
    case MQTT_LOOPED_STATUS_SUBSCRIPTION_IN_QUEUE:
      this->processSubscriptionQueue();
      return;
    default:
      LOG_PRINT("Error: Unrecognized Arduino_MQTT_Looped status: ");
      LOG_PRINTLN(this->status);
    case MQTT_LOOPED_STATUS_OKAY:
      // If something is available and there's no current status, it might be a subscription packet.
      if (this->wifiClient->available()) {
        this->status = MQTT_LOOPED_STATUS_READING_SUB_PACKET;
        this->readFullPacketUntilComplete();
        return;
      }
      // Check connection.
      if (!this->wifiClient->connected()) {
        this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
        return;
      }
      // Verify connection every so often.
      if (millis() - this->last_con_verify > VERIFY_TIMEOUT) {
        this->verifyConnection();
        return;
      }
  }
}

// ---------------------------- CONNECTION LOOP - CLOSE SOCKET, RESTART ----------------------------

bool Arduino_MQTT_Looped::closeConnection(bool wifi_connected) {
  if (*this->_sock != NO_SOCKET_AVAIL) {
    DEBUG_PRINTLN(F("Closing socket..."));
    ServerDrv::stopClient(*this->_sock);
    // In either loop, the next step is to wait for the socket to close.
    this->status = wifi_connected ? MQTT_LOOPED_STATUS_MQTT_CLOSING_SOCKET : MQTT_LOOPED_STATUS_WIFI_CLOSING_SOCKET;
    return false;
  }
  // In WiFi loop, connection is ready to begin.
  // In MQTT loop, connection was closed and needs to reconnect.
  this->status = wifi_connected ? MQTT_LOOPED_STATUS_MQTT_DISCONNECTED : MQTT_LOOPED_STATUS_WIFI_READY;
  return true;
}

bool Arduino_MQTT_Looped::closeSocket(bool wifi_connected) {
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

bool Arduino_MQTT_Looped::wifiSetup(void) {
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
    return true;
  }
  LOG_PRINTLN(F("...failure"));
  this->status = MQTT_LOOPED_STATUS_WIFI_ERRORS;
  return false;
}

bool Arduino_MQTT_Looped::wifiConnect(void) {
  int8_t wifiStatus = WiFiDrv::getConnectionStatus();
  switch (wifiStatus) {
    case WL_CONNECTED:
      this->status = MQTT_LOOPED_STATUS_WIFI_CONNECTED;
      LOG_PRINTLN(F("WiFi connected"));
      return true; // yay
    case WL_IDLE_STATUS:
    case WL_NO_SSID_AVAIL:
    case WL_SCAN_COMPLETED:
      // do nothing, wait...
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

bool Arduino_MQTT_Looped::mqttConnect(void) {
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
  ServerDrv::startClient(uint32_t(*this->mqtt_server), (uint16_t)1883, *this->_sock);
  this->timer = millis();
  return false;
}

bool Arduino_MQTT_Looped::waitOnConnection(void) {
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

bool Arduino_MQTT_Looped::waitAfterConnection(void) {
  if (millis() - this->timer > 3000) {
    this->status = MQTT_LOOPED_STATUS_MQTT_CONNECTION_SUCCESS;
    this->timer = 0;
    return true;
  }
  return false;
}

bool Arduino_MQTT_Looped::mqttConnectBroker() {
  LOG_PRINT(F("MQTT connecting to broker..."));
  // Check attempts.
  this->attempts++;
  if (this->attempts >= 5 || !this->wifiClient->connected()) {
    DEBUG_PRINT(F("error.."));
    this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
    this->attempts = 0;
    return false;
  }
  // Construct and send connect packet.
  uint8_t len = this->connectPacket();
  if (!this->sendPacket(this->buffer, len)) {
    DEBUG_PRINT(F("err send packet.."));
    // If we err here, we try again and fail after n attempts.
    return false;
  }
  // Read connect response packet and verify it
  this->status = MQTT_LOOPED_STATUS_READING_CONACK_PACKET;
  DEBUG_PRINT(F("reading conack.."));
  this->readFullPacketUntilComplete();
  return true;
}

bool Arduino_MQTT_Looped::confirmConnectToBroker() {
  this->attempts = 0;
  if (this->full_packet_len != 4) {
    DEBUG_PRINT(F("err read packet.."));
    this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
    return false;
  }
  if ((this->buffer[0] != (MQTT_CTRL_CONNECTACK << 4)) || (this->buffer[1] != 2)) {
    DEBUG_PRINT(F("err read buf.."));
    this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
    return false;
  }
  if (this->buffer[3] != 0) {
    DEBUG_PRINT(F("buffer ret: "));
    DEBUG_PRINT(this->buffer[3]);
    DEBUG_PRINT(F(".."));
    if (!this->buffer[3] == 1) {
      this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
      return false;
    }
  }

  LOG_PRINTLN(F("success"));
  this->status = MQTT_LOOPED_STATUS_MQTT_CONNECTION_CONFIRMED;
  return true;
}

bool Arduino_MQTT_Looped::mqttSubscribe(void) {
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
  LOG_PRINTLN(F("MQTT subscribing.."));
  DEBUG_PRINT(sub->topic);
  // Construct and send subscription packet.
  uint8_t len = this->subscribePacket(sub->topic.c_str(), 0);
  if (!this->sendPacket(this->buffer, len)) {
    DEBUG_PRINTLN(F("..error sending packet"));
    this->status = MQTT_LOOPED_STATUS_MQTT_SUBSCRIPTION_FAIL;
    return false;
  }
  this->status = MQTT_LOOPED_STATUS_READING_SUBACK_PACKET;
  this->readFullPacketUntilComplete();
  return true;
}

bool Arduino_MQTT_Looped::mqttSubscribeInc(void) {
  this->attempts = 0;
  ++this->subscription_counter;
  if (this->subscription_counter >= this->mqttSubs.size()) {
    this->subscription_counter = 0;
    this->status = MQTT_LOOPED_STATUS_MQTT_SUBSCRIBED;
    return false;
  }
  return true;
}

bool Arduino_MQTT_Looped::mqttAnnounce(void) {
  if (this->birth_msg.first != "") {
    LOG_PRINTLN(F("Announcing.."));
    if (!this->mqttPublish(this->birth_msg.first, this->birth_msg.second)) {
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

bool Arduino_MQTT_Looped::sendDiscoveries(void) {
  DEBUG_PRINT(F("Discoveries..."));
  // If there are none to send at the current counter, do nothing.
  if (this->discovery_counter >= this->discoveries.size()) {
    LOG_PRINTLN(F("Connection okay."));
    this->status = MQTT_LOOPED_STATUS_OKAY;
    this->discovery_counter = 0;
    return true;
  }
  LOG_PRINTLN(F("sending discovery.."));
  // Send at current counter, then inc and return, wait for next loop.
  this->status = MQTT_LOOPED_STATUS_SENDING_DISCOVERY;
  auto d = this->discoveries.at(this->discovery_counter);
  if (!this->mqttPublish(d->topic, d->payload)) {
    LOG_PRINTLN(F("error sending discovery"));
    if (!this->wifiClient->connected()) {
      this->discovery_counter = 0;
      this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
    }
    return false;
  }
  this->discovery_counter++;
  return true;
}

bool Arduino_MQTT_Looped::processSubscriptionQueue(void) {
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

bool Arduino_MQTT_Looped::verifyConnection(void) {
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

bool Arduino_MQTT_Looped::wifiIsConnected(void) {
  return (int)this->status >= (int)MQTT_LOOPED_STATUS_WIFI_CONNECTED;
}

bool Arduino_MQTT_Looped::mqttIsConnected(void) {
  return (int)this->status >= (int)MQTT_LOOPED_STATUS_OKAY;
}

bool Arduino_MQTT_Looped::mqttIsActive(void) {
  return (int)this->status > (int)MQTT_LOOPED_STATUS_OKAY;
}

// ------------------------------------------- MESSAGING -------------------------------------------

void Arduino_MQTT_Looped::setBirth(String topic, String payload) {
  this->birth_msg = { topic, payload };
}

bool Arduino_MQTT_Looped::setWill(String topic, String payload, uint8_t qos, bool retain) {
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

void Arduino_MQTT_Looped::addDiscovery(String topic, String payload, uint8_t qos, bool retain) {
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

void Arduino_MQTT_Looped::onMqtt(String topic, mqttcallback_t callback) {
  MQTTSubscribe* sub = new MQTTSubscribe(topic);
  sub->setCallback(callback);
  this->mqttSubs.push_back(sub);
}

void Arduino_MQTT_Looped::mqttSendMessage(String topic, String payload, bool retain, uint8_t qos) {
  if (!this->wifiClient->connected()) {
    this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
    return;
  }
  DEBUG_PRINT(F("MQTT publishing to "));
  DEBUG_PRINTLN(topic);
  if (!this->mqttPublish(topic, payload, retain, qos)) {
    DEBUG_PRINTLN(F("Error publishing"));
  }
}

bool Arduino_MQTT_Looped::mqttPublish(String topic, String payload, bool retain, uint8_t qos) {
  if (this->status != MQTT_LOOPED_STATUS_MQTT_PUBLISHED) {
    uint8_t* data = (uint8_t *)payload.c_str();
    uint16_t bLen = strlen(payload.c_str());
    const char* topic_c = topic.c_str();
    // Construct and send publish packet.
    uint16_t len = this->publishPacket(topic_c, data, bLen, qos, retain);
    if (!this->sendPacket(this->buffer, len)) {
      return false;
    }
    // If QoS is 0, skip waiting for puback.
    if (qos == 0) {
      this->status = MQTT_LOOPED_STATUS_OKAY;
      return true;
    }
    this->status = MQTT_LOOPED_STATUS_READING_PUBACK_PACKET;
    this->attempts = 0;
    this->readFullPacketUntilComplete();
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

void Arduino_MQTT_Looped::readFullPacketUntilComplete(void) {
  // Check we haven't timed out.
  if (this->read_packet_jump_to > 0 && millis() - this->read_packet_timer > READ_PACKET_TIMEOUT) {
    DEBUG_PRINT(F("timed out.."));
    // If offline, flag to connect; if connected, flag to reset connection.
    if (!this->wifiClient->connected()) {
      DEBUG_PRINT(F("offline.."));
      this->status = MQTT_LOOPED_STATUS_MQTT_OFFLINE;
    } else {
      DEBUG_PRINT(F("errors?.."));
      this->status = MQTT_LOOPED_STATUS_MQTT_ERRORS;
    }
    this->reading_full_packet = false;
    this->read_packet_jump_to = -1; // giving up, reset timer next time
    return;
  }
  // Run through each step incrementally, intermittently waiting on a packet read
  // before moving to the next step.
  switch (this->read_packet_jump_to) {
    case -1:
      // start timer
      this->read_packet_timer = millis();
    case 0:
      // Save input
      this->read_packet_buf = this->buffer;
      this->read_packet_pbuf = this->buffer;
      // init
      this->full_packet_len = 0;
      this->reading_full_packet = true;
      this->read_packet_maxlen = 1;
      this->read_packet_jump_to++;
    case 1:
      if (readPacketUntilComplete()) {
        this->read_packet_jump_to++;
      }
      break;
    case 2: // do...(while)
      if (this->read_packet_len != 1) {
        this->reading_full_packet = false;
        this->read_packet_jump_to = 0;
        return;
      }
      DEBUG_PRINT(F("Packet Type:\t"));
      DEBUG_PRINTBUFFER(this->read_packet_pbuf, this->read_packet_len);
      this->read_packet_pbuf++;
      this->read_packet_jump_to++;
    case 3:
      if (readPacketUntilComplete()) {
        this->read_packet_jump_to++;
      }
      break;
    case 4:
      if (this->read_packet_len != 1) {
        this->reading_full_packet = false;
        return;
      }
      {
        uint32_t value = 0;
        uint32_t multiplier = 1;
        uint8_t encodedByte = this->read_packet_pbuf[0]; // save the last read val
        this->read_packet_pbuf++; // get ready for reading the next byte
        uint32_t intermediate = encodedByte & 0x7F;
        intermediate *= multiplier;
        value += intermediate;
        multiplier *= 128;
        if (multiplier > (128UL * 128UL * 128UL)) {
          DEBUG_PRINT(F("Malformed packet len\n"));
          this->reading_full_packet = false;
          return;
        }
        if (encodedByte & 0x80) {
          this->read_packet_jump_to = 2; // (do...) while
        } else {
          DEBUG_PRINT(F("Packet Length:\t"));
          DEBUG_PRINTLN(value);
          this->read_packet_jump_to++;
        }
        // maxsize is limited to 65536 by 16-bit unsigned
        uint16_t sizediff = (MAXBUFFERSIZE - (this->read_packet_pbuf - this->read_packet_buf) - 1);
        if (value > uint32_t(sizediff)) {
          DEBUG_PRINTLN(F("Packet too big for buffer"));
          this->read_packet_maxlen = sizediff;
        } else {
          this->read_packet_maxlen = value;
        }
      }
    case 5:
      if (readPacketUntilComplete()) {
        this->read_packet_jump_to++;
      }
      break;
    case 6:
      this->reading_full_packet = false;
      this->read_packet_jump_to = -1; // read, reset timer next time
      // done
      this->full_packet_len = (this->read_packet_pbuf - this->read_packet_buf) + this->read_packet_len;
      {
        uint8_t packetType = (this->read_packet_buf[0] >> 4);
        switch (packetType) {
          case MQTT_CTRL_CONNECTACK:
            if (this->status == MQTT_LOOPED_STATUS_READING_CONACK_PACKET) {
              this->status = MQTT_LOOPED_STATUS_MQTT_CONNECTED_TO_BROKER;
            }
            break;
          case MQTT_CTRL_PUBLISH:
            this->status = MQTT_LOOPED_STATUS_SUBSCRIPTION_PACKET_READ;
            break;
          case MQTT_CTRL_SUBACK:
            this->status = MQTT_LOOPED_STATUS_MQTT_SUBSCRIBING;
            break;
          case MQTT_CTRL_PUBACK:
            if (this->status == MQTT_LOOPED_STATUS_READING_PUBACK_PACKET) {
              this->status = MQTT_LOOPED_STATUS_MQTT_PUBLISHED;
            }
            break;
          case MQTT_CTRL_PINGRESP:
            if (this->status == MQTT_LOOPED_STATUS_READING_PING_PACKET) {
              this->status = MQTT_LOOPED_STATUS_OKAY;
            }
            break;
          case MQTT_CTRL_CONNECT:     // send only
          case MQTT_CTRL_DISCONNECT:  // send only
          case MQTT_CTRL_PINGREQ:     // send only
          case MQTT_CTRL_SUBSCRIBE:   // send only
          case MQTT_CTRL_UNSUBSCRIBE: // unused
          case MQTT_CTRL_UNSUBACK:    // unused
          case MQTT_CTRL_PUBREC:      // unused
          case MQTT_CTRL_PUBREL:      // unused
          case MQTT_CTRL_PUBCOMP:     // unused
            DEBUG_PRINT(F("Unexpected packet: "));
            DEBUG_PRINTLN(packetType);
            break;
          default:
            DEBUG_PRINT(F("Unrecognized packet: "));
            DEBUG_PRINTLN(packetType);
        }
      }
      break;
    default:
      DEBUG_PRINT(F("Fell out of packet loop: "));
      DEBUG_PRINTLN(String(this->read_packet_jump_to));
  }
  this->last_con_verify = millis();
}

bool Arduino_MQTT_Looped::readPacketUntilComplete(void) {
  // Check we haven't timed out.
  if (this->reading_packet && millis() - this->read_packet_timer > READ_PACKET_TIMEOUT) {
    DEBUG_PRINT(F("..timed out.."));
    this->reading_packet = false;
    return true; // = done
  }
  // start engines
  if (!this->reading_packet) {
    this->read_packet_len = 0;
    this->reading_packet = true;
  }
  // handle zero-length packets
  if (this->read_packet_maxlen == 0) {
    return true;
  }
  // see if there is any data pending
  if (!this->wifiClient->available()) {
    // nothing left, finished?
    this->reading_packet = false;
    return true;
  }
  // read packet
  char c = this->wifiClient->read();
  this->read_packet_pbuf[this->read_packet_len] = c; // ERRRK
  this->read_packet_len++;
  if (this->read_packet_len != this->read_packet_maxlen) {
    return false;
  }
  // finished
  DEBUG_PRINT(F("Read data:\t"));
  DEBUG_PRINTBUFFER(this->read_packet_pbuf, this->read_packet_len);
  this->reading_packet = false;
  return true;
}

bool Arduino_MQTT_Looped::handleSubscriptionPacket(uint16_t len) {
  uint16_t i, topiclen, datalen;

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
    if (sub->topic.length() != topiclen)
      continue;
    // Stop if the subscription topic matches the received topic. Be careful
    // to make comparison case insensitive.
    if (strncasecmp((char *)this->buffer + topicstart, sub->topic.c_str(), topiclen) == 0) {
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

bool Arduino_MQTT_Looped::sendPacket(uint8_t *buf, uint16_t len) {
  uint16_t ret = 0;
  uint16_t offset = 0;
  DEBUG_PRINT(F("Sending packet"));
  while (len > 0) {
    // Check we haven't timed out.
    this->send_packet_timer = millis();
    if (millis() - this->send_packet_timer > SEND_PACKET_TIMEOUT) {
      DEBUG_PRINT(F("timed out.."));
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
    len -= ret;
    offset += ret;

    if (ret != sendlen) {
      DEBUG_PRINTLN(F("Failed to send packet."));
      return false;
    }
  }
  this->last_con_verify = millis();
  return true;
}

uint8_t Arduino_MQTT_Looped::connectPacket(void) {
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
    p = stringprint(p, this->will.topic.c_str());
    p = stringprint(p, this->will.payload.c_str());
  }

  p = stringprint(p, this->mqtt_user);
  p = stringprint(p, this->mqtt_pass);

  len = p - this->buffer;

  this->buffer[1] = len - 2; // don't include the 2 bytes of fixed header data
  DEBUG_PRINTLN(F("MQTT connect packet:"));
  DEBUG_PRINTBUFFER(this->buffer, len);
  return len;
}

uint16_t Arduino_MQTT_Looped::publishPacket(const char *topic, uint8_t *data, uint16_t bLen, uint8_t qos, bool retain) {
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

uint8_t Arduino_MQTT_Looped::subscribePacket(const char *topic, uint8_t qos) {
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
