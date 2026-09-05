/*
 * =============================================================================
 *  ElectroCSE IoT  —  the transport-independent half
 * =============================================================================
 *  Do not include this directly. Include <ElectroCSE.h>, which routes to the
 *  transport for your board and pulls this in first.
 *
 *  WHAT LIVES HERE
 *  ---------------
 *  Everything that is the same whatever radio the board has: the class, the
 *  parameter reader, the handler and timer tables, the queue, the echo table
 *  that clears the dashboard's "Pending" badge, and the two macros a sketch
 *  actually writes.
 *
 *  WHAT DOES NOT
 *  -------------
 *  Four methods. The first three are the RADIO seam and each board header
 *  defines them inline:
 *
 *      void ensureWiFi();
 *      bool connectImpl(token, ssid, pass, timeoutMs);
 *      bool checkIn();
 *
 *  The fourth is the PROTOCOL seam, defined once in ElectroCSE_Mqtt.h, which
 *  every board header includes:
 *
 *      void transportLoop();
 *
 *  AN EARLIER VERSION OF THIS COMMENT SAID A FOURTH METHOD WOULD MEAN THE
 *  SPLIT WAS IN THE WRONG PLACE. It was half right, and the half it got wrong
 *  is worth keeping. The original seam described a RADIO - three ways to say
 *  "this board's WiFi" - and adding a fourth radio method would indeed have
 *  been a smell. What arrived instead was a second, ORTHOGONAL axis: the
 *  protocol. HTTP is request/response and fits "post a body, read commands
 *  back" exactly; MQTT is a subscription that must be pumped, and has no
 *  request to hang the pumping off. That is not a deeper radio method, it is a
 *  different question, so it gets its own method and its own file - shared by
 *  both board families, because the MQTT engine is identical on all of them
 *  and only the TLS client class differs.
 *
 *  The seam stays honest because transportLoop() is a NO-OP on HTTP. A board
 *  that never leaves HTTP pays one comparison per loop() for it.
 *
 *  WHY THE SEAM IS INLINE IN HEADERS AND NOT IN A .cpp PER BOARD
 *  ------------------------------------------------------------
 *  The Arduino builder compiles EVERY .cpp under src/ for whichever board is
 *  selected. A src/ElectroCSE_ESP.cpp would therefore be compiled for a Nano
 *  33 IoT too, where <ESP8266WiFi.h> does not exist - so it would need an #if
 *  wrapper making the whole file empty, which is a file that exists to be
 *  skipped. Header-only transports skip themselves for free, because the
 *  router never includes them.
 * =============================================================================
 */

#ifndef ELECTROCSE_CORE_H
#define ELECTROCSE_CORE_H

#include <Arduino.h>
#include <ArduinoJson.h>

/*
 * The dashboard, unless the sketch said otherwise BEFORE the include.
 *
 * A bare hostname is treated as https by the transports; give a scheme for
 * plain http. See connect() below for why this can only be read from code
 * compiled as part of the sketch.
 */
#ifndef ELECTROCSE_IOT_SERVER
  #define ELECTROCSE_IOT_SERVER "iot.electrocse.com"
#endif

/*
 * The adaptive endpoint, not the older /api/device/telemetry.
 *
 * Both accept readings and both answer with commands and next_poll_ms, so
 * either would appear to work - but only this one validates `value_string`.
 * The older route drops it silently, so a text reading would arrive with no
 * value and nothing anywhere would say why.
 */
#ifndef ELECTROCSE_IOT_PATH
  #define ELECTROCSE_IOT_PATH "/api/v1/sync"
#endif

/*
 * Fixed-size tables, deliberately.
 *
 * There is no malloc here and there should not be: heap fragmentation on an
 * ESP8266 with ~40KB free is the classic cause of a board that runs for two
 * days and then stops, and a device sketch has a known, small number of
 * channels. Overflowing a table is reported on Serial rather than ignored.
 */
#ifndef ELECTROCSE_MAX_HANDLERS
  #define ELECTROCSE_MAX_HANDLERS 12
#endif

#ifndef ELECTROCSE_MAX_TIMERS
  #define ELECTROCSE_MAX_TIMERS 6
#endif

#ifndef ELECTROCSE_MAX_QUEUED
  #define ELECTROCSE_MAX_QUEUED 12
