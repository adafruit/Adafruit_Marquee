/*!
 * @file Adafruit_Marquee_ESP32.h
 *
 * Network adapter for the ESP32
 *
 * MIT license, all text here must be included in any redistribution.
 */

#ifndef ADAFRUIT_MARQUEE_ESP32_H
#define ADAFRUIT_MARQUEE_ESP32_H

#ifdef ARDUINO_ARCH_ESP32

#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include "Adafruit_Marquee.h"
#include "Arduino.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>

/*!
    @brief  Class for using the ESP32 network adapter.
*/
class Adafruit_Marquee_ESP32 : public Adafruit_Marquee {
public:
  /*!
      @brief  Initializes the Marquee client for ESP32.
  */
  Adafruit_Marquee_ESP32() : Adafruit_Marquee() {
    _mqtt_client_secure = new WiFiClientSecure;
    if (_mqtt_client_secure)
      _mqtt_client_secure->setCACert(_aio_root_ca);
  }

  /*!
      @brief  Destructor.
  */
  ~Adafruit_Marquee_ESP32() {
    if (_mqtt_client_secure)
      delete _mqtt_client_secure;
  }

  /*!
      @brief  Whether the station holds an association.
      @return True if associated, else False.
  */
  bool isNetConnected() { return WiFi.status() == WL_CONNECTED; }

  /*!
      @brief  Returns the type of network connection used by Marquee.
      @return "wifi"
  */
  const char *connectionType() { return "wifi"; }


  /*!
      @brief  Constructs the secure MQTT client
  */
  void setupMQTTClient() {
    if (!_mqtt_client_secure)
      return;

    _mqtt = new Adafruit_MQTT_Client(_mqtt_client_secure, MQ_IO_HOST,
                                     MQ_IO_MQTT_PORT, "", _aio_username,
                                     _aio_key, MQ_MQTT_BUFFER_LEN);
  }

protected:
  /*!
      @brief  Attempts to connect to the wireless network.
  */
  void _connect() {
    if (!_mode_set) {
      WiFi.mode(WIFI_STA);
      _mode_set = true;
    }
    _disconnect();
    WiFi.begin(_ssid, _pass);
  }

  /*!
      @brief  Disconnects from the wireless network.
  */
  void _disconnect() { WiFi.disconnect(); }

  WiFiClientSecure *_mqtt_client_secure = nullptr; ///< Instance of secure WiFiClient
  bool _mode_set = false; ///< Whether WIFI_STA has been already selected

  const char *_aio_root_ca =
      "-----BEGIN CERTIFICATE-----\n"
      "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\n"
      "-----END CERTIFICATE-----\n"; ///< Root certificate for io.adafruit.us
};

#endif // ARDUINO_ARCH_ESP32
#endif // ADAFRUIT_MARQUEE_ESP32_H
