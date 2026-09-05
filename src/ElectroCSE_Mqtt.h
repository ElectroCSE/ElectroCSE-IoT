/*
 * =============================================================================
 *  ElectroCSE IoT  —  the MQTT engine, shared by every board
 * =============================================================================
 *  Do not include this directly. Each board header includes it, after saying
 *  which two network client classes its radio provides:
 *
 *      typedef WiFiClient       ElectroCsePlainNet;
 *      typedef WiFiClientSecure ElectroCseSecureNet;
 *      inline void ecseMqttPrepareTls(ElectroCseSecureNet& net) { ... }
 *      #include "ElectroCSE_Mqtt.h"
 *
 *  Those three lines are the ONLY thing that differs between an ESP8266, an
 *  ESP32 and a WiFiNINA board here, which is why this is one file and not
 *  three. The protocol is identical on all of them; only the TLS client class
 *  and how it is talked into trusting a certificate are not.
 *
 *  WHY THERE IS NO #define IN ANY SKETCH SELECTING THIS
 *  ----------------------------------------------------
 *  The server decides, on the first check-in, and the board obeys - see
 *  ElectroCseClass::applyTransport(). A sketch written for this library is
 *  byte-for-byte identical on an HTTP deployment and an MQTT one, which is the
 *  entire point: a beginner should not have to know what a broker is, and an
 *  operator should not have to re-flash a field of boards to change their mind.
 *
 *  WHY FUNCTION-LOCAL STATICS AND NOT FILE-SCOPE OBJECTS
 *  ----------------------------------------------------
 *  This header is compiled into TWO translation units - the sketch, and
 *  src/ElectroCSE.cpp, which includes <ElectroCSE.h> to get the right seam.
 *  A file-scope `static PubSubClient` therefore becomes two different objects
 *  with internal linkage, while the inline member functions below are merged
 *  into one - so the merged function would refer to whichever copy the linker
 *  happened to keep. That is an ODR violation, and its symptom is a board that
 *  subscribes on one client object and pumps another: connected, subscribed,
 *  and no command ever arrives.
 *
 *  A static INSIDE an inline function is guaranteed to be one object across
 *  every translation unit. That guarantee is the whole reason for the accessor
 *  functions, so do not "simplify" them into plain globals.
 * =============================================================================
 */

#ifndef ELECTROCSE_MQTT_H
#define ELECTROCSE_MQTT_H

#include <PubSubClient.h>

/*
 * How many failed broker connections before the board gives up and goes back
 * to HTTP.
 *
 * FALLING BACK IS A FEATURE, NOT A SAFETY NET. Two things make it earn its
 * place. A broker that dies takes every device with it, and a board that
 * quietly returns to HTTP keeps reporting - slower, but a slow dashboard beats
 * a dark one. And because the fallback check-in re-reads the `transport` block,
 * it is also how a board LEARNS that an operator has switched the deployment
 * to HTTP: without it, that switch would strand every board on a broker
 * nobody is listening to any more, and the only cure would be re-flashing.
 *
 * Five, not one: a broker restart or a flapping router should be ridden out on
 * the fast transport rather than answered by abandoning it.
 */
#ifndef ELECTROCSE_MQTT_MAX_ATTEMPTS
  #define ELECTROCSE_MQTT_MAX_ATTEMPTS 5
#endif

/* --------------------------------------------------------------------------
 * The single instances. See the ODR note in the file header.
 * ----------------------------------------------------------------------- */

inline ElectroCsePlainNet& ecseMqttPlainNet() {
    static ElectroCsePlainNet net;
    return net;
}

inline ElectroCseSecureNet& ecseMqttSecureNet() {
    static ElectroCseSecureNet net;
    return net;
}

inline PubSubClient& ecseMqttClient() {
    static PubSubClient client;
    return client;
}

/*
 * PubSubClient takes a plain C function pointer, so an incoming message cannot
 * be delivered straight to a member function. This is the one line of glue.
 */
inline void ecseMqttTrampoline(char* topic, uint8_t* payload, unsigned int length) {
    ElectroCSE.onMqttMessage(topic, payload, length);
}

/* --------------------------------------------------------------------------
 * The seam
 * ----------------------------------------------------------------------- */

inline const char* ElectroCseClass::transport() const {
    return _useMqtt ? "mqtt" : "http";
}

