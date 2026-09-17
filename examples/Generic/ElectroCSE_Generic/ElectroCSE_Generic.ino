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
 * BOARDS
 * ------
 * Every board this library supports at all:
 *
 *   ESP8266            NodeMCU, Wemos D1                    remembers its wiring
 *   ESP32              ESP32, C3, S3, CAM                   remembers its wiring
 *   WiFiNINA           Nano 33 IoT, MKR WiFi 1010,
 *                      Nano RP2040 Connect                  asks on every boot
 *
 * The right-hand column is the only difference, and it is worth understanding
 * rather than skipping, because it is not the one people expect.
 *
 * The ESPs have a LittleFS partition, so the last configuration that PROVED
 * itself is written to flash. The WiFiNINA boards have no filesystem, so the
 * same two files live in RAM and are gone at power-off - which means those
 * boards come up with no pin map and fetch one within a few seconds of joining
 * WiFi. That costs one round trip and nothing else: the server is the authority
 * on the wiring either way, and a board that has to ask is a board that cannot
 * be running something stale.
 *
 * What is genuinely lost is narrower than "persistence", and it is this: the
 * count of how many times a configuration has been tried. That counter exists
 * to break a REBOOT LOOP - a configuration that makes the board restart before
 * the watchdog can time it out - and breaking that loop requires surviving the
 * reboot. On an ESP it does. On a WiFiNINA board it does not, so recovery from
 * that one case is manual: change the pin on the website, and the board picks
 * the new map up on its next boot, before it applies anything.
 *
 * That case is also an ESP hazard specifically. It is D3, D4 and D8 on a
 * NodeMCU - strapping pins the bootloader reads at power-on - and a SAMD21 or
 * an RP2040 has no header pin that decides whether the chip boots. So the
 * protection and the hazard are missing from the same boards.
 *
 *
 * INSTALL FIRST  (Arduino IDE -> Tools -> Manage Libraries...)
 *   ElectroCSE    - or add the ZIP from your device page
 *   ArduinoJson   by Benoit Blanchon
 *   PubSubClient  by Nick O'Leary
 *   WiFiNINA         }  Nano 33 IoT, MKR WiFi 1010 and
 *   ArduinoHttpClient}  Nano RP2040 Connect only
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

/*
 * WHAT EACH BOARD FAMILY BRINGS, in the three facts this sketch actually
 * differs on: how it makes an HTTP request, how wide its ADC is, and whether
 * it has somewhere to keep a file.
 *
 * Keyed on the ARCHITECTURE and never on a board name, the same rule
 * ElectroCSE.h states for its own routing table: the thing that decides the
 * answer is the core's API, and a list of board defines needs editing every
 * time Arduino ships another product.
 *
 * ECSE_HAS_FS is a CAPABILITY, not a board test, and everything below asks it
 * rather than asking which chip this is. That is what keeps the difference to
 * two functions: with no filesystem the two files live in RAM, every other line
 * in this sketch is unchanged, and the only behaviour that differs is what
 * survives a power cut - which is stated in the header and printed at boot.
 */
#if defined(ESP8266)
  #include <ESP8266HTTPClient.h>
  #include <WiFiClientSecure.h>
  #include <LittleFS.h>
  #define ECSE_TRANSPORT_ESP 1
  #define ECSE_HAS_FS        1
  #define ECSE_FS            LittleFS
  #define ECSE_ADC_MAX       1023
#elif defined(ESP32)
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
  #include <LittleFS.h>
  #define ECSE_TRANSPORT_ESP 1
  #define ECSE_HAS_FS        1
  #define ECSE_FS            LittleFS
  #define ECSE_ADC_MAX       4095
#elif defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_SAM) || defined(ARDUINO_ARCH_MBED)
  #include <WiFiNINA.h>
  #include <ArduinoHttpClient.h>
  #define ECSE_TRANSPORT_ESP 0

  /*
   * No filesystem, and not for want of looking.
   *
   * A SAMD21 has no data partition and no EEPROM; emulating one means writing
   * to the program flash through a third-party library, which is a dependency
   * this sketch would carry on every board to serve two. The mbed core on the
   * Nano RP2040 Connect can mount a LittleFS over FlashIAPBlockDevice, but
   * only after carving a region out of the same flash the sketch is running
   * from - a decision that belongs to whoever owns the board's memory map, not
   * to an example.
   *
   * So: no. The known-good configuration lives in RAM for the life of a boot,
   * the board re-fetches after a power cut, and the one guarantee that needs
   * flash to work is documented as absent rather than quietly broken.
   */
  #define ECSE_HAS_FS        0

  /*
   * 10-bit, which is the SAMD and mbed default and NOT what the chip can do.
   * A SAMD21 will give 12 bits after analogReadResolution(12), and this sketch
   * deliberately does not call it: the reading is reported as a percentage, so
   * the extra two bits buy nothing a dashboard can show, and a sketch that
   * changed a global ADC setting would change it for anything else the board
   * is doing.
   */
  #define ECSE_ADC_MAX       1023
#else
  #error "ElectroCSE generic firmware needs an ESP8266, an ESP32, or a WiFiNINA board (Nano 33 IoT, MKR WiFi 1010, Nano RP2040 Connect). A plain Uno/Nano/Mega has no network hardware; an UNO R4 WiFi and a Wio Terminal have radios this library does not speak to yet. Generate a normal sketch from your device page instead."
#endif


/* ==========================================================================
 *  Tunables that are NOT sent by the server
 * ======================================================================= */

/*
 * WHICH BUILD OF THIS FILE IS ON THE BOARD.
 *
 * Reported to the dashboard on every config fetch, and it is the fact that was
 * missing when this sketch was first debugged against real hardware. The device
 * page could see the board was online, see it polling on schedule, and see the
 * pin map it was being sent - and had no way at all to tell whether the sketch
 * running was this month's or the copy somebody downloaded a fortnight ago.
 * Every symptom of an old build is a symptom of something else as well.
 *
 * A DATE rather than a semantic version. This file is not released on its own;
 * it is vendored beside the website and synced by tools/sync-firmware.sh, so
 * the only question ever asked of it is "is this older than the fix" - which a
 * date answers and a number nobody increments does not.
 *
 * A SUFFIX when it changes twice in one day, and that is not pedantry: the
 * whole value of this string is telling two builds apart on the device page,
 * and the first time it was needed the fix and the build it replaced were
 * shipped the same afternoon. Two boards both honestly reporting
 * "generic-2026-09-15" would have made the page say they agreed.
 */