#endif

/*
 * MQTT sizing. Only reached on a deployment whose server answers `mqtt` - see
 * the transport section of the class below.
 *
 * The buffer is 512 because PubSubClient's DEFAULT IS 128 AND IT DROPS
 * ANYTHING LARGER WITHOUT A WORD - no return code, nothing on Serial. It is
 * the single commonest reason an MQTT publish appears to do nothing, and it
 * scales with the longest topic plus payload, not with the number of channels.
 */
#ifndef ELECTROCSE_MQTT_BUFFER
  #define ELECTROCSE_MQTT_BUFFER 512
#endif

/*
 * The floor between two publishes, in ms.
 *
 * On HTTP the server sets the pace with next_poll_ms and a sketch physically
 * cannot flood it. MQTT has no such answer coming back, so a sketch that calls
 * send() straight from loop() - which a beginner writes before they meet
 * every() - would publish thousands of times a second and get itself
 * rate-limited off the broker. This is the guard rail for that, and it is a
 * FLOOR rather than a schedule: nothing is published unless something was
 * actually queued, so a well-paced sketch never meets it.
 *
 * One second, not the HTTP cadence: making the two match would throw away the
 * responsiveness that is the entire reason a deployment runs MQTT.
 */
#ifndef ELECTROCSE_MQTT_MIN_PUBLISH_MS
  #define ELECTROCSE_MQTT_MIN_PUBLISH_MS 1000
#endif

/* Longest broker hostname and topic prefix the server may hand back. */
#ifndef ELECTROCSE_MQTT_HOST_LEN
  #define ELECTROCSE_MQTT_HOST_LEN 64
#endif
#ifndef ELECTROCSE_MQTT_PREFIX_LEN
  #define ELECTROCSE_MQTT_PREFIX_LEN 96
#endif

/* ------------------------------------------------------------------------- */

/**
 * The value that arrived from the dashboard.
 *
 * Everything crosses the wire as text, so this is a thin reader over it rather
 * than a variant: you know what your own channel carries, and asking for it
 * plainly is clearer than a type tag you have to check.
 */
class ElectroCseParam {
  public:
    explicit ElectroCseParam(const String& raw) : _raw(raw) {}

    int           asInt()    const { return _raw.toInt(); }
    float         asFloat()  const { return _raw.toFloat(); }
    const String& asString() const { return _raw; }

    /** Convenience for the commonest case: a switch. */
    bool isOn() const { return _raw.toInt() == 1; }

  private:
    String _raw;
};

typedef void (*ElectroCseHandler)(ElectroCseParam);
typedef void (*ElectroCseCallback)();

/**
 * A handler for channels nothing else claimed. Gets the NAME as well as the
 * value, which is the whole reason it exists - see onAny().
 */
typedef void (*ElectroCseAnyHandler)(const char* channel, ElectroCseParam param);

/* ------------------------------------------------------------------------- */

/*
 * -----------------------------------------------------------------------------
 * WHY THIS LIBRARY CARRIES ITS OWN THREE-LINE TYPE TRAITS
 * -----------------------------------------------------------------------------
 * send() has to know whether it was handed a whole number or a fraction, and
 * the ONLY thing that carries that is the argument's type. Plain overloads for
 * `int` and `float` look like they do it, and they do not:
 *
 *     ElectroCSE.send("temperature", 24.5, "C");     // ambiguous!
 *
 * because 24.5 is a `double`, and double->float and double->int are the same
 * rank to the compiler. So is `long`, which is what WiFi.RSSI() and millis()
 * return. Three of the four most natural calls in this library failed to
 * compile with a page of "candidate:" notes, and the workaround people find is
 * an (int) cast that silently truncates their sensor readings.
 *
 * The fix is one template per family of type, constrained so exactly one is
 * ever viable. That normally means <type_traits> - which cannot be used here:
 * the Arduino SAMD core ships a stub of that header, so `std::enable_if` does
 * not exist on a Nano 33 IoT and the build fails with "'enable_if' in namespace
 * 'std' does not name a template type". Verified, not assumed. Hence these,
 * which need no header at all and work on every core.
 */
template <bool B> struct EcseWhen {};
template <> struct EcseWhen<true> { typedef void Type; };

