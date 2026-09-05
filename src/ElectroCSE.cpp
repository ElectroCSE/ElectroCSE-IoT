/*
 * =============================================================================
 *  ElectroCSE IoT  —  the shared half, compiled once per sketch
 * =============================================================================
 *  WHAT IS AND IS NOT HERE
 *  -----------------------
 *  Here: registration, the timer wheel, the outgoing queue, the JSON body, the
 *  dispatch that clears the dashboard's "Pending" badge, and the loop that
 *  paces check-ins. None of it knows what a radio is.
 *
 *  Not here: ensureWiFi(), connectImpl() and checkIn(). Those are defined
 *  inline by whichever ElectroCSE_*.h the router picked - see
 *  ElectroCSE_Core.h for why the seam is header-only rather than one .cpp per
 *  board (short version: the Arduino builder compiles every .cpp under src/
 *  for every board, so a per-board .cpp would have to be #if'd into an empty
 *  file on the others).
 *
 *  This file therefore compiles unchanged on an ESP8266, an ESP32 and a Nano
 *  33 IoT, and including <ElectroCSE.h> below is what gives it the right
 *  transport for the board being built.
 * =============================================================================
 */

#include "ElectroCSE.h"

ElectroCseClass ElectroCSE;

/* ------------------------------------------------------------------ setup */

void ElectroCseClass::registerHandler(const char* channel, ElectroCseHandler fn) {
    if (_handlerCount >= ELECTROCSE_MAX_HANDLERS) {
        // Silently dropping it would mean a control on the dashboard that does
        // nothing, with nothing anywhere saying why.
        Serial.println(F("ElectroCSE: too many channels. Raise ELECTROCSE_MAX_HANDLERS."));
        return;
    }

    _handlers[_handlerCount].channel = channel;
    _handlers[_handlerCount].fn = fn;
    _handlerCount++;
}

void ElectroCseClass::registerLive(ElectroCseCallback fn) {
    _onLive = fn;
}

void ElectroCseClass::every(unsigned long intervalMs, ElectroCseCallback fn) {
    if (_timerCount >= ELECTROCSE_MAX_TIMERS) {
        Serial.println(F("ElectroCSE: too many timers. Raise ELECTROCSE_MAX_TIMERS."));
        return;
    }

    _timers[_timerCount].interval = intervalMs;
    // Staggered from now rather than from zero, so several timers created in
    // setup() do not all fire together on the first pass through loop().
    _timers[_timerCount].last = millis();
    _timers[_timerCount].fn = fn;
    _timerCount++;
}

/* ------------------------------------------------------------------ sending */

void ElectroCseClass::queue(const char* channel, const char* value,
                            const char* unit, bool isString) {
    if (_queuedCount >= ELECTROCSE_MAX_QUEUED) {
        // Flush rather than drop. A reading discarded here is a gap in somebody's
        // chart that nothing explains.
        checkIn();
    }

    if (_queuedCount >= ELECTROCSE_MAX_QUEUED) return;   // flush failed; give up

    Queued& q = _queued[_queuedCount++];

    strncpy(q.channel, channel, sizeof(q.channel) - 1);
    q.channel[sizeof(q.channel) - 1] = '\0';
    strncpy(q.value, value, sizeof(q.value) - 1);
    q.value[sizeof(q.value) - 1] = '\0';
    q.unit[0] = '\0';

    if (unit) {
        strncpy(q.unit, unit, sizeof(q.unit) - 1);
        q.unit[sizeof(q.unit) - 1] = '\0';
    }

    q.isString = isString;
}

/*
 * The three arms of send(). The public surface is TEMPLATED - see the traits at
 * the top of ElectroCSE_Core.h for why - so that no call site needs a cast;
 * every one of those templates lands in exactly one of these, which keeps the
 * formatting written once rather than once per argument type.
 */
void ElectroCseClass::sendFloat(const char* channel, double value, const char* unit) {
    char buf[24];
    /*
     * Width 0 so dtostrf does not pad. A non-zero width emits LEADING SPACES
     * inside what becomes a JSON number, and the dashboard rejects that as
     * malformed rather than reading it as whitespace.
     */
    dtostrf(value, 0, 3, buf);
    queue(channel, buf, unit, false);
}

void ElectroCseClass::sendSigned(const char* channel, long value, const char* unit) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%ld", value);
    queue(channel, buf, unit, false);
}

void ElectroCseClass::sendUnsigned(const char* channel, unsigned long value, const char* unit) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lu", value);
    queue(channel, buf, unit, false);
}

void ElectroCseClass::send(const char* channel, const char* value) {
    queue(channel, value, nullptr, true);
}

/* ---------------------------------------------------------------- dispatch */

/*
 * Record what was applied, so the next check-in echoes it back.
 *
 * The echo is what clears the dashboard's "Pending" badge. Doing it in the
 * library rather than asking the sketch to means it cannot be forgotten - and
 * forgetting it is the commonest first-project bug there is, presenting as a
 * card stuck on "Pending" for ever with nothing wrong anywhere else.
 *
 * Its own method because BOTH dispatch paths need it. When the catch-all was
 * added, the obvious shape was to leave this inline in the named-handler
 * branch and let the fallback return early - which would have made "Pending
 * never clears" the signature bug of every dynamically-configured board, for
 * the same reason and with the same invisibility.
 */
