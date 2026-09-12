/*
 * ElectroCSE IoT — GENERIC FIRMWARE
 * =============================================================================
 * Upload this ONCE. After that, which pin does what is configured from the
 * website, and this board follows without ever being plugged in again.
 *
 * Move an LED from D1 to D2 on your breadboard, change the dropdown on the
 * device page, press Save. Within a minute this board has re-wired itself.
 *
 *
 * WHAT IT HANDLES
 * ---------------
 *   output         a relay, an LED, a buzzer          dashboard toggle -> pin
 *   input          a button, a PIR motion sensor      pin -> dashboard
 *   input_pullup   a button wired to GND              pin -> dashboard
 *   analog         a potentiometer, an LDR (A0)       pin -> dashboard, as 0-100%
 *
 * WHAT IT DOES NOT, AND WHY IT CANNOT
 * -----------------------------------
 * Anything that speaks a PROTOCOL rather than a voltage: DHT11/DHT22, DS18B20,
 * OLED and other I2C parts, NeoPixels, servos, ultrasonic rangefinders. Each of
 * those needs a driver compiled in - a library, exact timing, a bus address -
 * and none of that can be sent down as configuration.
 *
 * That is not a gap to be worked around here. For those, generate the sketch
 * for your device from the device page: it uses the same library, the same
 * dashboard and the same channels, and it can do anything C++ can.
 *
 * BOARDS: ESP8266 (NodeMCU, Wemos D1) and ESP32.
 * NOT the Nano 33 IoT / MKR 1010 - they have no LittleFS to remember a
 * configuration across a power cut, and remembering it is most of the point.
 *
 *
 * INSTALL FIRST  (Arduino IDE -> Tools -> Manage Libraries...)
 *   ElectroCSE    - or add the ZIP from your device page
 *   ArduinoJson   by Benoit Blanchon
 *   PubSubClient  by Nick O'Leary
 *
 * Dashboard : https://iot.electrocse.com
 * =============================================================================
 */

#define ELECTROCSE_IOT_TOKEN "PASTE_YOUR_TOKEN_HERE"

// The library already points at the dashboard, so there is nothing to set.
// Running your own server? Uncomment this and put its address in.
// #define ELECTROCSE_IOT_SERVER "http://192.168.1.50"

/*
 * Did the line ABOVE set an address, or is the library about to supply its
 * default? Decided here because it can only be asked here: ElectroCSE.h has an
 * `#ifndef ELECTROCSE_IOT_SERVER` fallback, so one line further down the macro
 * is always defined and the question is unanswerable.
 *
 * setup() prints the answer. See the note there for why a board pointed at the
 * wrong dashboard is otherwise completely silent.
 */
#ifdef ELECTROCSE_IOT_SERVER
  #define ECSE_SERVER_FROM_SKETCH 1
#else
  #define ECSE_SERVER_FROM_SKETCH 0
#endif

#include <ElectroCSE.h>

char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";

#if defined(ESP8266)
  #include <ESP8266HTTPClient.h>
  #include <WiFiClientSecure.h>
  #include <LittleFS.h>
  #define ECSE_FS         LittleFS
  #define ECSE_ADC_MAX    1023
#elif defined(ESP32)
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
  #include <LittleFS.h>
  #define ECSE_FS         LittleFS
  #define ECSE_ADC_MAX    4095
#else
  #error "ElectroCSE generic firmware needs an ESP8266 or an ESP32. Other boards have no LittleFS to remember a configuration across a power cut - generate a normal sketch from your device page instead."
#endif


/* ==========================================================================
 *  Tunables that are NOT sent by the server
 * ======================================================================= */

/* Ten usable pins on a NodeMCU, so twelve rows is already generous. */
#define ECSE_MAX_ROWS 12

/*
 * How often to ask the server whether the wiring has changed.
 *
 * This is the "I moved the LED, why has it not moved yet" number. It is not
 * free - it is one request per board per minute - but the reply is about 120
 * bytes when nothing has changed, because the server answers a version check
 * with the version alone.
 */
#define ECSE_CONFIG_POLL_MS 60000UL

/*
 * How many times a configuration may be tried before it is refused.
 *
 * Guards a CRASH LOOP specifically. A configuration that stops the board
 * checking in is caught by the watchdog below and rolled back; one that makes
 * it reboot never reaches the watchdog at all, because the board is gone before
 * the timer expires. It comes back, is handed the same configuration, and
 * crashes again - for ever, with the board unreachable. Counting attempts in
 * flash is what survives the reboot and breaks that cycle.
 */
#define ECSE_MAX_TRIALS 3

/* Where the two files live. See "LITTLEFS STRATEGY" at the bottom. */
#define ECSE_FILE_GOOD  "/ecse-config.json"
#define ECSE_FILE_TRIAL "/ecse-trial.json"