template <typename T> struct EcseIsSigned   { static const bool value = false; };
template <typename T> struct EcseIsUnsigned { static const bool value = false; };
template <typename T> struct EcseIsFloat    { static const bool value = false; };

template <> struct EcseIsSigned<signed char>        { static const bool value = true; };
template <> struct EcseIsSigned<short>              { static const bool value = true; };
template <> struct EcseIsSigned<int>                { static const bool value = true; };
template <> struct EcseIsSigned<long>               { static const bool value = true; };
template <> struct EcseIsSigned<long long>          { static const bool value = true; };

template <> struct EcseIsUnsigned<bool>             { static const bool value = true; };
template <> struct EcseIsUnsigned<unsigned char>    { static const bool value = true; };
template <> struct EcseIsUnsigned<unsigned short>   { static const bool value = true; };
template <> struct EcseIsUnsigned<unsigned int>     { static const bool value = true; };
template <> struct EcseIsUnsigned<unsigned long>    { static const bool value = true; };
template <> struct EcseIsUnsigned<unsigned long long> { static const bool value = true; };

/*
 * `char` is a THIRD type, distinct from both signed char and unsigned char, and
 * its signedness differs per architecture - signed on the ESPs' xtensa, unsigned
 * on ARM. Listing it explicitly is what stops send("x", someChar) compiling on
 * one board in this library and not on another.
 */
template <> struct EcseIsSigned<char>               { static const bool value = true; };

template <> struct EcseIsFloat<float>               { static const bool value = true; };
template <> struct EcseIsFloat<double>              { static const bool value = true; };
template <> struct EcseIsFloat<long double>         { static const bool value = true; };

class ElectroCseClass {
  public:
    /**
     * Join the WiFi and prove the token.
     *
     * Named connect() rather than begin() because that is the Arduino
     * convention for reaching a REMOTE thing - PubSubClient's
     * mqtt.connect(id, user, pass) is the exact parallel. begin() is for
     * initialising a peripheral: Serial, Wire, SPI.
     *
     * Returns whether it went live, and blocks up to `timeoutMs` doing it.
     * Ignoring the result is fine: loop() reconnects on its own, so a sketch
     * that never checks still recovers from a router rebooting.
     */
    /*
     * DEFINED INLINE, AND THAT IS THE WHOLE POINT - do not move it to a .cpp.
     *
     * ELECTROCSE_IOT_SERVER is a macro the SKETCH defines before including
     * ElectroCSE.h. A macro only exists in the translation unit that saw the
     * #define, and ElectroCSE.cpp is compiled separately - so any code in the
     * .cpp reading that macro gets the DEFAULT above, never the sketch's
     * value. This method is compiled as part of your .ino, so the macro
     * expands there and the real address reaches the object.
     *
     * The failure this prevents is silent and cost a debugging session: the
     * sketch compiles, WiFi connects, the board reports itself happy, and
     * every request goes to iot.electrocse.com instead of the address printed
     * three lines above it in the file you are reading.
     */
    bool connect(const char* token,
                 const char* ssid,
                 const char* pass,
                 unsigned long timeoutMs = 20000) {
        _server = ELECTROCSE_IOT_SERVER;
        _path = ELECTROCSE_IOT_PATH;

        return connectImpl(token, ssid, pass, timeoutMs);
    }

    /** Point the board somewhere else at runtime. Call before connect(). */
    void setServer(const char* server) { _server = server; }

    /** Call this every time through loop(). It services everything. */
    void loop();

    /**
     * Queue a reading for the next check-in.
     *
     * Takes any number - int, long, float, double, a literal, whatever your
     * sensor library returns - with no cast. See the traits above for why that
     * needs templates rather than two plain overloads.
     */
    template <typename T>
    typename EcseWhen<EcseIsSigned<T>::value>::Type
    send(const char* channel, T value, const char* unit = nullptr) {
        sendSigned(channel, (long) value, unit);
    }

    template <typename T>
    typename EcseWhen<EcseIsUnsigned<T>::value>::Type
    send(const char* channel, T value, const char* unit = nullptr) {
        /*
         * Unsigned gets its own branch rather than casting to long, because
         * millis() passes 2^31 after 24 days and would arrive NEGATIVE - an
         * uptime that counts up for three weeks and then goes backwards, which
         * nobody debugs quickly.
         */
        sendUnsigned(channel, (unsigned long) value, unit);
    }

