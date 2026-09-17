# ElectroCSE — Arduino IoT library for ESP8266, ESP32 and Arduino WiFi boards

**Send sensor readings to a web dashboard and control your board from a browser
— in about a dozen lines of Arduino code.**

ElectroCSE is a free Arduino library that connects an **ESP8266 (NodeMCU,
Wemos D1 Mini)**, an **ESP32**, or an **Arduino WiFi board** (Nano 33 IoT,
MKR WiFi 1010, Nano RP2040 Connect) to the
[ElectroCSE IoT dashboard](https://iot.electrocse.com). You get live charts,
toggle switches, sliders and status cards on a web page, without writing any
HTML, any JavaScript, or any server code.

![License: MIT](https://img.shields.io/badge/license-MIT-blue)
![Version 0.1.0](https://img.shields.io/badge/version-0.1.0-informational)
![ESP8266 · ESP32 · SAMD · mbed](https://img.shields.io/badge/boards-ESP8266%20%7C%20ESP32%20%7C%20Nano%2033%20IoT%20%7C%20MKR%201010-success)
![No JavaScript needed](https://img.shields.io/badge/you%20write-Arduino%20only-orange)

```cpp
#define ELECTROCSE_IOT_TOKEN "PASTE_YOUR_TOKEN_HERE"
#include <ElectroCSE.h>

ELECTROCSE_LISTEN(relay) {                 // a switch on the dashboard...
  digitalWrite(D1, param.isOn());          // ...turns on a pin here
}

void setup() {
  pinMode(D1, OUTPUT);
  ElectroCSE.connect(ELECTROCSE_IOT_TOKEN, "MyWiFi", "MyPassword");

  ElectroCSE.every(10000, []() {           // every 10 seconds
    ElectroCSE.send("temperature", 24.5, "C");
  });
}

void loop() { ElectroCSE.loop(); }
```

That is a complete, working project. There is nothing else to set up.

---

## Contents

- [What can I build with this?](#what-can-i-build-with-this)
- [What do I need?](#what-do-i-need)
- [Quick start — your first project in 5 steps](#quick-start--your-first-project-in-5-steps)
- [Your first sketch, line by line](#your-first-sketch-line-by-line)
- [Installing the library](#installing-the-library)
- [The whole API — 7 things to learn](#the-whole-api--7-things-to-learn)
- [Example sketches](#example-sketches)
- [Troubleshooting](#troubleshooting)
- [Frequently asked questions](#frequently-asked-questions)
- [How it works](#how-it-works)
- [Things worth knowing](#things-worth-knowing)
- [Project layout](#project-layout)
- [Supported boards](#supported-boards)
- [Licence](#licence)

---

## What can I build with this?

Anything where a board measures something, or switches something, and you want
to see or press it from a phone:

| Project | What goes up | What comes down |
|---|---|---|
| Home temperature monitor | `temperature`, `humidity` | — |
| Smart plug / lamp | — | `relay` toggle |
| Plant watering system | `soil_moisture` | `pump` toggle |
| Garage door status | `door_open` | `door` toggle |
| Weather station | `temperature`, `pressure`, `light` | — |
| Robot arm demo | `angle` | `servo` slider |
| Room presence logger | `motion`, `rssi` | — |

These are ordinary school and college project shapes. Each one is `send()` in
one direction and `ELECTROCSE_LISTEN` in the other, which is the whole library.

---

## What do I need?

**Hardware — one of these boards:**

- ESP8266: NodeMCU v2/v3, Wemos D1 Mini, ESP-12E — *the cheapest way to start*
- ESP32: DevKit v1, ESP32-WROOM-32, ESP32-S3
- Arduino: Nano 33 IoT, MKR WiFi 1010, Nano RP2040 Connect

A USB cable that carries **data**, not only power. (A charging-only cable is a
classic first hurdle: the board lights up and no serial port ever appears.)

**Software:**

- [Arduino IDE](https://www.arduino.cc/en/software) 1.8.19 or 2.x
- The board package for your board, from Tools → Board → Boards Manager
- This library, plus **ArduinoJson** and **PubSubClient** — see
  [Installing](#installing-the-library). Both are needed on every board and every
  deployment: the library carries an MQTT transport whether or not your dashboard
  uses one, so the header will not compile without PubSubClient present.

**An account:** a free login at [iot.electrocse.com](https://iot.electrocse.com).

You do **not** need: a static IP, port forwarding, a router change, a paid
broker, a domain name, or any web development.

---

## Quick start — your first project in 5 steps

**Step 1 — Add your device.** Sign in to
[iot.electrocse.com](https://iot.electrocse.com), open **Devices → Add device**,
give it a name like `Node1`, and choose your board.

**Step 2 — Copy the token.** The device page shows a token that looks like
`ecse_iot_12|k3PmZ...`. That single string is both *which* device it is and
*proof* that it is yours, so treat it like a password: don't paste it into a
public repository, a forum post or a screenshot.

**Step 3 — Declare your channels.** Still on the device page, add the widgets
you want — a **Toggle** on the channel `relay`, a **Line chart** on `light`. The
channel name is the word your code and your dashboard agree on. Get this right
before you flash; see the [warning below](#step-3-matters-more-than-it-looks).

**Step 4 — Open the example.** In the Arduino IDE:
File → Examples → **ElectroCSE** → your board → **01_Basic_Blink**. Paste your
token into the first line, then fill in your Wi-Fi name and password.

**Step 5 — Upload, then open the Serial Monitor at 115200 baud.** You should
see:

```
ElectroCSE: connecting to MyWiFi.....
ElectroCSE: WiFi ok, IP 192.168.1.42
ElectroCSE: posting to https://iot.electrocse.com/api/v1/sync
```

The dashboard card turns green within a couple of seconds. Press the toggle and
the LED changes.

### Step 3 matters more than it looks

**Add your widgets before you upload the sketch, and use the same channel
names in both places.** If the dashboard sends `relay1` and your sketch listens
for `relay`, everything looks correct on both sides and nothing happens — the
card just sits on **Pending** for ever. The Serial Monitor is what tells you:

```
ElectroCSE: no handler for channel relay1
```

If you change a channel name later, you must **re-upload the sketch**. The board
does not learn about dashboard changes on its own.

---

## Your first sketch, line by line

```cpp
#define ELECTROCSE_IOT_TOKEN "ecse_iot_12|k3PmZ..."   // 1
#include <ElectroCSE.h>                               // 2

char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

ELECTROCSE_LISTEN(relay) {                               // 3
  digitalWrite(LED_BUILTIN, param.isOn() ? HIGH : LOW);
}

ELECTROCSE_LIVE() {                                      // 4
  Serial.println("We are live.");
}

void setup() {
  Serial.begin(115200);                                  // 5
  pinMode(LED_BUILTIN, OUTPUT);

  ElectroCSE.connect(ELECTROCSE_IOT_TOKEN, ssid, pass);  // 6

  ElectroCSE.every(10000, []() {                         // 7
    ElectroCSE.send("rssi", WiFi.RSSI(), "dBm");
  });
}

void loop() {
  ElectroCSE.loop();                                     // 8
}
```

1. **Your token, before the include.** The library reads it at compile time.
2. **One include for every board.** No `ElectroCSE_ESP32.h` to choose — the
   header works out which board the IDE selected. Choosing wrongly is a wall of
   errors about a WiFi type that doesn't exist, which reads as a broken library
   rather than as a wrong line, so the choice is removed.
3. **`ELECTROCSE_LISTEN(relay)` is your handler for one channel.** It runs when
   somebody presses that widget. Inside it, `param` holds the value:
   `.isOn()`, `.asInt()`, `.asFloat()`, `.asString()`. Listening is strictly
   *incoming* — see [`send` goes up, `LISTEN` comes down](#send-goes-up-listen-comes-down).
4. **`ELECTROCSE_LIVE()` runs once**, the moment this board first reaches the
   dashboard — the place for a startup message or a status LED. Its state
   counterpart is `ElectroCSE.isLive()`, which you can ask at any time: same
   word, and the `is` marks the question.
5. **Serial at 115200.** Everything the library wants to tell you goes here.
   Debugging without it is guessing.
6. **`connect()` joins Wi-Fi and proves the token.** It blocks for up to 20
   seconds and returns `true` or `false`.
7. **`every(ms, ...)` is a non-blocking timer.** Use it instead of `delay()`.
   `delay(10000)` freezes the board, so commands from the dashboard arrive up to
   ten seconds late and the card looks stuck.
8. **`ElectroCSE.loop()` does all the work** — checking in, sending your queued
   readings, running your timers, and calling your handlers. Call it every time
   through `loop()`, and don't block around it.

---

## Installing the library

### Option A — Library Manager (easiest)

Tools → **Manage Libraries…**, search **ElectroCSE**, click Install, and accept
when it offers **ArduinoJson** and **PubSubClient** too.

*Nothing found?* The library is not in Arduino's index yet — use Option B.

### Option B — ZIP

1. [Download the ZIP](https://github.com/electrocse/ElectroCSE-IoT/archive/refs/heads/main.zip)
2. Sketch → Include Library → **Add .ZIP Library…**
3. **Install the dependencies yourself** from Manage Libraries:
   - **ArduinoJson** (v6 or v7) — every board
   - **PubSubClient** by Nick O'Leary — every board, even if your dashboard runs
     HTTP. The MQTT transport is compiled in either way, because which one you
     use is decided by the server at run time and not by your sketch.
   - **WiFiNINA** *and* **ArduinoHttpClient** — Nano 33 IoT / MKR 1010 / Nano
     RP2040 Connect only
4. **Restart the IDE**, or File → Examples → ElectroCSE stays empty.

A ZIP install does *not* pull dependencies in, so skipping step 3 gives you a
missing-header error on the first compile — which looks like this library being
broken when it isn't.

### PlatformIO

```ini
lib_deps = electrocse/ElectroCSE
```

That is the whole of it. `library.json` declares the dependencies and which
platforms each one belongs to, so PlatformIO installs ArduinoJson and
PubSubClient everywhere and adds WiFiNINA and ArduinoHttpClient only on the
boards that need a co-processor radio — unlike the Arduino Library Manager,
which has no way to express that and installs all four on every board.

*Not in the registry yet?* Point it at the repository instead, which needs no
registration and pulls the same `library.json`:

```ini
lib_deps = https://github.com/electrocse/ElectroCSE-IoT.git
```

---

## The whole API — 7 things to learn

| Call | What it does |
|---|---|
| `ElectroCSE.connect(token, ssid, pass)` | Join Wi-Fi and prove the token. Returns `bool`; blocks up to 20 s. |
| `ElectroCSE.loop()` | Call every time through `loop()`. Services everything. |
| `ElectroCSE.send(channel, value, unit)` | Queue a reading. Any number — `int`, `long`, `float`, `double`, `millis()` — with no cast, or a `const char*` for text. |
| `ELECTROCSE_LISTEN(channel) { … }` | Handle a switch or slider. `param.isOn()`, `.asInt()`, `.asFloat()`, `.asString()`. |
| `ElectroCSE.every(ms, fn)` | Run something periodically, without blocking. |
| `ELECTROCSE_LIVE() { … }` | Fires once, when the board first reaches the dashboard. |
| `ElectroCSE.isLive()` | Are we reaching the dashboard, and is Wi-Fi still up? |
| `ElectroCSE.transport()` | `"http"` or `"mqtt"` — which one the server put you on. Diagnostics only. |

That is the entire public surface. There is no broker to configure, no topic
string to compose and no callback to register by hand — on **either** protocol.
`transport()` is there to answer "which one am I on?", never to be branched on:
if a sketch has to know, something above it has leaked.

### `send` goes up, `LISTEN` comes down

The two never meet on the same channel. **`ElectroCSE.send()` is upstream** —
your board reporting a measurement to the dashboard. **`ELECTROCSE_LISTEN()` is
downstream, and only downstream** — your board being told something by a switch
or a slider somebody pressed.

So a channel is one or the other, never both. Listening for a channel your board
publishes:

```cpp
ElectroCSE.send("temperature", 24.5, "C");   // upstream
ELECTROCSE_LISTEN(temperature) { … }         // never fires — nothing sends this down
```

compiles perfectly and stays silent for ever, because nothing on the dashboard
is sending that value back. If a `LISTEN` block never runs, check that its
channel belongs to a **control** widget — a toggle or a slider — and not to a
chart or a status card.

---

## Example sketches

Nine sketches, three per board family, each one a step up. Every one is
compiled on every release.

**File → Examples → ElectroCSE →** `ESP8266` / `ESP32` / `WiFiNINA`

Every sketch is named `<number>_<level>_<project>`, so the IDE's menu says what
the sketch *does* without your having to open it. The number is the order to
work through them in; the project name is what you will have built at the end.

| Example | You'll learn | Parts needed |
|---|---|---|
| **01_Basic_Blink** | One toggle on the dashboard turns the built-in LED on and off | Just the board |
| **02_Moderate_LDR_Relay** | A sensor going up *and* a switch coming down, at the same time | LDR + 10 kΩ resistor, relay or LED |
| **03_Advanced_Servo_Dashboard** | Several readings, a slider driving a servo, and the `ELECTROCSE_LIVE` hook | The above + servo + separate 5 V supply |

Each sketch's comment header lists the exact wiring and the exact widgets to add
on the dashboard, plus the mistake that board is famous for — the NodeMCU's
LED being wired backwards, the ESP32's ADC2 pins failing the moment Wi-Fi comes
on, and the servo brown-out that presents as the board randomly going offline.

---

## Generic firmware — configure the wiring from the website

**Get it from your device page** — *Wiring → the generic firmware* — which hands
it over with this dashboard's address already in it. It is also
**File → Examples → ElectroCSE → Generic → `ElectroCSE_Generic`** once the
library is installed; that copy is the same file, but it carries the *default*
address, so a self-hosted dashboard has one line to change.

Upload it once. After that, which pin does what is a dropdown on the device
page: move an LED from D1 to D2 on your breadboard, change it on the website,
press Save, and within a minute the board has re-wired itself. No USB cable.

ESP8266 and ESP32 only — a Nano 33 IoT has no LittleFS to remember a
configuration across a power cut, and remembering it is most of the point.

### What it covers, and what it cannot

| Works | Needs a normal sketch |
|---|---|
| Relay, LED, buzzer (`output`) | DHT11 / DHT22, DS18B20 |
| Button, PIR (`input`, `input_pullup`) | OLED, BME280 and other I²C parts |
| Potentiometer, LDR on A0 (`analog`, sent as 0–100%) | NeoPixel, servo, ultrasonic |

The right-hand column is not a gap to work around. Each of those speaks a
*protocol* — precise timing, a bus address, a driver — and none of that can be
sent down as configuration. Generate a sketch from your device page for those;
it uses the same library, dashboard and channels.

### The config block

It rides the reply to `/api/v1/sync`, and it is **version-gated**: the board
sends the version it holds, and gets the map back only when it differs.

```jsonc
// board → server, on its config poll
{ "config_version": 4149505834 }

// server → board, already current  (the usual case, every minute, for ever)
"config": { "version": 4149505834 }

// server → board, wiring has changed
"config": {
  "version": 2216894401,
  "sample_interval_ms": 5000,     // how often an input is READ
  "analog_deadband": 2,           // % of full scale before it is worth sending
  "force_report_ms": 300000,      // send an unchanged value at least this often
  "watchdog_ms": 60000,           // prove the config works within this
  "pins": [
    {"channel": "led",    "pin": "D1", "mode": "output"},
    {"channel": "button", "pin": "D2", "mode": "input_pullup"},
    {"channel": "knob",   "pin": "A0", "mode": "analog"}
  ]
}
```

`version` is a crc32 of the map plus the rate settings, so it changes on any
edit and returns to its old value if an edit is undone. `pin` is the name
printed on the board, never a GPIO number: resolving `D1` is the job of the side
that was compiled against the board's core.

The rate limits exist because every value sent is a database row. A
potentiometer nobody is touching still jitters by a count or two, and at a 0%
deadband that jitter alone is ~17,000 rows a day per channel — and this firmware
makes it a two-click job to turn on nine of them. The floors are enforced on the
server *and* in the firmware: one copy protects the server from a hand-edited
build, the other protects every board from a mistake in configuration.

### Why a bad configuration cannot brick the board

Two independent mechanisms, because they catch different failures.

**Nothing touches a pin until the board is online.** On an ESP8266, D3 and D4
held LOW at power-on, or D8 held HIGH, stop the chip booting. Those pins are
offered — people use them deliberately — so a configuration that drives one has
to be survivable. It is, because `setup()` calls no `pinMode` at all: the
strapping pins are read by the bootloader in their default high-impedance state
on every single boot, whatever is configured. The map is applied only after a
successful check-in.

**A config that stops the board talking is rolled back.** After applying a new
version the board has `watchdog_ms` (60 s) to reach the server again. If it
does not, the last known-good map is restored from flash. This catches a pin
that browns out the radio, not a light on the wrong pin — no watchdog can know
you meant D2.

And a config that makes the board *reboot* never reaches the watchdog at all, so
attempts are counted in flash **before** the config is applied. After three the
version is refused until the wiring changes.

### LittleFS storage strategy

Two files, and which one is written when is the whole design.

| File | Holds | Written |
|---|---|---|
| `/ecse-config.json` | the last configuration **proved** to work | only on promotion, after the watchdog passes |
| `/ecse-trial.json` | `{"v": version, "n": attempts}` | **before** a new config is applied |

The known-good file is never overwritten by something unproven, which is what
makes rollback trivial — recovering is just reading it back. Saving on arrival
and repairing afterwards would mean the one file the board depends on is briefly
the file that might be broken.

The trial counter is written *before* applying for the same reason in reverse:
if applying reboots the board, the increment has already reached flash and the
next boot can see it. A counter written afterwards would be lost by exactly the
failure it exists to detect.

A board whose filesystem will not mount still runs — it fetches its config on
every boot — it just cannot remember one across a power cut. It is deliberately
**not** reformatted: that would destroy the known-good configuration, which is
the board's only recovery path, in response to what may be a transient mount
failure.

### Two things worth knowing

**Outputs start LOW after a reboot**, not restored to their last value. The board
has just come up and does not know what happened while it was away; guessing is
how a heater comes on in an empty house. The dashboard re-sends the desired state
on the next check-in, because it is stored server-side.

**A wiring change takes up to a minute** to reach the board. That is the config
poll interval — one small request per board per minute, about 120 bytes back
when nothing has changed.

---

## Troubleshooting

Open the **Serial Monitor at 115200 baud** first. The library narrates what it
is doing, and almost every problem below names itself there.

| What you see | What it means | Fix |
|---|---|---|
| `ElectroCSE: WiFi failed. Check the name and password.` | Wrong SSID/password — or a 5 GHz network | These boards are **2.4 GHz only**. Check the SSID's capitals and spaces. |
| `ElectroCSE: token rejected. It is wrong, revoked, or expired.` | The token doesn't match a device | Re-copy it from the device page. Copy the *whole* string including the `\|`. |
| `ElectroCSE: no handler for channel relay1` | Dashboard name ≠ sketch name | Make them identical, then **re-upload**. |
| Card says **Pending** for ever | The board never confirmed the command | Usually the line above. Also check `loop()` isn't blocked by `delay()`. |
| `ElectroCSE: rate limited; slowing down.` | Checking in too fast | Normal and self-correcting. The library backs off on its own. |
| `ElectroCSE: check-in failed, HTTP 500` | The dashboard answered with an error | Try again; if it persists, contact support with the device ID. |
| Negative HTTP status on a WiFiNINA board | The HTTPS certificate isn't trusted by the module | Use `http://` on a local server — [see below](#tls-is-encrypted-but-not-verified). |
| `ElectroCSE: no WiFi module found.` | The NINA radio isn't answering | Run the **WiFiNINA → FirmwareUpdater** sketch; the module's firmware is probably too old. |
| Nothing at all on Serial | Wrong baud rate, or a charge-only USB cable | Set 115200. Try another cable. |
| Board reboots whenever the servo moves | Brown-out — the servo is drawing more current than the regulator can give | Power the servo from a **separate 5 V supply** and join the grounds. |
| `ElectroCSE: too many channels. Raise ELECTROCSE_MAX_HANDLERS.` | More than 12 handlers | `#define ELECTROCSE_MAX_HANDLERS 20` before the include. |
| Compile error naming a WiFi type that doesn't exist | Wrong board selected in the IDE | Tools → Board. The library follows that menu. |
| `#error "ElectroCSE: this board is not supported…"` | An Uno, Nano or Mega is selected | Those boards have no networking hardware. Use an ESP8266, ESP32 or Nano 33 IoT. |
| `fatal error: PubSubClient.h: No such file` | The MQTT dependency isn't installed | Manage Libraries → **PubSubClient** by Nick O'Leary. Needed on every board, even on an HTTP dashboard — see [Installing](#installing-the-library). |
| `ElectroCSE: broker refused us, rc=5` | The broker rejected the token | rc 5 is "not authorised". Re-copy the token; if it was revoked, generate a new one and re-upload. |
| `ElectroCSE: broker refused us, rc=-2` | Can't reach the broker at all | Wrong address, a firewall, or port 8883 blocked. School and office networks block it routinely. |
| `ElectroCSE: falling back to HTTP.` | Five broker attempts failed in a row | **Not a fault to fix in your sketch.** The board keeps working over HTTP and retries MQTT by itself. Chase the broker, not the board. |
| `ElectroCSE: server offered MQTT with fields missing` | The dashboard's broker settings are incomplete | An operator needs to set the broker host in admin settings. The board stays on HTTP meanwhile. |
| Commands feel slow (1–2 s) on an MQTT dashboard | You're on the HTTP fallback without noticing | `Serial.println(ElectroCSE.transport())` says which. If it prints `http` on an MQTT platform, look for the fallback line above it. |
| Board connects but a WiFiNINA board never reaches the broker over TLS | The NINA module doesn't trust the broker's certificate | There is no `setInsecure()` on this radio — [see below](#tls-is-encrypted-but-not-verified). Use a broker with a public certificate, or a plain-`1883` deployment. |

---

## Frequently asked questions

**Is it free?**
The library is MIT-licensed and free. See
[iot.electrocse.com](https://iot.electrocse.com) for the dashboard's own terms.

**Do I need to set up port forwarding or a static IP?**
No. Your board makes an outgoing HTTPS request, exactly like a browser does, so
it works from behind any home router, phone hotspot or school network.

**Will it work on an Arduino Uno / Nano / Mega?**
No, and it can't — those boards have no networking hardware at all. The build
stops with a message saying so, rather than failing at run time. An ESP8266
NodeMCU costs less than an Uno and is the usual answer.

**Does it work with a 5 GHz Wi-Fi network?**
No. ESP8266, ESP32 and NINA radios are all **2.4 GHz only**. If your router
publishes one name for both bands, that is a very common cause of "WiFi failed".

**Can I use it with Blynk / ThingSpeak / Firebase?**
They can coexist in one sketch, but there is no bridge between them. This
library talks to the ElectroCSE dashboard only.

**Why channel names instead of virtual pins?**
`temperature` says what it is; `V5` doesn't. Names match what your dashboard
and your database already use, and the widget form offers the channels your
board has actually published, so a typo shows up as a new channel you can *see*
rather than as data quietly landing on the wrong widget.

**How often does it talk to the server?**
The server decides, and tells the board with every reply — about once a second
while somebody has the dashboard open, about every thirty seconds when nobody
does. You can re-tune a whole classroom of boards without reflashing any of
them.

**Does it use MQTT?**
Not in this release. See [How it works](#how-it-works) for why, and what the
device page's generated sketch does instead.

**Can I run it against my own server?**
Yes — `#define ELECTROCSE_IOT_SERVER "http://192.168.1.50"` *before* the
include.

**Is there a Python version?**
Yes, for Raspberry Pi:
[ElectroCSE-IoT-Python](https://github.com/electrocse/ElectroCSE-IoT-Python)
(early — the boundary is in place, the client is being written).

**How much RAM does it use?**
No heap allocation at all. Handlers, timers and queued readings live in
fixed-size arrays sized by `#define`, so memory use is decided at compile time
and cannot fragment over weeks of uptime.

---

## How it works

Your board's first act is one HTTPS request that carries both directions:

```
    Board                                        Dashboard
      |  POST /api/v1/sync                            |
      |  Authorization: Bearer ecse_iot_12|k3P...     |
      |  { readings, state }                     ---> |
      |                                               |
      |  <--- { commands, next_poll_ms, transport }   |
```

Your readings go up, any pending switch or slider values come back down, and the
reply also says when to come back. Because the board opens the connection, there
is nothing to forward, expose or configure on your network.

### HTTP or MQTT — the server decides, and your sketch never says

That last field, `transport`, is what makes one sketch work on any ElectroCSE
deployment. Some run over plain HTTP, which works on any host including shared
cPanel hosting. Others run an MQTT broker, which delivers a button press in
milliseconds instead of a second or two.

**Nothing in your sketch chooses.** There is no `#define`, no setting and no
second version of the file. On the first check-in the server names the transport
it runs; on an MQTT deployment the library connects to the broker, subscribes,
and stops polling:

```
ElectroCSE: posting to https://iot.electrocse.com/api/v1/sync
ElectroCSE: switching to MQTT at broker.electrocse.com:8883 (TLS)
ElectroCSE: broker connected.
```

Ask at any time which one you ended up on:

```cpp
Serial.println(ElectroCSE.transport());     // "http" or "mqtt"
```

Three consequences worth knowing:

- **Your sketch is byte-for-byte identical on both.** So is every example in this
  repository, and so is the code on your device page.
- **An operator can switch the whole platform over without anyone reflashing a
  board.** Boards find out on their next check-in.
- **If the broker goes down, boards fall back to HTTP** rather than going dark.
  After five failed connections the library returns to polling, keeps reporting,
  and retries MQTT on the next check-in. Slower, but still working.

The endpoint is `/api/v1/sync`, not the older `/api/device/telemetry`. Both
accept readings and both answer with commands, so either *appears* to work —
but only `/api/v1/sync` validates a text reading, and the older route drops one
silently with nothing anywhere saying why.

---

## Things worth knowing

**The dashboard address is built in.** `ELECTROCSE_IOT_SERVER` defaults to
`iot.electrocse.com`, so a normal sketch never mentions it. To point at your own
machine:

```cpp
#define ELECTROCSE_IOT_SERVER "http://192.168.1.50"   // BEFORE the include
#include <ElectroCSE.h>
```

A bare hostname is treated as `https`; give the scheme for plain `http`. It must
come before the include, because the header reads the macro. It is applied
inside `connect()`, which is defined **inline in the header** so the macro
expands in your sketch's own translation unit. Move that method into the `.cpp`
and every board silently posts to the default instead — compiling and connecting
perfectly on the way.

**You never have to acknowledge a command.** `ELECTROCSE_LISTEN` records what you
applied and echoes it on the next check-in, which is what clears the card's
"Pending" badge. Forgetting that echo is the commonest first-project bug in
hand-written clients, and it presents as a card stuck on Pending for ever with
nothing else wrong.

**The server sets the pace.** It knows whether somebody is watching the
dashboard and your board does not, so `next_poll_ms` comes back with every
reply.

**TLS is encrypted but not verified.** `setInsecure()` stops passive sniffing,
not an active man-in-the-middle. Pinning ISRG Root X1 is the upgrade and needs
NTP time on the board, because a certificate cannot be date-checked without a
clock. Never pin the Let's Encrypt *fingerprint* instead — it changes every 90
days, and every board in the field would go dark the morning it does.

**No heap allocation.** Overflow is reported on Serial rather than ignored.
Raise `ELECTROCSE_MAX_HANDLERS` (12), `ELECTROCSE_MAX_TIMERS` (6) or
`ELECTROCSE_MAX_QUEUED` (12) with a `#define` before the include.

---

## Project layout

```
src/
├── ElectroCSE.h            the ONE include — a router, no implementation
├── ElectroCSE_Core.h       the class, the macros, the check-in state machine
├── ElectroCSE_ESP.h        ESP8266 / ESP32 radio
├── ElectroCSE_WiFiNINA.h   Nano 33 IoT / MKR 1010 / Nano RP2040 radio
├── ElectroCSE_Mqtt.h       the MQTT protocol, shared by every board
├── ElectroCSE_Json.h       the ArduinoJson 6/7 seam — why the stack, not the heap
└── ElectroCSE.cpp          the shared half, compiled once per sketch

examples/
├── ESP8266/{01_Basic_Blink, 02_Moderate_LDR_Relay, 03_Advanced_Servo_Dashboard}
├── ESP32/{01_Basic_Blink, 02_Moderate_LDR_Relay, 03_Advanced_Servo_Dashboard}
├── WiFiNINA/{01_Basic_Blink, 02_Moderate_LDR_Relay, 03_Advanced_Servo_Dashboard}
└── Generic/ElectroCSE_Generic

library.properties          Arduino Library Manager
library.json                PlatformIO
keywords.txt                IDE syntax colouring
CHANGELOG.md                what changed, and in which version
.github/workflows/          every example compiled on every board, per push
```

`ElectroCSE.h` contains no code — it looks at what the IDE selected and includes
one radio header, or stops the build with a message naming the boards that do
work.

**There are two independent axes here, and keeping them apart is what stops this
turning into six files.** A *radio* is how one board family talks to a network,
and owns exactly three methods (`ensureWiFi`, `connectImpl`, `checkIn`). A
*protocol* is what it says once connected, and MQTT owns one (`transportLoop`).
The MQTT engine is identical on every board — only the TLS client class differs
— so it is written once and each radio header names its two client types before
including it. Everything else is shared, so a fix to the back-off or the echo
reaches every board and both protocols at once.

**Adding a board family** is: write `src/ElectroCSE_<Family>.h`, typedef its two
network clients, `#include "ElectroCSE_Mqtt.h"`, add a branch to the router, add
the architecture to `library.properties`. Nothing else changes, and MQTT works
on the new board without being thought about.

---

## Supported boards

| Board | Core | Extra libraries |
|---|---|---|
| ESP8266 — NodeMCU, Wemos D1 Mini | `esp8266:esp8266` | ArduinoJson |
| ESP32 — DevKit, WROOM, S3 | `esp32:esp32` | ArduinoJson |
| Nano 33 IoT, MKR WiFi 1010 | `arduino:samd` | ArduinoJson, **WiFiNINA**, **ArduinoHttpClient** |
| Nano RP2040 Connect | `arduino:mbed_nano` | ArduinoJson, **WiFiNINA**, **ArduinoHttpClient** |

The Arduino boards need two extra libraries because the SAMD core ships no
networking at all — the radio is a separate NINA-W102 co-processor and those are
its drivers. They cost an ESP nothing but a folder, because the router never
includes them.

### TLS is encrypted but not verified

One difference worth knowing before you debug it. On an ESP,
`WiFiClientSecure::setInsecure()` skips certificate validation, so a self-signed
certificate on your own server works. On a WiFiNINA board there is no such call:
`WiFiSSLClient` validates against a root store burned into the module and cannot
be told not to. A private HTTPS server simply refuses to connect, and it is
reported as a *negative HTTP status* rather than as a certificate error. Use
`http://` on the bench.

**AVR boards — Uno, Nano, Mega — are not supported and cannot be.** They have no
network hardware. The build stops with a message saying so.

---

## Links

- **Dashboard** — [iot.electrocse.com](https://iot.electrocse.com)
- **Support** — [support.electrocse.com](https://support.electrocse.com)
- **Python client for Raspberry Pi** — [ElectroCSE-IoT-Python](https://github.com/electrocse/ElectroCSE-IoT-Python)
- **Report a bug** — [Issues](https://github.com/electrocse/ElectroCSE-IoT/issues)
- **What changed** — [CHANGELOG.md](CHANGELOG.md)

## Licence

MIT — see [`LICENSE`](LICENSE). Use it in coursework, in products, anywhere.

<sub>Keywords: Arduino IoT library · ESP8266 IoT dashboard · ESP32 web dashboard ·
NodeMCU sensor monitoring · Arduino Nano 33 IoT WiFi · MKR WiFi 1010 · IoT
dashboard for students · control Arduino from browser · send sensor data to web
· Arduino IoT project · ESP32 relay control over internet</sub>