inline void ElectroCseClass::applyTransport(JsonObjectConst transport) {
    /*
     * No `transport` key at all: a server older than this feature, which is
     * every standalone sketch's server and was this one until today. Staying
     * on HTTP is the only answer that cannot break an existing install.
     */
    if (transport.isNull()) return;

    const char* protocol = transport["protocol"] | "http";

    if (strcmp(protocol, "mqtt") != 0) return;      // told to stay on HTTP
    if (_useMqtt) return;                           // already moved

    JsonObjectConst mqtt = transport["mqtt"];
    if (mqtt.isNull()) return;

    const char* host = mqtt["host"] | "";
    const char* prefix = mqtt["topic_prefix"] | "";
    const char* clientId = mqtt["client_id"] | "";
    const char* user = mqtt["username"] | "";

    /*
     * Refuse a half-populated block rather than connecting with an empty topic
     * prefix. Publishing to "/up/relay" would be accepted by the broker and
     * matched by no subscription on the server, so the board would look
     * perfectly healthy and its readings would go nowhere.
     */
    if (!*host || !*prefix || !*clientId || !*user) {
        Serial.println(F("ElectroCSE: server offered MQTT with fields missing - staying on HTTP."));
        return;
    }

    strncpy(_mqttHost, host, sizeof(_mqttHost) - 1);
    strncpy(_mqttPrefix, prefix, sizeof(_mqttPrefix) - 1);
    strncpy(_mqttClientId, clientId, sizeof(_mqttClientId) - 1);
    strncpy(_mqttUser, user, sizeof(_mqttUser) - 1);

    _mqttPort = (uint16_t) (mqtt["port"] | 1883);
    _mqttTls = mqtt["tls"] | false;

    _useMqtt = true;
    _mqttAttempts = 0;
    _mqttRetryFrom = millis();
    _mqttRetryMs = 0;

    if (!_mqttAnnounced) {
        _mqttAnnounced = true;
        Serial.print(F("ElectroCSE: switching to MQTT at "));
        Serial.print(_mqttHost);
        Serial.print(':');
        Serial.print(_mqttPort);
        Serial.println(_mqttTls ? F(" (TLS)") : F(" (plain)"));
    }
}

inline void ElectroCseClass::mqttConnect() {
    PubSubClient& mqtt = ecseMqttClient();

    /*
     * TLS is a RUNTIME fact here - the server said so - which is why both
     * client objects exist and one is chosen rather than the choice being
     * compiled in. Only the selected one ever opens a socket.
     */
    if (_mqttTls) {
        ecseMqttPrepareTls(ecseMqttSecureNet());
        mqtt.setClient(ecseMqttSecureNet());
    } else {
        mqtt.setClient(ecseMqttPlainNet());
    }

    mqtt.setServer(_mqttHost, _mqttPort);
    mqtt.setCallback(ecseMqttTrampoline);

    // Before connect(): setBufferSize reallocates, and PubSubClient's 128-byte
    // default silently DROPS anything larger. See ELECTROCSE_MQTT_BUFFER.
    mqtt.setBufferSize(ELECTROCSE_MQTT_BUFFER);
    mqtt.setKeepAlive(60);

    char statusTopic[ELECTROCSE_MQTT_PREFIX_LEN + 16];
    snprintf(statusTopic, sizeof(statusTopic), "%s/status", _mqttPrefix);

    /*
     * The last four arguments are the "last will": what the broker publishes on
     * this board's behalf if it vanishes without saying goodbye - a power cut, a
     * crash, a flat battery. Retained, so the dashboard sees it whenever it next
     * looks rather than only if it happened to be listening at the time.
     *
     * Username is the device uuid and password is the same token the HTTP path
     * uses; the server checks that pair in BrokerAuthController.
     */
    bool ok = mqtt.connect(_mqttClientId, _mqttUser, _token,
                           statusTopic, 1, true, "offline");

    if (!ok) {
        _mqttAttempts++;

        /*
         * Exponential backoff, capped. A board that retries every second
         * against a broker that is refusing its token is a board generating
         * load and log noise for as long as it is powered.
         */
        unsigned long wait = 1000UL << (_mqttAttempts > 5 ? 5 : _mqttAttempts - 1);
        if (wait > 30000UL) wait = 30000UL;

        // Start + duration, not a deadline - see the note on these members.
        _mqttRetryFrom = millis();
        _mqttRetryMs = wait;

        Serial.print(F("ElectroCSE: broker refused us, rc="));
        Serial.print(mqtt.state());
        Serial.print(F(" (attempt "));
        Serial.print(_mqttAttempts);
        Serial.print('/');
        Serial.print(ELECTROCSE_MQTT_MAX_ATTEMPTS);
        Serial.println(')');

        if (_mqttAttempts >= ELECTROCSE_MQTT_MAX_ATTEMPTS) {
            /*
             * Back to HTTP. The next check-in re-reads the transport block, so
             * this is self-healing in both directions: if the broker comes
             * back, so does MQTT; if the operator has switched the deployment
             * to HTTP, this is how the board finds out.
             */
            _useMqtt = false;
            _mqttAttempts = 0;
            _lastCheckInAt = 0;              // check in immediately, not in 5s
            Serial.println(F("ElectroCSE: falling back to HTTP. Readings keep flowing, "
                             "commands will be slower. Retrying MQTT on the next check-in."));
        }
        return;
    }

    _mqttAttempts = 0;

    mqtt.publish(statusTopic, "online", true);

    char downTopic[ELECTROCSE_MQTT_PREFIX_LEN + 16];
    snprintf(downTopic, sizeof(downTopic), "%s/down/#", _mqttPrefix);
    mqtt.subscribe(downTopic, 1);

    Serial.println(F("ElectroCSE: broker connected."));

    if (!_everConnected) {
        _everConnected = true;
        if (_onLive && !_announcedLive) { _announcedLive = true; _onLive(); }
    }
}