    template <typename T>
    typename EcseWhen<EcseIsFloat<T>::value>::Type
    send(const char* channel, T value, const char* unit = nullptr) {
        sendFloat(channel, (double) value, unit);
    }

    /** Queue a text reading. */
    void send(const char* channel, const char* value);

    /** Run `fn` every `intervalMs`, without blocking. */
    void every(unsigned long intervalMs, ElectroCseCallback fn);

    /**
     * Handle any channel that no ELECTROCSE_LISTEN block claimed.
     *
     * ── Why this is not just a convenience ────────────────────────────────
     *
     * Every other way in is keyed on a channel name known when the sketch was
     * COMPILED. A sketch whose channels arrive at run time - one configured
     * from the dashboard rather than typed into the file - has no name to
     * register, and cannot use registerHandler() either: that table is fixed at
     * ELECTROCSE_MAX_HANDLERS and appends, so re-registering after each
     * configuration change would exhaust it after a few edits.
     *
     * So this takes the CHANNEL NAME as its first argument, which the fixed
     * handler signature deliberately does not. One function can then serve any
     * number of channels and look each one up in its own table.
     *
     * It is a FALLBACK, not an override: a named handler always wins, so
     * adding this to an ordinary sketch cannot change what that sketch already
     * does. The echo that clears the dashboard's "Pending" badge is recorded
     * for this path exactly as for a named one - forgetting that is the
     * commonest first-project bug and it must not come back through a side
     * door.
     */
    void onAny(ElectroCseAnyHandler fn);

    /** True once a check-in has succeeded and WiFi is still up. */
    bool isLive() const;

    /** How many seconds until the next check-in. Mostly for a status display. */
    unsigned long nextCheckInMs() const;

    /**
     * Which protocol this board ended up speaking: "http" or "mqtt".
     *
     * The sketch does not choose it and does not need to know it - the server
     * does, on the first check-in. This exists because "which one am I on?" is
     * the first question when a board behaves differently from the bench, and
     * without it the only way to answer is a packet capture.
     */
    const char* transport() const;

    // --- used by the macros; not part of the everyday surface -------------
    void registerHandler(const char* channel, ElectroCseHandler fn);
    void registerLive(ElectroCseCallback fn);

    /*
     * Public only because PubSubClient's callback is a plain C function
     * pointer, so delivery has to arrive through a free function
     * (ecseMqttTrampoline) rather than straight at a member. Not part of the
     * everyday surface and nothing in a sketch should call it.
     */
    void onMqttMessage(char* topic, const uint8_t* payload, unsigned int length);

  private:
    /*
     * THE RADIO SEAM. Defined by exactly one of the board headers the router
     * picks - never here, and never twice.
     */
    void ensureWiFi();
    bool connectImpl(const char* token, const char* ssid, const char* pass, unsigned long timeoutMs);
    bool checkIn();

    /*
     * THE PROTOCOL SEAM. Defined once, in ElectroCSE_Mqtt.h, which every board
     * header includes after typedef'ing its own two client classes.
     *
     * transportLoop() returns immediately on HTTP. Everything below it is only
     * reachable once the server has said `mqtt`.
     */
    void transportLoop();
    void mqttConnect();
    void mqttFlush();

    /*
     * Read the `transport` block out of a check-in reply and act on it.
     *
     * ── Why the SERVER picks the protocol and the sketch never mentions one ──
     *
     * The protocol is a property of the DEPLOYMENT, not of a board: every
     * device on an install speaks whichever one that install runs, and an
     * operator changes it in one place. Compiling it in would mean that
     * flipping it silently orphans every board already in the field until each
     * is physically re-flashed - and the symptom is the worst kind, because the
     * sketch still runs and the WiFi still joins. The dashboard just says
     * offline, for ever, with nothing wrong on the board.
     *
     * So the board asks, on the check-in it was making anyway. This is exactly
     * how next_poll_ms already worked, and for the same stated reason: the
     * whole fleet retunes from config with no reflashing.
     *
     * It is also the ONLY reason one sketch can serve both protocols. Nothing
     * in a sketch names a transport, so nothing in a sketch has to change when
     * the deployment's does.
     *
     * A reply with no `transport` key leaves the board on HTTP, which is what
     * every server predating this answers and what every standalone sketch
     * ever generated does.
     */
    void applyTransport(JsonObjectConst transport);