#define ECSE_FIRMWARE_BUILD "generic-2026-09-15.6"

/*
 * ONE ROW PER MAPPABLE PIN ON THE LARGEST BOARD, and it is sized from the
 * server's catalogue rather than guessed.
 *
 *   esp8266    10 pins
 *   wifinina   18 pins
 *   esp32      23 pins
 *
 * It was 12, written when a NodeMCU was the only board this ran on and "twelve
 * is already generous" was true of it. It stopped being true the moment the
 * ESP32 and the WiFiNINA boards were supported, and the way it stopped was
 * silent: the rows past the ceiling are dropped, so the dashboard goes on
 * showing a channel with a pin next to it and a toggle somebody can press,
 * while the board has never heard of it. Nothing is wrong anywhere - the
 * config is valid, the version matches, the board reports healthy.
 *
 * 24 covers the largest catalogue with one spare. The cost is about a kilobyte
 * of RAM across rows[] and the two previous-state arrays, which is 3% of a
 * SAMD21's and nothing at all on an ESP.
 *
 * Exceeding it is still possible - a device could in principle carry more
 * channels than its board has pins - so it is still counted and REPORTED, to
 * the dashboard as well as to the serial port. See pinSummary().
 */
#define ECSE_MAX_ROWS 24

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

/*
 * Where the two files live - in flash on an ESP, in RAM on a board with no
 * filesystem. See "The two files" below, which is the only place that
 * difference exists.
 */
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

/*
 * A version refused after ECSE_MAX_TRIALS, and a FLAG saying whether there is
 * one - rather than 0 standing in for "none".
 *
 * Zero is not a free sentinel here. It is also what `config["version"] | 0UL`
 * yields when the server sends a reply this sketch cannot read a version out
 * of, and `version == rejectedVersion` is checked BEFORE anything is applied.
 * So a single unreadable version made the board refuse every configuration
 * from then on - silently, because a refusal at that point prints nothing,
 * starts no trial and changes no state. The board polls for ever, perfectly
 * online, doing nothing, and every symptom available says it is healthy.
 *
 * Two variables cannot collide. The flag is the authority; the number is only
 * read when the flag is set.
 */
static bool     hasRejected = false;
static uint32_t rejectedVersion = 0;

/* Server-dictated, with the same floors the server applies. Defaults are only
 * what runs before the first successful fetch. */
static unsigned long sampleIntervalMs = 5000;
static unsigned long forceReportMs    = 300000;
static unsigned long watchdogMs       = 60000;
static int           analogDeadband   = 2;

static bool          configApplied    = false;   // pins are driven
static unsigned long lastConfigPollAt = 0;
static unsigned long lastSampleAt     = 0;

/*
 * WiFi signal, reported like any other reading.
 *
 * Not a pin, so sampleInputs() cannot carry it - it walks the configured rows
 * and there is no row for a radio. It gets its own pair of state variables and
 * the same two rules every other channel obeys: report on real movement, and
 * report anyway once in a while so silence still means something.
 *
 * `rssi` is not a channel somebody has to add. The dashboard puts it on every
 * device it knows about and explains it in the channel hints, so a "WiFi
 * signal" widget is offered on a generic board whether or not anything ever
 * fills it - and before this it was never filled: the generic firmware sent
 * only the pins it was told about, so that widget sat empty for ever on the one
 * firmware that cannot be edited to add it.
 *
 * 32767 as "never read": a valid RSSI is a small negative number, so no real
 * sample can collide with it, and the first pass therefore always reports.
 */
static int           lastRssi        = 32767;
static unsigned long lastRssiAt      = 0;

/* Watchdog state. `trialSince` of 0 means nothing is on trial. */
static unsigned long trialSince   = 0;
static uint32_t      trialVersion = 0;

/*
 * WHY THE LAST FAILURE IS KEPT IN A VARIABLE.
 *
 * Everything this sketch knows about a failed fetch used to go to the Serial
 * Monitor and nowhere else. That is the right place for somebody holding the
 * board and the wrong place for every other situation - and it made a real bug
 * take three rounds of "it still does not work" to find, because the one fact
 * that would have identified it in a minute was written to a port nobody had
 * open.
 *
 * So the reason rides the NEXT request. The board is already talking to the
 * server every minute; carrying sixty bytes of "here is why I have no pin map"
 * costs nothing and turns an invisible failure into a line on the device page.
 *
 * Cleared on success, so the dashboard shows a stale reason for at most one
 * poll after the board recovers.
 */
static String lastFetchNote;

static void setFetchNote(const String& why) {
    // Bounded here as well as on the server. This is a diagnostic, and a
    // diagnostic that can grow without limit is a way to fill somebody's
    // database from a device.
    lastFetchNote = why.length() > 110 ? why.substring(0, 110) : why;
}

