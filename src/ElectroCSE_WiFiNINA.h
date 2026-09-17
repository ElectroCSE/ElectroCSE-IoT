/*
 * =============================================================================
 *  ElectroCSE IoT  —  WiFiNINA transport
 * =============================================================================
 *  Nano 33 IoT · MKR WiFi 1010 · Nano RP2040 Connect
 *
 *  Do not include this directly. <ElectroCSE.h> routes here on SAMD, SAM and
 *  mbed architectures.
 *
 *  INSTALL FIRST  (Arduino IDE -> Tools -> Manage Libraries...)
 *
 *    WiFiNINA           by Arduino
 *    ArduinoHttpClient  by Arduino
 *    ArduinoJson        by Benoit Blanchon
 *
 *  Three, where an ESP needs one. Unlike Espressif's cores, the Arduino SAMD
 *  core ships no networking at all - the radio is a separate NINA-W102
 *  co-processor, and both libraries above are the drivers for it.
 *
 * -----------------------------------------------------------------------------
 *  FOUR WAYS THIS IS NOT THE ESP FILE WITH THE NAMES CHANGED
 * -----------------------------------------------------------------------------
 *  1. TLS IS A CLASS, NOT A MODE. An ESP uses one WiFiClientSecure and calls
 *     setInsecure() to skip validation. Here, WiFiClient is plaintext and
 *     WiFiSSLClient is TLS, chosen at construction - and WiFiSSLClient cannot
 *     be told to skip validation at all. It checks against a root store burned
 *     into the module's firmware.
 *
 *     The consequence is the opposite of the ESP's and it surprises people: a
 *     private server with a self-signed certificate simply will not connect,
 *     and there is no setInsecure() to reach for. Test against plain http, or
 *     put a real certificate on the host, or reflash the NINA's certificate
 *     bundle with Arduino's own tool.
 *
 *  2. HOST AND PORT ARE SEPARATE. ArduinoHttpClient takes them apart, where
 *     HTTPClient::begin() takes a whole URL. So this file parses the server
 *     string itself - see splitServer() - instead of handing it over.
 *
 *  3. THE REQUEST IS ASSEMBLED BY HAND. There is no POST(body) that sets
 *     Content-Length for you: beginRequest / sendHeader / beginBody / print /
 *     endRequest. Omitting Content-Length is the classic failure here, and it
 *     does not error - the server reads a zero-length body and stores nothing,
 *     so the board reports HTTP 200 and the dashboard stays empty.
 *
 *  4. NO dtostrf ON EVERY CORE. The shared .cpp needs it to format a float
 *     without pulling in printf's float support, and the SAMD core hides it
 *     behind a compatibility header that is not included by default. Pulled in
 *     below so the core file does not have to know which board it is on.
 * =============================================================================
 */

#ifndef ELECTROCSE_WIFININA_H
#define ELECTROCSE_WIFININA_H

#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>

/*
 * dtostrf lives in an AVR compatibility shim on SAMD and is not included by
 * Arduino.h. Without this the shared send(float, ...) fails to compile with
 * "dtostrf was not declared in this scope", pointing at ElectroCSE.cpp - a
 * file whose contents have nothing to do with the board being built.
 *
 * Guarded because the mbed core (Nano RP2040 Connect) already provides it and
 * has no such header to include.
 */
#if defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_SAM)
  #include <avr/dtostrf.h>
#endif

/* -------------------------------------------------------------------------- */
/*  Helpers                                                                   */
/* -------------------------------------------------------------------------- */

/**
 * Split "https://host:1234" into its parts.
 *
 * `static` at namespace scope, so each translation unit that includes this
 * header gets its own copy and there is nothing to collide at link time - the
 * same job `inline` does for the member functions below.
 *
 * A bare hostname is TLS, matching the ESP transport and the documented
 * contract: `iot.electrocse.com` and `https://iot.electrocse.com` are the same
 * address, while `http://iot.electrocse.com` is deliberately not.
 */
static void ecseSplitServer(const String& server, String& host, uint16_t& port, bool& tls) {
    String rest = server;

    tls = true;

    if (rest.startsWith("https://")) {
        rest = rest.substring(8);
    } else if (rest.startsWith("http://")) {
        tls = false;
        rest = rest.substring(7);
    }

    // Trim a trailing slash so "http://host/" does not become host "host/".
    while (rest.endsWith("/")) rest = rest.substring(0, rest.length() - 1);

    const int colon = rest.indexOf(':');

    if (colon >= 0) {
        port = (uint16_t) rest.substring(colon + 1).toInt();
        host = rest.substring(0, colon);
    } else {
        port = tls ? 443 : 80;
        host = rest;
    }
}

/* -------------------------------------------------------------------------- */
/*  What this radio calls its two network clients                             */
/* -------------------------------------------------------------------------- */

/*
 * The MQTT engine is identical on every board; only these differ. Declaring
 * them here and including the engine below is the whole of this file's part in
 * supporting MQTT - see ElectroCSE_Mqtt.h.
 */
typedef WiFiClient    ElectroCsePlainNet;
typedef WiFiSSLClient ElectroCseSecureNet;

