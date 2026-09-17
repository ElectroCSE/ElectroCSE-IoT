/*
 * =============================================================================
 *  ElectroCSE IoT  —  Espressif transport  (ESP8266 / ESP32)
 * =============================================================================
 *  Do not include this directly. <ElectroCSE.h> routes here when the board
 *  selected under Tools -> Board is an ESP8266 or an ESP32.
 *
 *  ONE FILE FOR BOTH CHIPS, and that is a considered choice rather than
 *  laziness. Between an ESP8266 and an ESP32 this library differs in exactly
 *  which three headers the core publishes; the connect logic, the TLS posture,
 *  the request and the response handling are identical. Two files would be two
 *  copies of ninety lines that drift, and the drift would be invisible until
 *  somebody's ESP32 stopped honouring next_poll_ms.
 *
 *  WHAT THIS FILE OWES THE CORE: ensureWiFi(), connectImpl(), checkIn().
 *  Nothing else. See ElectroCSE_Core.h for the seam.
 *
 *  EVERYTHING IT INCLUDES SHIPS WITH THE BOARD PACKAGE. There is nothing to
 *  install for the network side - ArduinoJson is the one library you must add
 *  yourself, and installing ElectroCSE from the Library Manager pulls it in.
 * =============================================================================
 */

#ifndef ELECTROCSE_ESP_H
#define ELECTROCSE_ESP_H

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <WiFiClientSecure.h>
#elif defined(ESP32)
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
#else
  #error "ElectroCSE_ESP.h was included for a board that is neither ESP8266 nor ESP32. Include <ElectroCSE.h> and let it route."
#endif

/* -------------------------------------------------------------------------- */
/*  What this radio calls its two network clients                             */
/* -------------------------------------------------------------------------- */

/*
 * The MQTT engine is identical on every board; only these differ. Declaring
 * them here and including the engine below is the whole of this file's part in
 * supporting MQTT - see ElectroCSE_Mqtt.h.
 */
typedef WiFiClient       ElectroCsePlainNet;
typedef WiFiClientSecure ElectroCseSecureNet;

/*
 * setInsecure() encrypts the link without verifying the certificate, so it
 * stops passive sniffing and not an active man-in-the-middle - the same trade
 * checkIn() makes below, made in the same place for the same reason. Pinning
 * ISRG Root X1 is the upgrade and additionally needs NTP time on the board,
 * because a certificate cannot be date-checked without a clock.
 */
inline void ecseMqttPrepareTls(ElectroCseSecureNet& net) {
    net.setInsecure();
}

#include "ElectroCSE_Mqtt.h"

/* -------------------------------------------------------------------------- */
/*  The three radio methods                                                   */
/* -------------------------------------------------------------------------- */

/*
 * `inline` on every one of these is load-bearing, not decoration.
 *
 * These are member function definitions in a header. Without `inline` the
 * definition is emitted into every translation unit that includes it, and the
 * moment a sketch has a second .cpp the link fails with "multiple definition
 * of ElectroCseClass::checkIn()" - an error that names the library and points
 * at a file the user did not write.
 */

inline void ElectroCseClass::ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _pass);
}

inline bool ElectroCseClass::connectImpl(const char* token,
                                         const char* ssid,
                                         const char* pass,
                                         unsigned long timeoutMs) {
    _token = token;
    _ssid = ssid;
    _pass = pass;

    Serial.print(F("ElectroCSE: connecting to "));
    Serial.print(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    unsigned long startedAt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startedAt < timeoutMs) {
        delay(300);
        Serial.print('.');
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("\nElectroCSE: WiFi failed. Check the name and password."));
        return false;
    }

    Serial.print(F("\nElectroCSE: WiFi ok, IP "));
    Serial.println(WiFi.localIP());

    // Check in once immediately, so the dashboard shows the board as online
    // without waiting out a poll interval.
    return checkIn();
}