static void setFetchNote(const __FlashStringHelper* why) {
    setFetchNote(String(why));
}

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
#elif defined(ESP32)
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
#else
    /*
     * WiFiNINA boards use plain Arduino numbering, so the server's catalogue
     * sends "2".."13" for the digital pins and "A0".."A5" for the analog ones -
     * and those two halves are NOT the same kind of string, which is the whole
     * reason this branch is longer than a call to atoi().
     *
     * A digital pin's name IS its number. `A0` is not: it is a constant the
     * core defines, and on a Nano 33 IoT it is 15. Reading "A0" with atoi()
     * yields 0, which is a real and different digital pin - so the analog
     * channel would silently read D0 and report a number that moves for the
     * wrong reason. The table is what makes that unrepresentable.
     *
     * The board's own core is asked for the value, exactly as the ESP8266
     * branch above asks it for D1 - the side that was compiled against the core
     * is the side that knows.
     */
    struct Entry { const char* name; uint8_t pin; };

    static const Entry analog[] = {
        {"A0", A0}, {"A1", A1}, {"A2", A2}, {"A3", A3}, {"A4", A4}, {"A5", A5},
    };

    for (uint8_t i = 0; i < sizeof(analog) / sizeof(analog[0]); i++) {
        if (strcmp(analog[i].name, name) == 0) { out = analog[i].pin; return true; }
    }

    if (!name[0]) return false;

    for (const char* c = name; *c; c++) {
        if (*c < '0' || *c > '9') return false;
    }

    long pin = atol(name);

    /*
     * The catalogue offers 2..13 and nothing else. 0 and 1 are the hardware
     * serial pins the Serial Monitor is on, and driving them is how a board
     * stops printing the diagnostics this sketch relies on to explain itself -
     * so a hand-edited row naming one is refused here as well as being absent
     * from the dropdown.
     */
    if (pin < 2 || pin > 13) return false;

    out = (uint8_t) pin;
    return true;
#endif
}


/* ==========================================================================
 *  The two files
 * ======================================================================= */

/*
 * TWO FUNCTIONS, AND THEY ARE THE ENTIRE DIFFERENCE BETWEEN THE BOARD
 * FAMILIES.
 *
 * Everything else in this sketch - the trial, the watchdog, the rollback, the
 * refusal after three failed attempts - is written against readFile() and
 * saveFile() and asks nothing about the hardware. So a board with no filesystem
 * is served by keeping the same two files in RAM: every code path above and
 * below runs unchanged, and the only thing that differs is what is still there
 * after a power cut.
 *
 * The alternative was `#if ECSE_HAS_FS` scattered through promoteTrial(),
 * rollback(), bumpTrial() and setup(). That is the shape where one of them gets
 * missed, and a missed guard here is a board that compiles, runs, and quietly
 * has no rollback - which is indistinguishable from one that does until the day
 * it is needed.
 */
#if ECSE_HAS_FS

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

static void removeFile(const char* path) {
    ECSE_FS.remove(path);
}

#else

/*
 * RAM standing in for flash, on the boards that have no flash to stand in.
 *
 * Two named slots rather than a list, because there are exactly two paths and
 * both are compile-time constants in this file. A map keyed on the string would
 * be a general mechanism serving two callers, and would turn a typo in a path -
 * which the linker cannot catch either way - into a silently separate third
 * slot rather than into the fallthrough below, which is loud.
 *
 * WHAT IS LOST, precisely, and it is one thing: the trial COUNTER no longer
 * survives a reboot. The known-good configuration not surviving costs nothing
 * that matters, because the board asks the server for the current map within
 * seconds of joining WiFi and the server is the authority on it anyway. The
 * counter is different - it exists to break a reboot loop, and breaking a
 * reboot loop is by definition something that has to outlive a reboot. See the
 * header: the hazard it guards against is an ESP strapping-pin hazard, and
 * these boards do not have one.
 */
static String ramGood;
static String ramTrial;

static String* ramSlot(const char* path) {
    if (strcmp(path, ECSE_FILE_GOOD) == 0)  return &ramGood;
    if (strcmp(path, ECSE_FILE_TRIAL) == 0) return &ramTrial;

    return nullptr;
}

static bool saveFile(const char* path, const String& body) {
    String* slot = ramSlot(path);
    if (!slot) return false;

    *slot = body;

    /*
     * A String assignment can fail to allocate and leaves the old value in
     * place, which would report success while storing nothing. Cheap to check
     * and the failure is otherwise invisible - this is the same silent-empty
     * shape that cost three rounds of debugging in the HTTP path below.
     */
    return slot->length() == body.length();
}

static String readFile(const char* path) {
    String* slot = ramSlot(path);

    return slot ? *slot : String();
}

/*
 * Deleting is what clearTrial() means on a filesystem, and an empty slot is
 * what it means here. Declared so the one call site reads the same on both.
 */
static void removeFile(const char* path) {
    String* slot = ramSlot(path);
    if (slot) *slot = String();
}

#endif

/*
 * How many times the version currently on trial has been attempted.
 *
 * Written BEFORE the configuration is applied, never after, which is the whole
 * point: if applying it reboots the board, the increment has already reached
 * flash and the next boot can see it. A counter written afterwards would be
 * lost by exactly the failure it exists to detect.
 */
static uint8_t bumpTrial(uint32_t version) {
    ELECTROCSE_JSON_QUIET_BEGIN
    StaticJsonDocument<96> doc;
    ELECTROCSE_JSON_QUIET_END
    uint8_t attempts = 1;

    String body = readFile(ECSE_FILE_TRIAL);

    if (body.length() && deserializeJson(doc, body) == DeserializationError::Ok) {
        /*
         * `| 0UL`, NOT `| 0`, and the difference silently disabled this whole
         * mechanism for half the versions it can ever see.
         *
         * A config version is a crc32, so it uses the full 32 bits and is above
         * INT32_MAX about half the time. The default value in `variant | x`
         * chooses the type the variant is read as - so `| 0` asks for an `int`,
         * ArduinoJson finds the stored number does not fit one, and returns the
         * DEFAULT. The comparison below was therefore `0 == version`, false for
         * every large version, and `attempts` stayed 1 no matter how many times
         * a configuration had been tried.
         *
         * Nothing failed. The counter was written, read back, and quietly
         * ignored - so the crash-loop guard that ECSE_MAX_TRIALS exists to
         * provide was absent on half of all boards, and absent exactly where it
         * matters: a configuration that reboots the board would be retried for
         * ever rather than refused after three attempts.
         */
        if ((uint32_t) (doc["v"] | 0UL) == version) attempts = (uint8_t) (doc["n"] | 0) + 1;
    }

    ELECTROCSE_JSON_QUIET_BEGIN
    StaticJsonDocument<96> out;
    ELECTROCSE_JSON_QUIET_END
    out["v"] = version;
    out["n"] = attempts;

    String encoded;
    serializeJson(out, encoded);
    saveFile(ECSE_FILE_TRIAL, encoded);

    return attempts;
}