void ElectroCseClass::recordApplied(const char* channel, const String& value) {
    for (uint8_t j = 0; j < _appliedCount; j++) {
        if (strcmp(_applied[j].channel, channel) == 0) {
            strncpy(_applied[j].value, value.c_str(), sizeof(_applied[j].value) - 1);
            _applied[j].value[sizeof(_applied[j].value) - 1] = '\0';
            return;
        }
    }

    if (_appliedCount < ELECTROCSE_MAX_HANDLERS) {
        Queued& a = _applied[_appliedCount++];
        strncpy(a.channel, channel, sizeof(a.channel) - 1);
        a.channel[sizeof(a.channel) - 1] = '\0';
        strncpy(a.value, value.c_str(), sizeof(a.value) - 1);
        a.value[sizeof(a.value) - 1] = '\0';
        a.unit[0] = '\0';
        a.isString = false;
    }
}

void ElectroCseClass::onAny(ElectroCseAnyHandler fn) {
    _onAny = fn;
}

void ElectroCseClass::dispatch(const char* channel, const String& value) {
    for (uint8_t i = 0; i < _handlerCount; i++) {
        if (strcmp(_handlers[i].channel, channel) != 0) continue;

        _handlers[i].fn(ElectroCseParam(value));
        recordApplied(channel, value);

        return;
    }

    /*
     * Nothing named this channel. A catch-all gets it, WITH the name - which
     * is what lets a sketch whose channels are configured at run time work at
     * all. Named handlers are tried first, so this cannot change the behaviour
     * of a sketch that does not install one.
     */
    if (_onAny) {
        _onAny(channel, ElectroCseParam(value));
        recordApplied(channel, value);

        return;
    }

    Serial.print(F("ElectroCSE: no handler for channel "));
    Serial.println(channel);
}

/* ---------------------------------------------------------------- check-in */

String ElectroCseClass::buildBody() {
    /*
     * StaticJsonDocument is deprecated in ArduinoJson 7 and kept deliberately.
     * Its replacement, a bare JsonDocument, allocates from the heap - and this
     * runs on every check-in for the life of the board, which is the exact
     * allocate/free churn that fragments an ESP8266's ~40KB and stops it dead
     * after a couple of days. This one lives on the stack and costs nothing to
     * release. It also still compiles under ArduinoJson 6, which the README
     * promises. Do not "fix" the warning by dropping the size parameter.
     */
    StaticJsonDocument<640> doc;
    JsonArray readings = doc.createNestedArray("readings");

    for (uint8_t i = 0; i < _queuedCount; i++) {
        JsonObject r = readings.createNestedObject();
        r["channel"] = _queued[i].channel;

        if (_queued[i].isString) r["value_string"] = _queued[i].value;
        else                     r["value"] = atof(_queued[i].value);

        if (_queued[i].unit[0]) r["unit"] = _queued[i].unit;
    }

    if (_appliedCount) {
        JsonObject state = doc.createNestedObject("state");
        for (uint8_t i = 0; i < _appliedCount; i++) {
            state[_applied[i].channel] = atof(_applied[i].value);
        }
    }

    String body;
    serializeJson(doc, body);

    return body;
}

/* -------------------------------------------------------------------- loop */

void ElectroCseClass::loop() {
    if (WiFi.status() != WL_CONNECTED) { ensureWiFi(); return; }

    unsigned long now = millis();

    for (uint8_t i = 0; i < _timerCount; i++) {
        if (now - _timers[i].last >= _timers[i].interval) {
            _timers[i].last = now;
            _timers[i].fn();
        }
    }

    /*
     * Services MQTT: reconnects if needed, delivers incoming commands, and
     * publishes whatever send() has queued. Returns immediately on an HTTP
     * deployment, which is one comparison.
     */
    transportLoop();

    /*
     * On MQTT there is nothing to poll - readings went out above and commands
     * arrive on their own. The board is deliberately still WELCOME to fall back
     * here: transportLoop() clears _useMqtt after repeated broker failures and
     * zeroes _lastCheckInAt, so the very next pass resumes HTTP check-ins and
     * re-reads which transport it should be on.
     */
    if (_useMqtt) return;

    if (now - _lastCheckInAt >= _pollIntervalMs) {
        _lastCheckInAt = now;
        checkIn();
    }
}

bool ElectroCseClass::isLive() const {
    if (!_everConnected || WiFi.status() != WL_CONNECTED) return false;

    /*
     * ON MQTT, "live" MUST MEAN THE BROKER SESSION IS UP.
     *
     * This method answers "am I reaching the dashboard right now", and on the
     * HTTP path a joined WiFi plus one successful check-in is the whole of
     * that. On MQTT it is not: the broker can drop while WiFi stays perfectly
     * connected, and a board whose status LED is driven by isLive() would keep
     * insisting it was online with nothing arriving at the other end - which
     * is exactly the class of silent lie the rest of this library goes out of
     * its way to avoid.
     *
     * Note this stays true across a fallback to HTTP: _useMqtt is cleared
     * there, so the answer reverts to the HTTP meaning, which is then correct
     * again.
     */
    if (_useMqtt) return ecseMqttClient().connected();

    return true;
}

unsigned long ElectroCseClass::nextCheckInMs() const {
    unsigned long elapsed = millis() - _lastCheckInAt;

    return elapsed >= _pollIntervalMs ? 0 : _pollIntervalMs - elapsed;
}