inline void ElectroCseClass::onMqttMessage(char* topic, const uint8_t* payload, unsigned int length) {
    /*
     * The channel is the last segment: "iot/8/<uuid>/down/relay" -> "relay".
     * Taken from the RIGHT rather than by counting from the left, because the
     * prefix has a variable number of segments and a uuid is not one of them
     * on every deployment.
     */
    const char* slash = strrchr(topic, '/');
    if (!slash) return;
    const char* channel = slash + 1;

    char value[24] = {0};
    unsigned int n = length < sizeof(value) - 1 ? length : sizeof(value) - 1;
    memcpy(value, payload, n);

    // Runs the ELECTROCSE_LISTEN block and records what was applied.
    dispatch(channel, String(value));

    /*
     * Echo immediately - THIS is what clears the dashboard's "Pending" badge.
     *
     * On HTTP the echo rides the next check-in, because there is a request going
     * anyway. Here there is not, and waiting for the next publish would leave a
     * card showing Pending for up to a second after the board had already acted
     * - which reads as the very latency MQTT was chosen to remove.
     *
     * The RAW value is published, not a JSON wrapper. The server's parsePayload
     * puts a bare numeric payload in `value` and anything else in
     * `value_string`, which is exactly right, and it means a text value needs no
     * escaping to be safe here.
     */
    char echoTopic[ELECTROCSE_MQTT_PREFIX_LEN + 40];
    snprintf(echoTopic, sizeof(echoTopic), "%s/up/%s", _mqttPrefix, channel);
    ecseMqttClient().publish(echoTopic, value);
}

inline void ElectroCseClass::mqttFlush() {
    if (_queuedCount == 0) return;

    unsigned long now = millis();
    if (now - _mqttLastPubAt < ELECTROCSE_MQTT_MIN_PUBLISH_MS) return;
    _mqttLastPubAt = now;

    PubSubClient& mqtt = ecseMqttClient();

    for (uint8_t i = 0; i < _queuedCount; i++) {
        Queued& q = _queued[i];

        /*
         * ONE PUBLISH PER CHANNEL, to `<prefix>/up/<channel>`, carrying an
         * object with an explicit `value`.
         *
         * The obvious alternative is one publish of a flat map to
         * `<prefix>/up` - fewer messages, and what the standalone sketch does.
         * It cannot carry a UNIT: the server reads a flat map as
         * channel-name -> value and has nowhere to put "C". send() takes a unit
         * on both transports, so a sketch that says send("temp", 24.5, "C")
         * would silently lose it on MQTT and keep it on HTTP - the same code
         * behaving differently on two deployments, which is precisely what this
         * library exists to prevent.
         */
        StaticJsonDocument<128> doc;

        if (q.isString) doc["value"] = q.value;
        else            doc["value"] = atof(q.value);

        if (q.unit[0]) doc["unit"] = q.unit;

        char body[128];
        size_t len = serializeJson(doc, body, sizeof(body));

        char topic[ELECTROCSE_MQTT_PREFIX_LEN + 40];
        snprintf(topic, sizeof(topic), "%s/up/%s", _mqttPrefix, q.channel);

        mqtt.publish(topic, (const uint8_t*) body, len, false);
    }

    _queuedCount = 0;
}

inline void ElectroCseClass::transportLoop() {
    if (!_useMqtt) return;                       // HTTP: one comparison, no cost

    PubSubClient& mqtt = ecseMqttClient();

    if (!mqtt.connected()) {
        /*
         * Non-blocking, unlike the delay(5000) a hand-written sketch uses. This
         * runs inside the caller's loop(), so blocking here would stall their
         * timers and every ELECTROCSE_LISTEN block with it.
         */
        if (millis() - _mqttRetryFrom < _mqttRetryMs) return;
        mqttConnect();
        return;
    }

    // Must run often - this is what actually delivers incoming commands.
    mqtt.loop();

    mqttFlush();
}

#endif  // ELECTROCSE_MQTT_H