    struct Handler { const char* channel; ElectroCseHandler fn; };
    struct Timer   { unsigned long interval; unsigned long last; ElectroCseCallback fn; };
    struct Queued  { char channel[24]; char value[24]; char unit[8]; bool isString; };

    /*
     * The three send() templates funnel into these, so the formatting exists
     * once rather than once per argument type.
     */
    void   sendSigned(const char* channel, long value, const char* unit);
    void   sendUnsigned(const char* channel, unsigned long value, const char* unit);
    void   sendFloat(const char* channel, double value, const char* unit);

    void   dispatch(const char* channel, const String& value);

    /* The echo bookkeeping, shared by the named and catch-all paths. */
    void   recordApplied(const char* channel, const String& value);
    void   queue(const char* channel, const char* value, const char* unit, bool isString);
    String buildBody();

    const char* _token = nullptr;
    const char* _ssid  = nullptr;
    const char* _pass  = nullptr;

    /*
     * Set by connect() from the SKETCH's macros. The initialisers here are
     * only a fallback for a caller that never calls connect(), and they
     * expand in ElectroCSE.cpp - which is exactly why they cannot be relied
     * on to carry your address.
     */
    const char* _server = ELECTROCSE_IOT_SERVER;
    const char* _path   = ELECTROCSE_IOT_PATH;

    Handler _handlers[ELECTROCSE_MAX_HANDLERS];
    uint8_t _handlerCount = 0;

    Timer   _timers[ELECTROCSE_MAX_TIMERS];
    uint8_t _timerCount = 0;

    Queued  _queued[ELECTROCSE_MAX_QUEUED];
    uint8_t _queuedCount = 0;

    /*
     * Applied output values, echoed on every check-in. This is what clears the
     * dashboard's "Pending" badge, and putting it in the library rather than in
     * the sketch is most of the point: forgetting the echo is the commonest
     * first-project bug, and it presents as a card stuck on "Pending" for ever
     * with nothing in any log.
     */
    Queued  _applied[ELECTROCSE_MAX_HANDLERS];
    uint8_t _appliedCount = 0;

    ElectroCseCallback _onLive = nullptr;
    ElectroCseAnyHandler _onAny = nullptr;
    bool _announcedLive = false;

    unsigned long _pollIntervalMs = 5000;
    unsigned long _lastCheckInAt  = 0;
    uint8_t       _failures       = 0;
    bool          _everConnected  = false;
    bool          _announcedUrl   = false;

    /* ---------------------------------------------------------------------
     * TRANSPORT, chosen by the server on the first check-in.
     *
     * Everything here is empty and unused on an HTTP deployment. It is stored
     * in fixed char arrays rather than String for the reason the tables above
     * are fixed: this is written once and read for the life of the board, and
     * a heap allocation that outlives every free is exactly what fragments an
     * ESP8266 into a device that dies after two days.
     * ------------------------------------------------------------------ */
    bool     _useMqtt   = false;
    char     _mqttHost[ELECTROCSE_MQTT_HOST_LEN]     = {0};
    char     _mqttPrefix[ELECTROCSE_MQTT_PREFIX_LEN] = {0};
    char     _mqttClientId[40] = {0};
    char     _mqttUser[40]     = {0};
    uint16_t _mqttPort = 0;
    bool     _mqttTls  = false;

    /*
     * A START and a DURATION, never a deadline.
     *
     * `_mqttRetryAt = millis() + wait` reads better and is wrong: millis()
     * wraps to zero after about 49.7 days, so a deadline computed just before
     * the wrap is a huge number that `millis() <` stays true against for
     * weeks. The board would sit refusing to reconnect to a broker that was
     * perfectly healthy, once, on a device that had been up for seven weeks -
     * which is exactly the kind of fault nobody reproduces on a bench.
     *
     * Unsigned subtraction wraps correctly, so `now - from >= ms` is right on
     * both sides of the boundary. Every other timer in this library is written
     * that way; this one is not an exception.
     */
    unsigned long _mqttRetryFrom  = 0;
    unsigned long _mqttRetryMs    = 0;
    unsigned long _mqttLastPubAt  = 0;
    uint8_t       _mqttAttempts   = 0;
    bool          _mqttAnnounced  = false;
};

