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

  WiFiClientSecure *_mqtt_client_secure =
      nullptr;            ///< Instance of secure WiFiClient
  bool _mode_set = false; ///< Whether WIFI_STA has been already selected

  const char *_aio_root_ca =
      "-----BEGIN CERTIFICATE-----\n"
      "MIIEjTCCA3WgAwIBAgIQDQd4KhM/xvmlcpbhMf/ReTANBgkqhkiG9w0BAQsFADBh\n"
      "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
      "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
      "MjAeFw0xNzExMDIxMjIzMzdaFw0yNzExMDIxMjIzMzdaMGAxCzAJBgNVBAYTAlVT\n"
      "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
      "b20xHzAdBgNVBAMTFkdlb1RydXN0IFRMUyBSU0EgQ0EgRzEwggEiMA0GCSqGSIb3\n"
      "DQEBAQUAA4IBDwAwggEKAoIBAQC+F+jsvikKy/65LWEx/TMkCDIuWegh1Ngwvm4Q\n"
      "yISgP7oU5d79eoySG3vOhC3w/3jEMuipoH1fBtp7m0tTpsYbAhch4XA7rfuD6whU\n"
      "gajeErLVxoiWMPkC/DnUvbgi74BJmdBiuGHQSd7LwsuXpTEGG9fYXcbTVN5SATYq\n"
      "DfbexbYxTMwVJWoVb6lrBEgM3gBBqiiAiy800xu1Nq07JdCIQkBsNpFtZbIZhsDS\n"
      "fzlGWP4wEmBQ3O67c+ZXkFr2DcrXBEtHam80Gp2SNhou2U5U7UesDL/xgLK6/0d7\n"
      "6TnEVMSUVJkZ8VeZr+IUIlvoLrtjLbqugb0T3OYXW+CQU0kBAgMBAAGjggFAMIIB\n"
      "PDAdBgNVHQ4EFgQUlE/UXYvkpOKmgP792PkA76O+AlcwHwYDVR0jBBgwFoAUTiJU\n"
      "IBiV5uNu5g/6+rkS7QYXjzkwDgYDVR0PAQH/BAQDAgGGMB0GA1UdJQQWMBQGCCsG\n"
      "AQUFBwMBBggrBgEFBQcDAjASBgNVHRMBAf8ECDAGAQH/AgEAMDQGCCsGAQUFBwEB\n"
      "BCgwJjAkBggrBgEFBQcwAYYYaHR0cDovL29jc3AuZGlnaWNlcnQuY29tMEIGA1Ud\n"
      "HwQ7MDkwN6A1oDOGMWh0dHA6Ly9jcmwzLmRpZ2ljZXJ0LmNvbS9EaWdpQ2VydEds\n"
      "b2JhbFJvb3RHMi5jcmwwPQYDVR0gBDYwNDAyBgRVHSAAMCowKAYIKwYBBQUHAgEW\n"
      "HGh0dHBzOi8vd3d3LmRpZ2ljZXJ0LmNvbS9DUFMwDQYJKoZIhvcNAQELBQADggEB\n"
      "AIIcBDqC6cWpyGUSXAjjAcYwsK4iiGF7KweG97i1RJz1kwZhRoo6orU1JtBYnjzB\n"
      "c4+/sXmnHJk3mlPyL1xuIAt9sMeC7+vreRIF5wFBC0MCN5sbHwhNN1JzKbifNeP5\n"
      "ozpZdQFmkCo+neBiKR6HqIA+LMTMCMMuv2khGGuPHmtDze4GmEGZtYLyF8EQpa5Y\n"
      "jPuV6k2Cr/N3XxFpT3hRpt/3usU/Zb9wfKPtWpoznZ4/44c1p9rzFcZYrWkj3A+7\n"
      "TNBJE0GmP2fhXhP1D/XVfIW/h0yCJGEiV9Glm/uGOa3DXHlmbAcxSyCRraG+ZBkA\n"
      "7h4SeM6Y8l/7MBRpPCz6l8Y=\n"
      "-----END CERTIFICATE-----\n"; ///< Root certificate for io.adafruit.com
};

#endif // ARDUINO_ARCH_ESP32
#endif // ADAFRUIT_MARQUEE_ESP32_H