/* ==========================================================================
 *  The configuration, in memory
 * ======================================================================= */

enum EcseMode : uint8_t {
    ECSE_OUT = 0,
    ECSE_IN,
    ECSE_IN_PULLUP,
    ECSE_ANALOG,
};

struct EcseRow {
    char     channel[24];
    char     pinName[8];
    uint8_t  gpio;
    uint8_t  mode;

    /*
     * -1 means "never reported". A real reading can be 0, so a sentinel inside
     * the value's own range would make a genuine zero look like a board that
     * had not spoken yet - and the first thing an input does is report 0.
     */
    int           lastValue;
    unsigned long lastReportAt;
};

static EcseRow  rows[ECSE_MAX_ROWS];
static uint8_t  rowCount = 0;

static uint32_t configVersion = 0;      // what is applied right now
static uint32_t rejectedVersion = 0;    // refused after ECSE_MAX_TRIALS

/* Server-dictated, with the same floors the server applies. Defaults are only
 * what runs before the first successful fetch. */
static unsigned long sampleIntervalMs = 5000;
static unsigned long forceReportMs    = 300000;
static unsigned long watchdogMs       = 60000;
static int           analogDeadband   = 2;

static bool          configApplied    = false;   // pins are driven
static unsigned long lastConfigPollAt = 0;
static unsigned long lastSampleAt     = 0;

/* Watchdog state. `trialSince` of 0 means nothing is on trial. */
static unsigned long trialSince   = 0;
static uint32_t      trialVersion = 0;

/*
 * The configuration on trial, held as the raw JSON the server sent.
 *
 * Kept in RAM and written to flash only once it has PROVED itself, which is
 * what makes the rollback trivial: the known-good file is never overwritten by
 * something unproven, so recovering is just reading it back. Saving on arrival
 * and repairing afterwards would mean the one file the board depends on is
 * briefly the file that might be broken.
 */
static String pendingGood;


/* ==========================================================================
 *  Pin names
 * ======================================================================= */

/*
 * The server sends the name printed on the BOARD - "D1", not 5 - and the
 * mapping to a GPIO number is resolved here.
 *
 * It has to be this way round. `D1` is a constant the board's own core defines,
 * and its value differs between an ESP8266 and an ESP32; a server that sent
 * GPIO numbers would need its own copy of every board's pinout, and would be
 * wrong the first time a core renumbered one. The side that was compiled
 * against the core is the side that knows.
 */
static bool gpioFor(const char* name, uint8_t& out) {
#if defined(ESP8266)
    struct Entry { const char* name; uint8_t gpio; };

    static const Entry table[] = {
        {"D0", D0}, {"D1", D1}, {"D2", D2}, {"D3", D3}, {"D4", D4},
        {"D5", D5}, {"D6", D6}, {"D7", D7}, {"D8", D8}, {"A0", A0},
    };

    for (uint8_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strcmp(table[i].name, name) == 0) { out = table[i].gpio; return true; }
    }

    return false;
#else
    /*
     * ESP32 boards are silkscreened with raw GPIO numbers, which is what the
     * server's catalogue holds for them. Anything non-numeric is refused rather
     * than being quietly read as pin 0 by atoi() - pin 0 exists, is a boot
     * strapping pin, and driving it is how a board stops starting.
     */
    if (!name[0]) return false;

    for (const char* c = name; *c; c++) {
        if (*c < '0' || *c > '9') return false;
    }

    long gpio = atol(name);
    if (gpio < 0 || gpio > 39) return false;

    out = (uint8_t) gpio;
    return true;
#endif
}


/* ==========================================================================
 *  LittleFS
 * ======================================================================= */

static bool saveFile(const char* path, const String& body) {
    File f = ECSE_FS.open(path, "w");
    if (!f) return false;

    f.print(body);
    f.close();

    return true;
}

static String readFile(const char* path) {
    File f = ECSE_FS.open(path, "r");
    if (!f) return String();

    String body = f.readString();
    f.close();

    return body;
}

/*
 * How many times the version currently on trial has been attempted.
 *
 * Written BEFORE the configuration is applied, never after, which is the whole
 * point: if applying it reboots the board, the increment has already reached
 * flash and the next boot can see it. A counter written afterwards would be
 * lost by exactly the failure it exists to detect.
 */
static uint8_t bumpTrial(uint32_t version) {
    StaticJsonDocument<96> doc;
    uint8_t attempts = 1;

    String body = readFile(ECSE_FILE_TRIAL);

    if (body.length() && deserializeJson(doc, body) == DeserializationError::Ok) {
        if ((uint32_t) (doc["v"] | 0) == version) attempts = (uint8_t) (doc["n"] | 0) + 1;
    }

    StaticJsonDocument<96> out;
    out["v"] = version;
    out["n"] = attempts;

    String encoded;
    serializeJson(out, encoded);
    saveFile(ECSE_FILE_TRIAL, encoded);

    return attempts;
}