static void clearTrial() {
    /*
     * removeFile(), not ECSE_FS.remove(), and the indirection earns its keep:
     * ECSE_FS does not exist on a board with no filesystem, so the direct call
     * was the one line that would have needed an #if of its own - which is
     * exactly the scattering the two helpers above exist to avoid.
     */
    removeFile(ECSE_FILE_TRIAL);
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

/*
 * HOW MANY MAPPED CHANNELS THIS BOARD IS NOT DRIVING.
 *
 * A row is dropped for two reasons - the ceiling above, or a pin name this
 * board's core does not define - and both were reported to the Serial Monitor
 * and nowhere else. That is the wrong place for a fault whose entire symptom is
 * a dashboard that looks correct: the channel is listed, its pin is listed, the
 * toggle is live, the device is online, and pressing it does nothing. Somebody
 * with the board on the desk finds it in a minute; somebody whose board is
 * already in a cupboard never finds it at all.
 *
 * So it rides the next request, like every other reason in this sketch.
 */
static uint8_t droppedPins = 0;

/*
 * "3 pin(s)" - or "3 pin(s), 1 skipped", which is the half that matters.
 *
 * Used by the notes that report SUCCESS, deliberately. A skipped pin is not a
 * failed fetch and must not be reported as one: the config arrived, parsed and
 * applied, and the honest sentence is "this worked, and here is what it does
 * not cover".
 */
static String pinSummary() {
    String out = String(rowCount) + F(" pin(s)");

    if (droppedPins) out += String(F(", ")) + droppedPins + F(" skipped");

    return out;
}

static bool parseConfig(JsonObjectConst config) {
    JsonArrayConst pins = config["pins"];
    if (pins.isNull()) return false;

    prevCount = rowCount;

    for (uint8_t i = 0; i < rowCount; i++) {
        prevGpio[i] = rows[i].gpio;
        prevMode[i] = rows[i].mode;
    }

    rowCount = 0;
    droppedPins = 0;

    for (JsonObjectConst pin : pins) {
        /*
         * `continue`, not `break`, and the difference is the count.
         *
         * Breaking out leaves the remaining rows unexamined, so the board knows
         * it ran out of room and not by how much - and "some channels are
         * missing" is a materially worse thing to tell somebody than "four
         * channels are missing". Iterating an already-parsed array costs
         * nothing.
         */
        if (rowCount >= ECSE_MAX_ROWS) {
            droppedPins++;
            continue;
        }

        const char* channel = pin["channel"] | "";
        const char* pinName = pin["pin"] | "";

        uint8_t gpio;
        if (!*channel || !gpioFor(pinName, gpio)) {
            droppedPins++;

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

    if (droppedPins) {
        Serial.print(F("generic: "));
        Serial.print(droppedPins);
        Serial.print(F(" of "));
        Serial.print(rowCount + droppedPins);
        Serial.println(F(" mapped channels are NOT being driven by this board. Their cards on the "
                         "dashboard will not respond."));
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

    /*
     * THE MAP ITSELF, once per apply.
     *
     * A count alone answers the wrong question. The one thing somebody at the
     * desk needs is whether the board agrees with the dropdowns they just
     * pressed - and "applied 4 pin(s)" is equally true of the right map and of
     * a stale one with the same number of rows in it. Three short columns turn
     * "it is not working" into "it thinks led1 is on D1 and I moved it to D6",
     * which is an answer rather than a symptom.
     *
     * Only on apply, so it costs one block per wiring change rather than a line
     * every poll.
     */
    for (uint8_t i = 0; i < rowCount; i++) {
        Serial.print(F("generic:   "));
        Serial.print(rows[i].channel);
        Serial.print(F(" -> "));
        Serial.print(rows[i].pinName);
        Serial.print(F(" ("));

        switch (rows[i].mode) {
            case ECSE_OUT:        Serial.print(F("output"));       break;
            case ECSE_IN_PULLUP:  Serial.print(F("input_pullup")); break;
            case ECSE_ANALOG:     Serial.print(F("analog"));       break;
            default:              Serial.print(F("input"));        break;
        }

        Serial.println(')');
    }

    if (rowCount == 0) {
        Serial.println(F("generic: no channel on the device page has a pin against it, so this board "
                         "has nothing to drive. Set one in the Wiring panel."));
    }
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


/**
 * Report the WiFi signal, on the same terms as everything else.
 *
 * ── Why a deadband here too ───────────────────────────────────────────────
 *
 * RSSI is noisy by nature: a board sitting perfectly still on a bench moves a
 * few dBm between samples as the radio environment shifts. Reporting every
 * sample would write a row every five seconds for a number nobody is watching
 * that closely - about 17,000 rows a day, for the channel least likely to be
 * read. 3 dBm is below what changes a decision (the hints call −50 excellent
 * and −70 fair), so anything that moves the reading between those bands still
 * gets through.
 *
 * The heartbeat is the same one the pins use, and matters more here: signal
 * strength that has not changed is exactly the case where a chart going flat
 * and a board going missing must not look alike.
 *
 * Sent in dBm rather than as a percentage, unlike the analog inputs. A
 * percentage would need a floor and a ceiling to map between, and every choice
 * of those is a fiction - dBm is what the radio reports, what the dashboard's
 * hint explains, and what every other sketch in this library sends, so a device
 * moved between firmwares keeps one continuous chart.
 */
static void sampleRadio() {
    const unsigned long now = millis();

    if (WiFi.status() != WL_CONNECTED) return;

    const int rssi = (int) WiFi.RSSI();

    /*
     * 0 is not a signal strength, it is the value several cores return when
     * there is nothing to report yet. Sending it would put a spike at the top
     * of the chart meaning "excellent" at the exact moment the radio could not
     * answer.
     */
    if (rssi == 0) return;

    const bool first = lastRssi == 32767;
    const int  delta = rssi > lastRssi ? rssi - lastRssi : lastRssi - rssi;
    const bool moved = delta >= 3;
    const bool stale = (now - lastRssiAt) >= forceReportMs;

    if (!first && !moved && !stale) return;

    lastRssi = rssi;
    lastRssiAt = now;

    ElectroCSE.send("rssi", rssi, "dBm");
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
/* ==========================================================================
 *  The one request this sketch makes for itself
 * ======================================================================= */

/*
 * WHY THE TRANSPORT IS A FUNCTION AND NOT INLINE IN fetchConfig()
 *
 * Three board families reach this sketch and they disagree about exactly one
 * thing: how to make an HTTP request. An ESP has HTTPClient, which takes a
 * whole URL and hands back a Stream. A WiFiNINA board has ArduinoHttpClient,
 * which takes a host and a port apart and is driven header by header.
 *
 * Everything either side of the request - what to send, how to read the reply,
 * which branch was taken and what to report - is identical, and it is the part
 * that has been wrong four times. So it stays in one place, written once, and
 * the eighty lines that genuinely differ are quarantined below.
 */

/* `http.begin()` refused the address outright: nothing was sent and there is no
 * HTTP status to report. A sentinel rather than a bool out-parameter, so the
 * one caller has a single value to switch on - and far outside the range of
 * both a real status and ArduinoHttpClient's own negative error codes. */
#define ECSE_SYNC_NO_BEGIN (-900)

/**
 * Read `config` out of a reply that is still arriving.
 *
 * PARSED STRAIGHT OFF THE SOCKET, NEVER OUT OF A String, and that is a fix
 * rather than a preference. It is worth stating in full once, here, because
 * BOTH client libraries have the same defect with different spellings and the
 * WiFiNINA one was about to be written the wrong way by analogy.
 *
 *   ESP8266HTTPClient::getString()
 *       if (_size > 0) {
 *           if (!_payload->reserve(_size + 1)) {
 *               DEBUG_HTTPCLIENT(...);
 *               return *_payload;            // <-- EMPTY STRING, no error
 *           }
 *       }
 *
 *   ArduinoHttpClient::responseBody()
 *       if (bodyLength > 0) {
 *           if (response.reserve(bodyLength) == 0) {
 *               return String((const char*)NULL);   // <-- same, no error
 *           }
 *       }
 *       ...
 *       if (!response.concat((char)c)) return String((const char*)NULL);
 *       if (bodyLength > 0 && bodyLength != response.length())
 *           return String((const char*)NULL);
 *
 * Both ask the heap for the whole body as ONE contiguous block, and both hand
 * back an empty String when that fails. An ESP8266 running WiFi, a TLS-capable
 * client and a PubSubClient buffer fragments long before it runs out, so the
 * request can fail while ESP.getFreeHeap() still reads comfortable; the caller
 * cannot tell it from a server that sent nothing. ArduinoHttpClient has three
 * such returns rather than one, and the last of them - a short read - is
 * reported identically to a successful empty body.
 *
 * Reading the stream needs no such allocation: ArduinoJson pulls bytes as it
 * parses and builds only the document. It also cannot be handed a truncated
 * body that happens to parse, because it stops at the end of the JSON value
 * rather than at a byte count somebody else computed.
 */
static DeserializationError ecseParseReply(Stream& body, JsonDocument& into) {
#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 7

    /*
     * ArduinoJson 7 grows the document on demand, so there is no size to come
     * in under and nothing for a filter to save. Parsed plain, deliberately:
     * this is the most-travelled path in the sketch, and on a mechanism that
     * has now failed several times in the field the right instinct is fewer
     * moving parts between the socket and the answer, not cleverer ones.
     */
    return deserializeJson(into, body);

#else

    /*
     * ArduinoJson 6 has a fixed capacity, and the reply is mostly fields this
     * sketch never reads - `transport`, `commands`, `server_time`, one of which
     * grows with the deployment's hostname. Unfiltered, a board with ten or
     * twelve mapped channels (which ECSE_MAX_ROWS allows) overflows and gets
     * NoMemory, and the pins are LAST in the reply - so what is lost is exactly
     * the part that matters, on exactly the boards doing the most with this.
     *
     * The filter is therefore a v6 necessity rather than an improvement, which
     * is why v7 above does without it.
     */
    ELECTROCSE_JSON_QUIET_BEGIN
    StaticJsonDocument<96> filter;
    ELECTROCSE_JSON_QUIET_END
    filter["config"] = true;

    return deserializeJson(into, body, DeserializationOption::Filter(filter));

#endif
}

/**
 * POST the sketch's own body to /api/v1/sync and parse what comes back.
 *
 * Returns the HTTP status, ECSE_SYNC_NO_BEGIN, or whichever negative code the
 * client library uses for a connection that never completed. `err` and
 * `bodySize` are only meaningful on a 200/201; `bodySize` is -1 where the
 * client cannot say.
 */
#if ECSE_TRANSPORT_ESP

static int ecsePostSync(const String& origin, const String& path, const String& payload,
                        JsonDocument& into, DeserializationError& err, int& bodySize) {
    const String url = origin + path;
    const bool https = origin.startsWith("https://");

    WiFiClientSecure secure;
    WiFiClient plain;

    /* Encrypted but not authenticated, the same trade the library documents:
     * it stops passive sniffing, not an active man-in-the-middle. */
    if (https) secure.setInsecure();

    HTTPClient http;

    if (!(https ? http.begin(secure, url) : http.begin(plain, url))) return ECSE_SYNC_NO_BEGIN;

    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    http.addHeader("Authorization", String("Bearer ") + ELECTROCSE_IOT_TOKEN);
    http.setTimeout(15000);

    /*
     * HTTP/1.0, AND IT IS THE FIX RATHER THAN A PREFERENCE.
     *
     * This asks the server for the simplest framing HTTP has: no chunked
     * transfer encoding, no keep-alive, one response then the connection
     * closes. Both of the things it switches off were in the path, and both
     * fail in the same direction - quietly, with an empty or partial body and
     * a 200 in the server's log.
     *
     *   chunked      PHP-FPM streams, so nginx frames the reply in
     *                length-prefixed pieces. getString() reassembles them, and
     *                deserializeJson() reading a STREAM cannot - which is why
     *                this line and the stream parse are one change and not two.
     *                Under HTTP/1.0 a server may not chunk at all.
     *
     *   keep-alive   leaves the socket open afterwards, so the end of the body
     *                is known only from its length. Every disagreement about
     *                that length is a read that hangs or stops early.
     *
     * The cost is one TCP connection per minute, which is what this was doing
     * anyway - http.end() follows every fetch.
     *
     * ArduinoHttpClient has no equivalent switch and does not need one: it
     * decodes chunked framing itself, in read(), so its stream is already
     * dechunked by the time ArduinoJson sees it.
     */
    http.useHTTP10(true);

    const int status = http.POST(payload);

    if (status != 200 && status != 201) {
        http.end();

        return status;
    }

    err = ecseParseReply(http.getStream(), into);
    bodySize = http.getSize();

    http.end();

    return status;
}

#else

static int ecsePostSync(const String& origin, const String& path, const String& payload,
                        JsonDocument& into, DeserializationError& err, int& bodySize) {
    String   host;
    uint16_t port;
    bool     tls;

    /*
     * The LIBRARY's splitter, not a second copy written here.
     *
     * It is `static` at namespace scope in ElectroCSE_WiFiNINA.h, so this
     * translation unit already has it. Writing another would mean two answers
     * to "is a bare hostname TLS?" - the contract says yes, and a sketch that
     * disagreed with the library it is built on would talk to port 80 for its
     * pin map and 443 for everything else, against a server that answers both.
     */
    ecseSplitServer(origin, host, port, tls);

    /*
     * BOTH CLIENTS ARE CONSTRUCTED AND ONE IS USED, and the declaration has to
     * be here rather than inside the branch.
     *
     * HttpClient holds a Client REFERENCE, so whichever it is given has to
     * outlive the whole request. A client created inside an if() is destroyed
     * at the closing brace, which presents as a connection that closes
     * mid-reply - a 200 with a body that stops partway, i.e. the exact failure
     * everything above is written to avoid. They are stack objects with no
     * constructor side effects, so the unused one costs a few bytes.
     *
     * TLS IS A CLASS HERE, NOT A MODE. There is no setInsecure(): WiFiSSLClient
     * validates against the root certificates burned into the NINA module's own
     * firmware, and cannot be told not to. A self-hosted dashboard on https
     * with a private or self-signed certificate is therefore unreachable from
     * these boards - it fails at connect, before any of this sketch's
     * diagnostics can see a status - and http is the honest answer on a LAN.
     * The ESP branch's setInsecure() has no counterpart to port.
     */
    WiFiSSLClient secure;
    WiFiClient    plain;

    HttpClient http = tls ? HttpClient(secure, host.c_str(), port)
                          : HttpClient(plain, host.c_str(), port);

    /*
     * The timeout is load-bearing twice over. It bounds the connect, and it is
     * also what ArduinoJson's stream reader inherits - Stream::readBytes()
     * blocks on _timeout - so a reply that stops halfway ends as a parse error
     * fifteen seconds later rather than as a board that never returns from
     * loop().
     */
    http.setTimeout(15000);

    /*
     * Assembled header by header, because ArduinoHttpClient has no "POST this
     * body with these headers" call.
     *
     * Content-Length is the line that matters. Leave it out and the request is
     * still well-formed and still answered 200 - the server reads an empty
     * body, so `config_version` is absent, so it sends the full map every time
     * and the board is told it is permanently out of date. There is no error
     * anywhere to find.
     */
    http.beginRequest();
    http.post(path);
    http.sendHeader("Content-Type", "application/json");
    http.sendHeader("Accept", "application/json");
    http.sendHeader("Content-Length", payload.length());
    http.sendHeader("Authorization", String("Bearer ") + ELECTROCSE_IOT_TOKEN);
    http.beginBody();
    http.print(payload);
    http.endRequest();

    const int status = http.responseStatusCode();

    if (status != 200 && status != 201) {
        http.stop();

        return status;
    }

    /*
     * The headers have to be consumed before the body is a body.
     *
     * HttpClient::read() returns header bytes until this is called; handing the
     * object to ArduinoJson without it parses "HTTP/1.1 200 OK" as JSON and
     * fails with InvalidInput on a reply that is perfectly correct. The ESP
     * branch has no counterpart because HTTPClient::getStream() is already
     * positioned at the body.
     */
    http.skipResponseHeaders();

    bodySize = http.contentLength();
    err = ecseParseReply(http, into);

    http.stop();

    return status;
}

#endif


static bool fetchConfig() {
    if (WiFi.status() != WL_CONNECTED) return false;

    /*
     * Built once, here, and handed to the transport in two pieces.
     *
     * The ORIGIN and the PATH are separate because the two client libraries
     * want them differently - HTTPClient takes a whole URL, ArduinoHttpClient
     * takes a host, a port and a path - and joining them in one branch only to
     * split them again in the other is how the two drift. `url` below is for
     * the error message and nothing else.
     */
    String origin = String(ELECTROCSE_IOT_SERVER);

    if (origin.indexOf("://") == -1) origin = "https://" + origin;

    while (origin.endsWith("/")) origin = origin.substring(0, origin.length() - 1);

    const String path = "/api/v1/sync";
    const String url  = origin + path;

    /*
     * No readings - the library sends those. This carries the version this
     * board is running, plus which build of this file is running it.
     *
     * The build string rides THIS request rather than the library's check-in
     * because this request is the one a generic board always makes: on an MQTT
     * deployment the library stops checking in over HTTP entirely, so a `meta`
     * block sent there would reach the dashboard on half the estate and never
     * on the other half - the same trap documented above fetchConfig().
     */
    ELECTROCSE_JSON_QUIET_BEGIN
    StaticJsonDocument<256> body;
    ELECTROCSE_JSON_QUIET_END
    body["config_version"] = configVersion;
    body["meta"]["firmware_version"] = ECSE_FIRMWARE_BUILD;

    /*
     * Why the LAST attempt failed, if it did. Sent before this attempt is made,
     * which is the only order that works: a board that cannot read the reply
     * cannot report that it could not read the reply in the same breath.
     */
    if (lastFetchNote.length()) body["meta"]["note"] = lastFetchNote;

    String encoded;
    serializeJson(body, encoded);

    /*
     * ONLY `config` IS KEPT on ArduinoJson 6, and that is a correctness fix
     * rather than a saving - see ecseParseReply(). The document is declared
     * here because it has to outlive the call.
     */
    ELECTROCSE_JSON_QUIET_BEGIN
    StaticJsonDocument<1536> reply;
    ELECTROCSE_JSON_QUIET_END

    DeserializationError err = DeserializationError::Ok;
    int bodySize = -1;

    const int status = ecsePostSync(origin, path, encoded, reply, err, bodySize);

    if (status == ECSE_SYNC_NO_BEGIN) {
        setFetchNote(F("could not open the server address"));
        reportStall(F("the server address could not be opened. Check it is reachable from this network."));

        return false;
    }

    if (status != 200 && status != 201) {
        if (status == 401) {
            setFetchNote(F("token rejected (401)"));
            Serial.println(F("generic: token rejected. It is wrong, revoked, or expired."));

            return false;
        }

        setFetchNote(String(F("HTTP status ")) + status);

        /*
         * EVERYTHING ELSE USED TO BE SILENT, and that is exactly what a board
         * pointed at the wrong dashboard looks like: both client libraries
         * return a NEGATIVE status for a connection that never completed -
         * refused, timed out, DNS that resolved nowhere - and this returned
         * false without a word. No error, no check-in, nothing on the device
         * page but "offline", for ever.
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

    /*
     * EVERY FAILURE FROM HERE IS REPORTED TWICE - to the Serial Monitor, and to
     * the dashboard on the next poll.
     *
     * The Serial half is free and is the right answer for somebody sitting at
     * the board. It is also the half nobody has when the board is already in a
     * cupboard, on a roof, or three rooms away - and this specific failure took
     * three rounds of "it still does not work" to pin down precisely because
     * the only place it was ever written down was a serial port nobody had open.
     *
     * `lastFetchNote` rides the next request, so the device page can say what
     * happened without anybody plugging in a cable. It is a diagnostic and
     * carries no authority: the server records it as a claim by the board.
     */
    if (err == DeserializationError::NoMemory) {
        setFetchNote(F("out of memory parsing the pin map"));

        Serial.println(F("generic: pin map too large to parse. Map fewer channels, or free some heap."));
        return false;
    }

    /*
     * THIS USED TO BE A BARE `return false`, AND IT IS THE WORST PLACE IN THE
     * SKETCH TO SAY NOTHING.
     *
     * Everything upstream of here is visible: a bad token is a 401 and is
     * printed, an unreachable server is a negative status and is printed. A
     * reply that arrives and cannot be read produced no output at all - and the
     * board carries on polling once a minute for ever, perfectly online, with
     * whatever pin map it already had. On a board that has never managed a
     * first fetch, that is no pin map at all: the dashboard shows it online,
     * the wiring panel shows the wiring saved, and not one pin is driven.
     */
    if (err != DeserializationError::Ok) {
        setFetchNote(String(F("reply unreadable: ")) + err.c_str());

        Serial.print(F("generic: the server answered 200 but the reply could not be read - "));
        Serial.println(err.c_str());
        Serial.print(F("generic: Content-Length was "));
        Serial.println(bodySize);

        if (bodySize == 0) {
            Serial.println(F("generic: an empty reply usually means something between this board and "
                             "the dashboard closed the connection - a proxy, or a redirect to https."));
        }

        return false;
    }

    /*
     * The note stops being a FAULT report here and becomes an OUTCOME report.
     *
     * Cleared at this point so nothing below inherits the reason the last
     * attempt failed - every branch from here sets its own, including the two
     * that mean everything is fine. That is deliberate: "the board is running
     * config N with 2 pins" is the sentence that was missing, and a channel
     * that only ever carries bad news cannot say it.
     */
    lastFetchNote = String();

    /*
     * EVERY REMAINING EARLY RETURN REPORTS WHAT WAS ACTUALLY PARSED.
     *
     * Up to here a failure is a failure and says so. Below here they are
     * DECISIONS - "nothing to do", "no map in this reply", "already current" -
     * and each of them looked identical from the outside: the board polls on
     * schedule, drives nothing, and reports no problem, because by its own
     * lights there isn't one.
     *
     * That is the shape that cost three rounds of "it still does not work".
     * The reply parsed perfectly every time; what could not be seen from
     * anywhere was which branch it then took. So the branch is named, and where
     * the answer depends on the document's contents the document goes with it -
     * truncated, because this rides a request every minute and is a diagnostic
     * rather than a channel.
     */
    JsonObjectConst config = reply["config"];

    if (config.isNull()) {
        String doc;
        serializeJson(reply, doc);

        setFetchNote(String(F("no config in reply: ")) + doc.substring(0, 70));

        Serial.print(F("generic: the reply had no config block. Document was: "));
        Serial.println(doc.substring(0, 200));

        return true;
    }

    const uint32_t version = config["version"] | 0UL;

    /* Already running it. This is the ordinary case, every minute, for ever. */
    if (version == configVersion && configApplied) {
        setFetchNote(String(F("running config ")) + version + F(" (") + pinSummary() + F(")"));
        return true;
    }

    /*
     * A version with no version in it. Reported rather than passed over: it
     * means the server answered, the reply parsed, and the one number this
     * whole mechanism turns on was missing or unreadable - which is a fault at
     * one end or the other and is invisible from both.
     */
    if (version == 0) {
        setFetchNote(F("reply carried no usable config version"));

        Serial.println(F("generic: the reply had no usable config version in it."));
        return true;
    }

    /* A version that has already reboot-looped this board. Refused until
     * somebody changes the wiring, which changes the version. */
    if (hasRejected && version == rejectedVersion) {
        setFetchNote(String(F("refusing config ")) + version + F(" - it failed too many times"));
        return true;
    }

    JsonArrayConst pins = config["pins"];

    /* Version moved but the server sent no map: it believed we were current.
     * Ask again next time with a version we certainly do not hold.
     *
     * Announced rather than done quietly. Reaching here twice running is a
     * disagreement between this board and the server about what it is running,
     * and the zeroing is what breaks the deadlock - but if it ever fails to,
     * the board polls for ever with nothing to show for it and this line is the
     * only evidence the loop is happening at all. */
    if (pins.isNull()) {
        String doc;
        serializeJson(config, doc);

        setFetchNote(String(F("config had no pins: ")) + doc.substring(0, 70));

        Serial.print(F("generic: the server sent a version but no pin map. Config was: "));
        Serial.println(doc.substring(0, 200));

        configVersion = 0;
        return true;
    }

    const uint8_t attempts = bumpTrial(version);

    if (attempts > ECSE_MAX_TRIALS) {
        hasRejected = true;
        rejectedVersion = version;

        Serial.print(F("generic: config version "));
        Serial.print(version);
        Serial.println(F(" has failed too many times and is being refused. "
                         "Check the pins on the device page - a boot pin is the usual cause."));

        clearTrial();
        return true;
    }

    if (!parseConfig(config)) {
        setFetchNote(F("the pin map could not be read out of the config"));
        return true;
    }

    configVersion = version;
    applyPins();

    setFetchNote(String(F("applied config ")) + version + F(" (") + pinSummary() + F(", on trial)"));

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

    ELECTROCSE_JSON_QUIET_BEGIN
    StaticJsonDocument<1536> doc;
    ELECTROCSE_JSON_QUIET_END

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

    Serial.print(F("\n\nElectroCSE generic firmware, build "));
    Serial.println(F(ECSE_FIRMWARE_BUILD));

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
#if ECSE_HAS_FS

    if (!ECSE_FS.begin()) {
        Serial.println(F("generic: no filesystem. The board still runs, but it must be online to be configured."));
    }

#else

    /*
     * SAID EVERY BOOT, and it is not noise.
     *
     * This board keeps its configuration in RAM, so every boot starts with no
     * pin map and fetches one. That is fine and is the design - but it is also
     * indistinguishable, from the outside, from a board that has lost its
     * settings; somebody watching an LED come back on a few seconds late needs
     * to know which of the two they are looking at before they start
     * diagnosing the wrong one.
     */
    Serial.println(F("generic: this board has no filesystem, so it asks the server for its wiring "
                     "on every boot. That takes a few seconds after WiFi and needs no action."));

#endif

    /*
     * Read the trial counter BEFORE connecting, so a configuration that
     * reboot-loops this board is already known to be suspect by the time the
     * server offers it again.
     */
    const String trial = readFile(ECSE_FILE_TRIAL);

    if (trial.length()) {
        ELECTROCSE_JSON_QUIET_BEGIN
        StaticJsonDocument<96> doc;
        ELECTROCSE_JSON_QUIET_END

        if (deserializeJson(doc, trial) == DeserializationError::Ok) {
            const uint32_t v = doc["v"] | 0UL;
            const uint8_t  n = doc["n"] | 0;

            if (n >= ECSE_MAX_TRIALS && v != 0) {
                hasRejected = true;
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

    /*
     * THE WATCHDOG READS millis() FRESH, AND NEVER THE `now` ABOVE.
     *
     * This line was `(now - trialSince) >= watchdogMs`, and it rolled back
     * every configuration in the SAME loop iteration that applied it.
     *
     * `now` is read at the top of loop(). fetchConfig() then runs - a whole
     * HTTP request, a few hundred milliseconds - and sets `trialSince` to a
     * millis() from AFTER that. So `trialSince` is LATER than `now`, the
     * unsigned subtraction underflows to about four billion, and the comparison
     * against 60000 passes instantly. The trial was born already expired.
     *
     * What that looked like from outside, for four rounds of debugging: the
     * board fetched the map on schedule, applied it, and released every pin
     * again microseconds later, before the next statement could sample an input
     * or the next command could drive an output. rollback() with no known-good
     * file - which is every board that has never completed a trial, i.e. all of
     * them, because promotion needs a second fetch that this made unreachable -
     * sets `rowCount = 0`, `configApplied = false` and `configVersion = 0`.
     * So the next poll asked from scratch, applied, and was rolled back again,
     * once a minute, for ever. Every signal said healthy: online, polling,
     * 200s, a valid pin map arriving each time. No pin ever moved.
     *
     * The identical trap is written up twelve lines above, for the PROMOTE
     * path, where it was found first and fixed by comparing `trialSince` to
     * itself rather than doing arithmetic on it. The rollback line one
     * statement below kept the bug the note describes.
     *
     * millis() is monotonic and `trialSince` is always a millis() already
     * taken, so reading it fresh here cannot underflow: the difference is a
     * real elapsed time or zero.
     */
    if (trialSince != 0) {
        const unsigned long trialAge = millis() - trialSince;

        if (trialAge >= watchdogMs) rollback();
    }

    /* Inputs. The floor is enforced here as well as on the server. */
    if (configApplied && (now - lastSampleAt) >= sampleIntervalMs) {
        lastSampleAt = now;
        sampleInputs();

        /*
         * NOT gated on `configApplied` in spirit - it shares the timer only.
         *
         * The radio needs no pin map, so a board that has never managed to
         * fetch one could report its signal perfectly well. It is called from
         * inside this branch anyway because a board with no configuration is
         * usually a board that cannot reach the server either, and a second
         * timer for one channel is not worth the duplication.
         */
        sampleRadio();
    }
}
