# Changelog

All notable changes to this library are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The version in `library.properties`, in `library.json` and on the git tag must
always be the same string — the Arduino Library Manager reads the tag, and
PlatformIO reads `library.json`, so a mismatch publishes two different things
under one version number.

## [Unreleased]

## [0.1.0] — 2026-09-17

First release.

### Added

- One include, `<ElectroCSE.h>`, which picks the transport for whichever board
  is selected under **Tools → Board** and refuses at compile time, with a
  sentence rather than a link error, for a board that has no radio.
- ESP8266 and ESP32 support over the core's own WiFi and HTTP stacks.
- WiFiNINA support (Nano 33 IoT, MKR WiFi 1010, Nano RP2040 Connect) through
  `ArduinoHttpClient`.
- HTTP and MQTT transports, chosen **by the server** at run time from the
  `transport` block in a check-in reply — so one sketch is byte-for-byte the
  same on either deployment, and an operator can move a field of boards without
  re-flashing any of them. Five failed broker connections fall back to HTTP,
  which is also how a board learns the deployment has moved back.
- `send()` for readings, `ELECTROCSE_LISTEN` for commands, `every()` for
  timers, `ELECTROCSE_LIVE` for first contact, and `onAny()` for channels
  nothing else claimed.
- The applied-value echo that clears the dashboard's **Pending** badge — and
  `onAny()` returning `false` so a sketch that could not act does not claim it
  did.
- Adaptive polling: the server sets the pace through `next_poll_ms`, clamped to
  0.5–300 s, with exponential back-off on failure and a 60 s slow-down on 401
  and 429.
- Generic firmware example: wiring configured from the dashboard rather than
  compiled in, stored in LittleFS.
- Nine example sketches — basic, moderate and advanced, per board family.

### Notes

- `StaticJsonDocument` is used deliberately rather than ArduinoJson 7's heap
  `JsonDocument`; see `src/ElectroCSE_Json.h` for the ESP8266 fragmentation
  reasoning and for what a future v8 port should do instead.
- TLS is encrypted but **not** certificate-verified on ESP boards
  (`setInsecure()`), which stops passive sniffing and not an active
  man-in-the-middle. WiFiNINA validates against the root store burned into the
  module and cannot be turned off.

[Unreleased]: https://github.com/electrocse/ElectroCSE-IoT/compare/0.1.0...HEAD
[0.1.0]: https://github.com/electrocse/ElectroCSE-IoT/releases/tag/0.1.0