static void clearTrial() {
    ECSE_FS.remove(ECSE_FILE_TRIAL);
}


/* ==========================================================================
 *  Parsing and applying
 * ======================================================================= */

static uint8_t modeFrom(const char* name) {
    if (strcmp(name, "output") == 0)       return ECSE_OUT;
    if (strcmp(name, "input_pullup") == 0) return ECSE_IN_PULLUP;
    if (strcmp(name, "analog") == 0)       return ECSE_ANALOG;

    return ECSE_IN;
}

/**
 * Read a config document into `rows`. Does NOT touch any pin.
 *
 * Parsing and applying are separate so a malformed document cannot leave the
 * board half-configured - with three pins driven from the new map and four from
 * the old, which is a state no rollback could describe.
 */
/*
 * The pins the PREVIOUS configuration was driving.
 *
 * Snapshotted before the row table is overwritten, so applyPins() can hand back
 * any pin the new map no longer mentions. Without it, moving an LED from D1 to
 * D2 - the single commonest thing this firmware exists to do - leaves D1 still
 * configured as an output and still driven, so the old pin goes on holding a
 * relay closed while the dashboard shows a channel that has nothing to do with
 * it. Nothing reports that; the pin is simply never mentioned again.
 */
static uint8_t prevGpio[ECSE_MAX_ROWS];
static uint8_t prevCount = 0;
static uint8_t prevMode[ECSE_MAX_ROWS];

static bool parseConfig(JsonObjectConst config) {
    JsonArrayConst pins = config["pins"];
    if (pins.isNull()) return false;

    prevCount = rowCount;

    for (uint8_t i = 0; i < rowCount; i++) {
        prevGpio[i] = rows[i].gpio;
        prevMode[i] = rows[i].mode;
    }

    rowCount = 0;

    for (JsonObjectConst pin : pins) {
        if (rowCount >= ECSE_MAX_ROWS) {
            Serial.println(F("generic: too many mapped channels; the rest are ignored."));
            break;
        }

        const char* channel = pin["channel"] | "";
        const char* pinName = pin["pin"] | "";

        uint8_t gpio;
        if (!*channel || !gpioFor(pinName, gpio)) {
            Serial.print(F("generic: skipping unusable pin \""));
            Serial.print(pinName);
            Serial.println(F("\" - this board has no such pin."));
            continue;
        }

        EcseRow& r = rows[rowCount++];

        strncpy(r.channel, channel, sizeof(r.channel) - 1);
        r.channel[sizeof(r.channel) - 1] = '\0';
        strncpy(r.pinName, pinName, sizeof(r.pinName) - 1);
        r.pinName[sizeof(r.pinName) - 1] = '\0';

        r.gpio = gpio;
        r.mode = modeFrom(pin["mode"] | "input");
        r.lastValue = -1;
        r.lastReportAt = 0;
    }

    /* Floors applied HERE as well as on the server. Two copies of one rule is
     * deliberate: this one protects the server from a hand-edited config file,
     * and the server's protects every board from a mistake made there. */
    unsigned long interval = config["sample_interval_ms"] | 5000UL;
    sampleIntervalMs = interval < 5000UL ? 5000UL : interval;

    forceReportMs  = config["force_report_ms"] | 300000UL;
    watchdogMs     = config["watchdog_ms"] | 60000UL;
    analogDeadband = config["analog_deadband"] | 2;

    return true;
}

/**
 * Drive the pins.
 *
 * NOTHING calls this until the board is online - see the note in setup(). That
 * is the boot-pin safety rule, and it is what makes a bad configuration
 * survivable: on an ESP8266, D3 and D4 held LOW at power-on, or D8 held HIGH,
 * stop the chip booting at all. If this ran from setup() a single bad dropdown
 * on the website would produce a board that never starts again and therefore
 * can never be told otherwise - the one failure that cannot be fixed from the
 * website, which is the whole point of the website.
 *
 * Leaving every pin alone until after a successful check-in means the strapping
 * pins are read by the bootloader in their default high-impedance state, every
 * time, whatever is configured.
 */