/*
 * ONE name for the object, and deliberately no short alias.
 *
 * A `#define ECSE ElectroCSE` costs nothing to add and is the reason no two
 * people's sketches look alike: a student hits an error, searches it, and
 * finds an answer written the other way with no way to tell whether the
 * difference matters. Aliases earn their place when a rename has to keep old
 * code compiling - there is no old code here, so there is nothing to keep.
 */
extern ElectroCseClass ElectroCSE;

/* ------------------------------------------------------------------------- */

/**
 * Registers a handler before setup() runs, so there is no registration line to
 * forget. The object is static at file scope; its constructor runs during
 * global initialisation.
 */
class ElectroCseRegistrar {
  public:
    ElectroCseRegistrar(const char* channel, ElectroCseHandler fn) {
        ElectroCSE.registerHandler(channel, fn);
    }
};

class ElectroCseLiveRegistrar {
  public:
    explicit ElectroCseLiveRegistrar(ElectroCseCallback fn) {
        ElectroCSE.registerLive(fn);
    }
};

/**
 * ELECTROCSE_LISTEN(relay) { digitalWrite(D1, param.isOn() ? HIGH : LOW); }
 *
 * Read it as a sentence: "listen for relay". The block below it is a function
 * you never call yourself - the library calls it for you, each time somebody
 * moves that control on the dashboard, with the new value in `param`.
 *
 * LISTEN IS STRICTLY ONE-WAY: DASHBOARD -> BOARD. It is the opposite direction
 * from send(), and the two do not pair up on a channel name. Listening for a
 * channel this board PUBLISHES - ELECTROCSE_LISTEN(temperature) next to a
 * send("temperature", ...) - compiles perfectly and never fires, because
 * nothing on the dashboard is sending that value back down. That is the one
 * predictable way to misread the word, and it costs an afternoon.
 *
 * The name is deliberately not Blynk's BLYNK_WRITE. That one is written from
 * the SERVER's point of view and then used in the DEVICE's sketch, so it says
 * "write" about the moment the board receives - which is why people spend
 * their first hour looking for the matching read. This file belongs to the
 * board, so the verb is the board's: it listens.
 *
 * The argument is a bare word, not a string, because it is pasted into a
 * function name as well as being stringified for the channel. That means it
 * must be a valid C identifier: `relay`, `pump_1`, `temp2` are fine. A channel
 * with a `.` or `-` in its name cannot be written this way - the plain
 * registerHandler("my-channel", fn) call still works for those.
 */
#define ELECTROCSE_LISTEN(channel)                                               \
    void ecseOn_##channel(ElectroCseParam param);                              \
    static ElectroCseRegistrar ecseReg_##channel(#channel, ecseOn_##channel);  \
    void ecseOn_##channel(ElectroCseParam param)

/**
 * ELECTROCSE_LIVE() { Serial.println("We are live."); }
 *
 * Runs once, the first time this board reaches the dashboard - so it is where
 * a startup message or a status LED belongs.
 *
 * ONE WORD, TWO FORMS, AND THE GRAMMAR CARRIES THE DIFFERENCE:
 *
 *     ELECTROCSE_LIVE() { ... }     the MOMENT it went live. Fires once.
 *     ElectroCSE.isLive()           the STATE right now. Ask it whenever.
 *
 * An earlier draft used two unrelated words for these on the theory that a
 * different thing deserves a different name. It reads worse: nothing about
 * "ready" tells you that "online" is its state counterpart, so the pair has to
 * be memorised instead of inferred. Sharing the stem and letting `is` mark the
 * question is the half that makes them teach each other - which matters far
 * more than the distinction, because the two are never interchangeable at a
 * call site anyway. One is a block you define; the other returns a bool.
 */
#define ELECTROCSE_LIVE()                                                \
    void ecseOnLive();                                                       \
    static ElectroCseLiveRegistrar ecseRegLive(ecseOnLive);              \
    void ecseOnLive()

#endif  // ELECTROCSE_CORE_H