inline bool ElectroCseClass::checkIn() {
    if (WiFi.status() != WL_CONNECTED) { ensureWiFi(); return false; }

    WiFiClientSecure secure;
    WiFiClient plain;

    const bool https = String(_server).indexOf("://") == -1
        ? true                         // bare hostname: assume the real thing
        : String(_server).startsWith("https://");

    /*
     * setInsecure() encrypts the link but does not verify the certificate, so
     * it stops passive sniffing and not an active man-in-the-middle. Pinning
     * ISRG Root X1 is the upgrade and additionally needs NTP time on the board,
     * because a certificate cannot be date-checked without a clock.
     *
     * There is deliberately no setBufferSizes() call on the ESP8266. BearSSL
     * defaults to a 16KB receive buffer, which costs heap but holds any TLS
     * record a server can send; shrinking it only works when the server
     * negotiates max_fragment_length, and when it does not the handshake fails
     * with no useful error at all.
     */
    if (https) secure.setInsecure();

    String url = String(_server);
    if (url.indexOf("://") == -1) url = (https ? "https://" : "http://") + url;
    url += _path;

    /*
     * Printed on the FIRST check-in only.
     *
     * The address is the one thing about this library a sketch can get wrong
     * while every other signal looks healthy - WiFi connects, the board says
     * nothing is amiss, and the requests go somewhere else entirely. One line
     * on Serial turns that from a packet capture into a glance.
     */
    if (!_everConnected && !_announcedUrl) {
        _announcedUrl = true;
        Serial.print(F("ElectroCSE: posting to "));
        Serial.println(url);
    }

    HTTPClient http;
    bool opened = https ? http.begin(secure, url) : http.begin(plain, url);
    if (!opened) return false;

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("Authorization", String("Bearer ") + _token);
    http.setTimeout(15000);

    int status = http.POST(buildBody());

    if (status == 200 || status == 201) {
        _queuedCount = 0;
        _failures = 0;

        /*
         * 1024, not 768. The reply carries a `transport` block now, and
         * ArduinoJson does not error on a document that is too small - it
         * silently truncates, so the symptom would be a board that ignores the
         * broker it was just told about and polls for ever.
         */
        ELECTROCSE_JSON_QUIET_BEGIN
        StaticJsonDocument<1024> reply;
        DeserializationError err = deserializeJson(reply, http.getString());
        ELECTROCSE_JSON_QUIET_END

        /*
         * NoMemory is called out by name because it fails in the most
         * confusing possible direction: the check-in SUCCEEDED, the readings
         * were stored, and the board then silently discards the whole reply -
         * commands, next_poll_ms and the transport block together. So the
         * dashboard fills with data while every button does nothing, which
         * reads as the commands being broken rather than as one number here
         * being too small.
         *
         * If this ever fires, raise the size above; it is on the stack, and an
         * ESP8266's is only about 4KB, which is why it is not simply doubled
         * as a precaution.
         */
        if (err == DeserializationError::NoMemory) {
            Serial.println(F("ElectroCSE: reply too large to parse. Commands ignored. "
                             "Raise the StaticJsonDocument size in the transport header."));
        }

        if (err == DeserializationError::Ok) {
            JsonObject commands = reply["commands"];
            for (JsonPair item : commands) {
                dispatch(item.key().c_str(), item.value().as<String>());
            }

            // The server sets the pace: it knows whether somebody is watching
            // the dashboard right now, and this board does not.
            unsigned long next = reply["next_poll_ms"] | _pollIntervalMs;
            _pollIntervalMs = constrain(next, 500UL, 300000UL);

            // ...and the server also says WHICH TRANSPORT to be on. This is
            // what moves the board onto MQTT without a line in the sketch.
            applyTransport(reply["transport"].as<JsonObjectConst>());
        }

        if (!_everConnected) {
            _everConnected = true;
            if (_onLive && !_announcedLive) { _announcedLive = true; _onLive(); }
        }

        http.end();
        return true;
    }

    if (status == 401) {
        Serial.println(F("ElectroCSE: token rejected. It is wrong, revoked, or expired."));
        _pollIntervalMs = 60000;
    } else if (status == 429) {
        Serial.println(F("ElectroCSE: rate limited; slowing down."));
        _pollIntervalMs = 60000;
    } else {
        // Back off further on each failure, capped. A server that is struggling
        // should not be hit harder because it is struggling.
        if (_failures < 5) _failures++;
        _pollIntervalMs = min(5000UL * (1UL << _failures), 300000UL);
        Serial.print(F("ElectroCSE: check-in failed, HTTP "));
        Serial.println(status);
    }

    http.end();
    return false;
}

#endif  // ELECTROCSE_ESP_H