static void applyPins() {
    for (uint8_t i = 0; i < rowCount; i++) {
        EcseRow& r = rows[i];

        switch (r.mode) {
            case ECSE_OUT:
                pinMode(r.gpio, OUTPUT);
                /*
                 * Started LOW rather than restored to its last value. The board
                 * has just come up and does not know what the world did while
                 * it was away; guessing is how a heater comes on in an empty
                 * house. The dashboard re-sends the desired state on the next
                 * check-in, because `desired_value` is durable server-side.
                 */
                digitalWrite(r.gpio, LOW);
                break;

            case ECSE_IN_PULLUP: pinMode(r.gpio, INPUT_PULLUP); break;
            case ECSE_ANALOG:    /* the ADC needs no pinMode */ break;
            default:             pinMode(r.gpio, INPUT); break;
        }

        r.lastValue = -1;      // force one report of the true state
    }

    /*
     * Hand back every pin the new map dropped.
     *
     * Only pins that were OUTPUTS are worth releasing: an input was never
     * driving anything, and calling pinMode on it again costs a little and
     * gains nothing. INPUT is the state the chip powers up in, so this returns
     * the pin to exactly where it started rather than to some chosen default.
     */
    for (uint8_t i = 0; i < prevCount; i++) {
        if (prevMode[i] != ECSE_OUT) continue;

        bool stillUsed = false;

        for (uint8_t j = 0; j < rowCount; j++) {
            if (rows[j].gpio == prevGpio[i]) { stillUsed = true; break; }
        }

        if (stillUsed) continue;

        digitalWrite(prevGpio[i], LOW);
        pinMode(prevGpio[i], INPUT);
    }

    prevCount = 0;

    configApplied = true;

    Serial.print(F("generic: applied "));
    Serial.print(rowCount);
    Serial.print(F(" pin(s), config version "));
    Serial.println(configVersion);
}


/* ==========================================================================
 *  Commands in
 * ======================================================================= */

/*
 * Declared here because onCommand() reports through it and the definition sits
 * further down, beside the loop that is its other caller. The Arduino IDE
 * usually generates prototypes for a .ino, but it does not do so reliably for
 * `static` functions - and the failure is a compile error on a file the
 * website hands out, which is the one place a build must not need fixing.
 */
static void reportStall(const __FlashStringHelper* why);

/*
 * Every dashboard control lands here, whatever it is called.
 *
 * A normal sketch writes ELECTROCSE_LISTEN(relay) and knows the name when it is
 * compiled. This one does not - its channels arrive from the server - so it
 * takes the catch-all, which is the one hook that is handed the channel NAME as
 * well as the value. The library still records the echo that clears the card's
 * "Pending" badge, exactly as it does for a named handler.
 */
static bool onCommand(const char* channel, ElectroCseParam param) {
    /*
     * FALSE MEANS "I DID NOT DRIVE ANYTHING", and the library then records no
     * echo - see ElectroCseAnyHandler.
     *
     * Every `return false` below is a state somebody can actually be in, and
     * each one used to report success. The dashboard cleared its "Pending"
     * badge and showed the value as applied, so a board driving nothing at all
     * was indistinguishable from a working one - on the single screen anybody
     * has for telling them apart. Leaving the badge up is what turns "the
     * toggle does nothing" into a question with a visible answer.
     */
    if (!configApplied) {
        reportStall(F("a command arrived before any pin map. Nothing is wired yet."));

        return false;                   // pins are not ours to drive yet
    }

    for (uint8_t i = 0; i < rowCount; i++) {
        if (strcmp(rows[i].channel, channel) != 0) continue;

        /*
         * The channel is MAPPED but not as an output. Reported rather than
         * skipped past: it means the wiring says input for something the
         * dashboard is putting a switch on, which is a mistake on the website
         * that nothing else would ever mention.
         */
        if (rows[i].mode != ECSE_OUT) {
            Serial.print(F("generic: "));
            Serial.print(channel);
            Serial.print(F(" is mapped to "));
            Serial.print(rows[i].pinName);
            Serial.println(F(" as an input, so it cannot be switched. "
                             "Set it to output in the wiring on the device page."));

            return false;
        }

        const bool on = param.asInt() != 0;

        digitalWrite(rows[i].gpio, on ? HIGH : LOW);

        Serial.print(F("generic: "));
        Serial.print(channel);
        Serial.print(F(" -> "));
        Serial.print(on ? F("HIGH") : F("LOW"));
        Serial.print(F(" on "));
        Serial.println(rows[i].pinName);

        return true;
    }

    /*
     * No row at all. The commonest cause is a channel with no pin against it in
     * the wiring editor - a legitimate state for a sensor, and exactly wrong
     * for something with an On/Off control pointed at it.
     */
    Serial.print(F("generic: no pin is mapped to "));
    Serial.print(channel);
    Serial.println(F(" - set one in the wiring on the device page."));

    return false;
}


/* ==========================================================================
 *  Readings out
 * ======================================================================= */