/*
 * Deliberately empty, and NOT an oversight - see note 1 in the file header.
 *
 * On this radio TLS is a CLASS, not a mode: WiFiSSLClient validates against the
 * root store burned into the NINA module and there is no setInsecure() to call.
 * So a broker with a self-signed or private certificate cannot be reached from
 * these boards at all, where an ESP would shrug and connect - the ESP's version
 * of this function is the one doing something, and this one has nothing it is
 * permitted to do.
 */
inline void ecseMqttPrepareTls(ElectroCseSecureNet&) {}

#include "ElectroCSE_Mqtt.h"

/* -------------------------------------------------------------------------- */
/*  The three radio methods                                                   */
/* -------------------------------------------------------------------------- */

/*
 * `inline` on every one of these is load-bearing. These are member function
 * definitions in a header; without it, the second .cpp in a sketch produces
 * "multiple definition of ElectroCseClass::checkIn()".
 */

inline void ElectroCseClass::ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    WiFi.begin(_ssid, _pass);
}

inline bool ElectroCseClass::connectImpl(const char* token,
                                         const char* ssid,
                                         const char* pass,
                                         unsigned long timeoutMs) {
    _token = token;
    _ssid = ssid;
    _pass = pass;

    /*
     * Is the radio even there?
     *
     * On an ESP the WiFi is the same chip as the CPU, so it cannot be absent.
     * Here it is a separate module over SPI, and a board with an old NINA
     * firmware or a bad solder joint reports NO_MODULE - at which point every
     * later call quietly does nothing and the sketch looks like a WiFi
     * password problem for as long as somebody is willing to retype it.
     */
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println(F("ElectroCSE: no WiFi module found. This board's NINA radio is not responding."));
        return false;
    }

    Serial.print(F("ElectroCSE: connecting to "));
    Serial.print(ssid);

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

    String host;
    uint16_t port;
    bool tls;
    ecseSplitServer(String(_server), host, port, tls);

    /*
     * Both clients are constructed and one is used.
     *
     * They are stack objects with no constructor side effects, so the unused
     * one costs a few bytes of stack and nothing else - and a HttpClient holds
     * a Client REFERENCE, so whichever it is given has to outlive the request.
     * Declaring them here rather than inside the branch is what guarantees
     * that; a client created inside an if() would be destroyed before the
     * response was read, which presents as a connection that closes mid-reply.
     */
    WiFiSSLClient secure;
    WiFiClient plain;

    HttpClient http = tls ? HttpClient(secure, host.c_str(), port)
                          : HttpClient(plain, host.c_str(), port);

    /*
     * Printed on the FIRST check-in only. The address is the one thing a
     * sketch can get wrong while every other signal looks healthy.
     */
    if (!_everConnected && !_announcedUrl) {
        _announcedUrl = true;
        Serial.print(F("ElectroCSE: posting to "));
        Serial.print(tls ? F("https://") : F("http://"));
        Serial.print(host);
        Serial.print(':');
        Serial.print(port);
        Serial.println(_path);
    }

    const String body = buildBody();

    /*
     * Assembled by hand, because ArduinoHttpClient has no "POST this body and
     * set the headers" call.
     *
     * Content-Length is the line that matters. Leave it out and the request is
     * still well-formed and still answered 200 - the server simply reads an
     * empty body and stores nothing, so the board reports success while the
     * dashboard stays blank. There is no error anywhere to find.
     */
    http.setTimeout(15000);
    http.beginRequest();
    http.post(_path);
    http.sendHeader("Content-Type", "application/json");
    http.sendHeader("Accept", "application/json");
    http.sendHeader("Content-Length", body.length());
    http.sendHeader("Authorization", String("Bearer ") + _token);
    http.beginBody();
    http.print(body);
    http.endRequest();

    const int status = http.responseStatusCode();

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
        DeserializationError err = deserializeJson(reply, http.responseBody());
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

        http.stop();
        return true;
    }

    if (status == 401) {
        Serial.println(F("ElectroCSE: token rejected. It is wrong, revoked, or expired."));
        _pollIntervalMs = 60000;
    } else if (status == 429) {
        Serial.println(F("ElectroCSE: rate limited; slowing down."));
        _pollIntervalMs = 60000;
    } else {
        /*
         * A NEGATIVE status is ArduinoHttpClient's own, not the server's:
         * -1 timeout, -2 invalid response, -3 connection failed. On TLS the
         * commonest is -3 against a certificate the NINA's root store does not
         * carry - which is exactly the self-signed case point 1 above warns
         * about, and it looks nothing like a certificate error from here.
         */
        if (_failures < 5) _failures++;
        _pollIntervalMs = min(5000UL * (1UL << _failures), 300000UL);
        Serial.print(F("ElectroCSE: check-in failed, HTTP "));
        Serial.println(status);

        if (status < 0 && tls) {
            Serial.println(F("ElectroCSE: negative status on TLS usually means the certificate is not trusted by this module."));
        }
    }

    http.stop();
    return false;
}

#endif  // ELECTROCSE_WIFININA_H