/**
 * Sample every input and send only what is worth sending.
 *
 * ── The deadband is a cost control, not a nicety ──────────────────────────
 *
 * Every value sent is a row in the database. A potentiometer nobody is touching
 * still jitters by a count or two, so a board that reported every sample would
 * write about 17,000 rows a day per channel doing nothing at all - and generic
 * firmware makes it a two-click job to turn on nine of them. Reporting only
 * real movement is what keeps that from becoming a bill.
 *
 * The heartbeat is the other half. Pure report-on-change is indistinguishable
 * from a dead board: a switch nobody has touched since Tuesday publishes
 * nothing, and the dashboard cannot tell that from a device that fell off the
 * Wi-Fi. `force_report_ms` sends an unchanged value occasionally so the two
 * look different.
 */
static void sampleInputs() {
    const unsigned long now = millis();

    for (uint8_t i = 0; i < rowCount; i++) {
        EcseRow& r = rows[i];

        if (r.mode == ECSE_OUT) continue;

        int value;

        if (r.mode == ECSE_ANALOG) {
            /*
             * Sent as 0-100%, not raw counts. This is the beginner firmware and
             * a chart labelled 0-100 is immediately readable, where "0-1023" is
             * a number you have to be told about. It also puts the reading and
             * the deadband in the same unit, so "2" means the same thing in
             * both. Needing the raw value is a good reason to generate a normal
             * sketch, where you have analogRead() itself.
             */
            value = (int) map(analogRead(r.gpio), 0, ECSE_ADC_MAX, 0, 100);
        } else {
            value = digitalRead(r.gpio) == HIGH ? 1 : 0;
        }

        const bool first  = r.lastValue < 0;
        const bool stale  = (now - r.lastReportAt) >= forceReportMs;

        bool moved;

        if (r.mode == ECSE_ANALOG) {
            const int delta = value > r.lastValue ? value - r.lastValue : r.lastValue - value;
            moved = delta >= analogDeadband;
        } else {
            moved = value != r.lastValue;      // a switch has no "nearly"
        }

        if (!first && !moved && !stale) continue;

        r.lastValue = value;
        r.lastReportAt = now;

        if (r.mode == ECSE_ANALOG) ElectroCSE.send(r.channel, value, "%");
        else                       ElectroCSE.send(r.channel, value);
    }
}


/* ==========================================================================
 *  Fetching the configuration
 * ======================================================================= */

/**
 * Ask the server for the pin map, sending the version we already hold.
 *
 * ── Why this does its own request instead of reading the library's ────────
 *
 * On an HTTP deployment the library's own check-in reply carries this block and
 * reading it there would be free. On an MQTT one there is no reply to read -
 * the board is subscribed to a broker and stops polling entirely - so a sketch
 * that depended on it would work on half the estate and silently never
 * reconfigure on the other half.
 *
 * One small request of its own works the same on both, and keeps the library
 * free of anything that exists only for this sketch.
 *
 * ── It is also the watchdog's heartbeat ───────────────────────────────────
 *
 * A successful reply is the proof that this board can still reach the server,
 * which is exactly what a configuration on trial has to demonstrate. So there
 * is no separate liveness check to get wrong.
 */
static bool fetchConfig() {
    if (WiFi.status() != WL_CONNECTED) return false;

    String url = String(ELECTROCSE_IOT_SERVER);
    const bool https = url.indexOf("://") == -1 || url.startsWith("https://");

    if (url.indexOf("://") == -1) url = (https ? "https://" : "http://") + url;
    url += "/api/v1/sync";

    WiFiClientSecure secure;
    WiFiClient plain;

    /* Encrypted but not authenticated, the same trade the library documents:
     * it stops passive sniffing, not an active man-in-the-middle. */
    if (https) secure.setInsecure();

    HTTPClient http;

    if (!(https ? http.begin(secure, url) : http.begin(plain, url))) {
        reportStall(F("the server address could not be opened. Check it is reachable from this network."));

        return false;
    }

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("Authorization", String("Bearer ") + ELECTROCSE_IOT_TOKEN);
    http.setTimeout(15000);

    /* No readings - the library sends those. This carries the version and
     * nothing else, which is what makes the usual reply about 120 bytes. */
    StaticJsonDocument<64> body;
    body["config_version"] = configVersion;

    String encoded;
    serializeJson(body, encoded);

    const int status = http.POST(encoded);

    if (status != 200 && status != 201) {
        http.end();

        if (status == 401) {
            Serial.println(F("generic: token rejected. It is wrong, revoked, or expired."));

            return false;
        }

        /*
         * EVERYTHING ELSE USED TO BE SILENT, and that is exactly what a board
         * pointed at the wrong dashboard looks like: HTTPClient returns a
         * NEGATIVE status for a connection that never completed - refused,
         * timed out, DNS that resolved nowhere - and this returned false
         * without a word. No error, no check-in, nothing on the device page but
         * "offline", for ever.
         *
         * The URL is printed rather than described. It is the one fact that
         * separates "the server is down" from "this board is asking the wrong
         * server", and those two are identical in every other symptom.
         *
         * Not rate-limited, because the fetch itself is: once a minute at
         * worst, which reads as a log rather than as a wall.
         */
        Serial.print(F("generic: POST "));
        Serial.print(url);
        Serial.print(F(" failed, status "));
        Serial.println(status);

        if (status < 0) {
            Serial.println(F("generic: nothing answered at that address. It is unreachable from this "
                             "network, or it is not where your dashboard lives."));
        }

        return false;
    }

    StaticJsonDocument<1536> reply;
    const DeserializationError err = deserializeJson(reply, http.getString());
    http.end();

    if (err == DeserializationError::NoMemory) {
        Serial.println(F("generic: config too large to parse. Map fewer channels, or raise the document size."));
        return false;
    }

    if (err != DeserializationError::Ok) return false;

    JsonObjectConst config = reply["config"];
    if (config.isNull()) return true;          // reached the server; nothing to do

    const uint32_t version = config["version"] | 0UL;

    /* Already running it. This is the ordinary case, every minute, for ever. */
    if (version == configVersion && configApplied) return true;

    /* A version that has already reboot-looped this board. Refused until
     * somebody changes the wiring, which changes the version. */
    if (version == rejectedVersion) return true;

    JsonArrayConst pins = config["pins"];

    /* Version moved but the server sent no map: it believed we were current.
     * Ask again next time with a version we certainly do not hold. */
    if (pins.isNull()) {
        configVersion = 0;
        return true;
    }

    const uint8_t attempts = bumpTrial(version);

    if (attempts > ECSE_MAX_TRIALS) {
        rejectedVersion = version;

        Serial.print(F("generic: config version "));
        Serial.print(version);
        Serial.println(F(" has failed too many times and is being refused. "
                         "Check the pins on the device page - a boot pin is the usual cause."));

        clearTrial();
        return true;
    }

    if (!parseConfig(config)) return true;

    configVersion = version;
    applyPins();

    /* On trial from here until the next successful fetch. */
    trialVersion = version;
    trialSince = millis();

    String raw;
    serializeJson(config, raw);
    pendingGood = raw;

    Serial.print(F("generic: trying config version "));
    Serial.print(version);
    Serial.print(F(" (attempt "));
    Serial.print(attempts);
    Serial.print('/');
    Serial.print(ECSE_MAX_TRIALS);
    Serial.println(F(")"));

    return true;
}


/* ==========================================================================
 *  The rollback watchdog
 * ======================================================================= */

/**
 * A configuration has proved it can still reach the server. Keep it.
 *
 * This is the ONLY thing that writes the known-good file, and it happens after
 * the board has demonstrated the config works rather than when the config
 * arrived. So a power cut at any moment leaves the last configuration that was
 * known to work, never the one currently being tested.
 */
static void promoteTrial() {
    if (pendingGood.length()) saveFile(ECSE_FILE_GOOD, pendingGood);

    pendingGood = String();
    clearTrial();

    trialSince = 0;
    trialVersion = 0;

    Serial.print(F("generic: config version "));
    Serial.print(configVersion);
    Serial.println(F(" confirmed working and saved."));
}

/**
 * The trial ran out of time. Put the last known-good configuration back.
 *
 * ── What this actually catches ────────────────────────────────────────────
 *
 * Not "wrong pin" - a light on the wrong pin still checks in perfectly, and no
 * watchdog can know you meant D2. It catches a configuration that stops this
 * board TALKING: a pin that shorts the supply and browns out the radio, a
 * strapping pin driven so the next boot never completes, anything that makes
 * the board unreachable. Those are the ones where rolling back is the only
 * remaining way to get a board back, because the website cannot reach a board
 * that is not there.
 */
static void rollback() {
    Serial.print(F("generic: config version "));
    Serial.print(trialVersion);
    Serial.print(F(" did not check in within "));
    Serial.print(watchdogMs / 1000);
    Serial.println(F("s. Rolling back."));

    pendingGood = String();
    trialSince = 0;

    const String good = readFile(ECSE_FILE_GOOD);

    if (!good.length()) {
        /*
         * Nothing to go back to - this was the board's first ever
         * configuration. Every pin is released rather than left driven: a board
         * that cannot talk to the server must not also be left holding a relay
         * closed, and high-impedance is the state it powers up in anyway.
         */
        for (uint8_t i = 0; i < rowCount; i++) pinMode(rows[i].gpio, INPUT);

        rowCount = 0;
        configApplied = false;
        configVersion = 0;

        Serial.println(F("generic: no earlier config to restore; all pins released."));
        return;
    }

    StaticJsonDocument<1536> doc;

    if (deserializeJson(doc, good) != DeserializationError::Ok) {
        Serial.println(F("generic: saved config is unreadable; all pins released."));

        for (uint8_t i = 0; i < rowCount; i++) pinMode(rows[i].gpio, INPUT);

        rowCount = 0;
        configApplied = false;
        configVersion = 0;
        return;
    }

    JsonObjectConst config = doc.as<JsonObjectConst>();

    if (parseConfig(config)) {
        configVersion = config["version"] | 0UL;
        applyPins();
    }
}


/* ==========================================================================
 *  setup / loop
 * ======================================================================= */

/*
 * SAY WHY NOTHING IS HAPPENING, at most once every fifteen seconds.
 *
 * A board stuck before its first configuration was completely silent, which is
 * the worst failure available to a device whose only diagnostic is the Serial
 * Monitor: no pin moves, no line appears, and "no WiFi", "wrong token" and
 * "dead sketch" look identical from the desk. Rate-limited because this is
 * called from loop().
 */
static void reportStall(const __FlashStringHelper* why) {
    static unsigned long lastStallAt = 0;
    const unsigned long now = millis();

    if (lastStallAt != 0 && (now - lastStallAt) < 15000UL) return;

    lastStallAt = now;

    Serial.print(F("generic: no configuration yet - "));
    Serial.println(why);
}

void setup() {
    Serial.begin(115200);
    delay(100);

    Serial.println(F("\n\nElectroCSE generic firmware"));

    /*
     * THE COMMONEST FAILURE, AND IT USED TO BE SILENT.
     *
     * An unreplaced token gets a 401 from the server, which IS reported - but
     * only once the board is on WiFi and has managed a request, and by then the
     * reader has usually decided the sketch is broken. Saying it at boot costs
     * one comparison and names the exact edit, on the one screen that is
     * already open while somebody flashes a board.
     */
    if (String(ELECTROCSE_IOT_TOKEN) == "PASTE_YOUR_TOKEN_HERE") {
        Serial.println(F("generic: ELECTROCSE_IOT_TOKEN is still PASTE_YOUR_TOKEN_HERE."));
        Serial.println(F("generic: paste the token from step 2 of your device page, then upload again."));
    }

    /*
     * THE ADDRESS, PRINTED EVERY BOOT, AND THE SECOND COMMONEST FAILURE.
     *
     * This file exists in two places and only one of them knows where your
     * dashboard is. The copy on the device page is substituted before it is
     * handed over, so its #define carries that deployment's address. The copy
     * in the Arduino IDE's example menu is this one, with that line COMMENTED
     * OUT - so it falls back to ELECTROCSE_IOT_SERVER's default, which is the
     * public dashboard.
     *
     * A board flashed from the example menu onto a self-hosted or sandbox
     * install therefore talks to a server that has never heard of it, and the
     * symptom is nothing whatsoever: no error, no 401, no check-in, a device
     * page that just goes on saying offline. That is not a failure anybody can
     * reason their way out of, and it cost a real afternoon.
     */
    Serial.print(F("generic: server "));
    Serial.println(ELECTROCSE_IOT_SERVER);

    if (!ECSE_SERVER_FROM_SKETCH) {
        Serial.println(F("generic: that address came from the library, not from this sketch."));
        Serial.println(F("generic: if your dashboard is somewhere else, this board will never reach "
                         "it and will report nothing at all. Download this file from your device "
                         "page instead - its copy has the right address already in it."));
    }

    /*
     * NO pinMode ANYWHERE IN setup(), AND THAT IS THE BOOT-PIN RULE.
     *
     * On an ESP8266, D3 and D4 must not be held LOW at power-on and D8 must not
     * be held HIGH, or the chip does not boot. Those pins are offered by the
     * website - people use them deliberately - so a configuration that drives
     * one is legitimate and has to be survivable.
     *
     * It is survivable only because nothing here touches a pin. The strapping
     * pins are read by the bootloader in the high-impedance state they power up
     * in, every single boot, whatever is configured; the configuration is
     * applied later, once the board is online and can be told to stop. A board
     * that applied its pins here could be made unbootable by one dropdown, and
     * would then be unreachable by the only tool that could undo it.
     */

    /*
     * begin() FORMATS AN UNMOUNTABLE FILESYSTEM, on both cores, and that is
     * wanted here even though it sounds alarming.
     *
     * A brand-new board has never had a LittleFS partition written, so the
     * first mount necessarily fails; without the format there would be no
     * filesystem on any board until somebody made one by hand, and the whole
     * remember-across-a-power-cut half of this sketch would silently never
     * work. What is lost in the bad case is the known-good configuration - and
     * the board recovers that from the server on its next fetch anyway, which
     * is the same path it takes on its first ever boot.
     *
     * If it still will not mount, the board runs perfectly well without it. It
     * just has to be online to be configured, which is said plainly rather than
     * left to be discovered as "my pins reset when the power blipped".
     */
    if (!ECSE_FS.begin()) {
        Serial.println(F("generic: no filesystem. The board still runs, but it must be online to be configured."));
    }

    /*
     * Read the trial counter BEFORE connecting, so a configuration that
     * reboot-loops this board is already known to be suspect by the time the
     * server offers it again.
     */
    const String trial = readFile(ECSE_FILE_TRIAL);

    if (trial.length()) {
        StaticJsonDocument<96> doc;

        if (deserializeJson(doc, trial) == DeserializationError::Ok) {
            const uint32_t v = doc["v"] | 0UL;
            const uint8_t  n = doc["n"] | 0;

            if (n >= ECSE_MAX_TRIALS) {
                rejectedVersion = v;

                Serial.print(F("generic: config version "));
                Serial.print(v);
                Serial.println(F(" failed on the last boots and will not be tried again."));
            }
        }
    }

    ElectroCSE.onAny(onCommand);
    ElectroCSE.connect(ELECTROCSE_IOT_TOKEN, ssid, pass);
}

void loop() {
    ElectroCSE.loop();

    const unsigned long now = millis();

    /*
     * WIFI, not ElectroCSE.isLive(), and the difference is a real defect fixed.
     *
     * This was `if (!ElectroCSE.isLive()) return;`, described as the boot-pin
     * guarantee. It is not that guarantee - `configApplied` is, and it is
     * checked separately before any pin is driven. What isLive() additionally
     * requires, on a platform running MQTT, is a live BROKER SESSION: see
     * ElectroCseClass::isLive(), which returns the MQTT client's connected
     * state whenever _useMqtt is set.
     *
     * So a board with perfect WiFi that could not reach the broker never ran a
     * single line of this loop. It never fetched a configuration, never applied
     * a pin, and said nothing about why - while a GENERATED sketch on the same
     * board worked, because its pins are compiled in and it never has to ask
     * the server what it is wired to. That is exactly the reported shape: "I
     * uploaded ElectroCSE_Generic.ino and it did nothing, only the sketch with
     * the pins defined in code works."
     *
     * fetchConfig() is a plain HTTP POST to /api/v1/sync needing an address, a
     * token and WiFi. WiFi is therefore the honest precondition, and a
     * successful fetch IS the check-in the old comment was reaching for -
     * config is applied on the strength of that fetch, never on the strength of
     * a transport that has nothing to do with it.
     */
    if (WiFi.status() != WL_CONNECTED) {
        reportStall(F("waiting for WiFi. Check the name and password, and that the network is 2.4 GHz."));

        return;
    }

    /*
     * Poll FASTER while a configuration is on trial.
     *
     * The trial is confirmed by a successful fetch, so at the ordinary once-a-
     * minute rate the next fetch and the 60s watchdog expiry land at the same
     * moment - a race that decides whether a perfectly good configuration is
     * kept or rolled back, settled by whichever branch this function reaches
     * first. Checking every five seconds removes the race entirely, and has the
     * better side effect: a wiring change is confirmed in about five seconds
     * rather than a minute.
     */
    const unsigned long pollEvery = trialSince != 0 ? 5000UL : ECSE_CONFIG_POLL_MS;

    if (lastConfigPollAt == 0 || (now - lastConfigPollAt) >= pollEvery) {
        lastConfigPollAt = now;

        /*
         * CAPTURED BEFORE THE FETCH, and that is the whole of the fix.
         *
         * fetchConfig() may itself start a trial, and a trial must never be
         * confirmed by the request that delivered it - the point is to prove
         * the board survives the new configuration, which it cannot have done
         * yet. The first version of this compared timestamps and was wrong in
         * the worst possible direction: `now` is read at the top of loop(),
         * fetchConfig() then sets trialSince to a LATER millis(), and the
         * unsigned subtraction `now - trialSince` underflows to about four
         * billion - which passed the check every time. The watchdog would have
         * promoted every configuration instantly and never watched anything,
         * while looking entirely correct.
         *
         * Comparing the value to itself needs no arithmetic and cannot
         * underflow: a trial is promoted only if one was already running before
         * this fetch and this fetch did not replace it.
         */
        const unsigned long trialBefore = trialSince;

        const bool reached = fetchConfig();

        if (reached && trialBefore != 0 && trialSince == trialBefore) promoteTrial();
    }

    if (trialSince != 0 && (now - trialSince) >= watchdogMs) rollback();

    /* Inputs. The floor is enforced here as well as on the server. */
    if (configApplied && (now - lastSampleAt) >= sampleIntervalMs) {
        lastSampleAt = now;
        sampleInputs();
    }
}
