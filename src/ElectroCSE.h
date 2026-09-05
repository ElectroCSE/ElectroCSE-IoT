/*
 * =============================================================================
 *  ElectroCSE IoT  —  Arduino library
 * =============================================================================
 *  Connects a Wi-Fi board to your ElectroCSE IoT dashboard.
 *
 *      #define ELECTROCSE_IOT_TOKEN "paste your token"
 *      #include <ElectroCSE.h>
 *
 *  THREE THINGS TO FILL IN, and only three: your Wi-Fi name, your Wi-Fi
 *  password, and the token from your device page.
 *
 *  The address is NOT one of them. It defaults to iot.electrocse.com, so a
 *  sketch aimed at the real dashboard says nothing about it. Add
 *
 *      #define ELECTROCSE_IOT_SERVER "http://192.168.1.50"
 *
 *  BEFORE this include only when you are testing against your own machine.
 *  A bare hostname is treated as https; give the scheme for plain http.
 *
 *  Dashboard : https://iot.electrocse.com
 *  Contact   : https://electrocse.com/contact
 *  Support   : https://support.electrocse.com
 *
 * -----------------------------------------------------------------------------
 *  THIS FILE IS A ROUTER. IT CONTAINS NO IMPLEMENTATION.
 * -----------------------------------------------------------------------------
 *  Everything below is preprocessor. The job here is to look at what the IDE
 *  selected under Tools -> Board and include the one transport header that can
 *  talk to that silicon - and to fail loudly, at compile time, when it cannot.
 *
 *  WHY A ROUTER RATHER THAN A HEADER PER BOARD
 *  -------------------------------------------
 *  Blynk ships BlynkSimpleEsp8266.h, BlynkSimpleEsp32.h, BlynkSimpleWiFiNINA.h
 *  and so on, and asking a beginner to pick the right one is exactly the
 *  friction this library exists to remove: choosing wrongly produces a wall of
 *  errors about a WiFi type that does not exist, which reads as the library
 *  being broken rather than as the wrong line having been typed. There is one
 *  include, and the compiler works the rest out.
 *
 *  WHY A ROUTER RATHER THAN #if BLOCKS SPREAD THROUGH ONE BIG HEADER
 *  ----------------------------------------------------------------
 *  The transports are not variations on a theme. An ESP has WiFi and HTTP in
 *  its own core and reaches TLS through WiFiClientSecure::setInsecure(); a
 *  WiFiNINA board has a separate radio co-processor, needs ArduinoHttpClient,
 *  and validates TLS against a root store burned into the module that cannot
 *  be turned off. Interleaving those in one file means every reader steps over
 *  code for hardware they do not own, and every edit risks the other board.
 *
 *  ADDING A BOARD FAMILY is: write src/ElectroCSE_<Family>.h implementing the
 *  three transport methods named in ElectroCSE_Core.h, add a branch below, and
 *  add the architecture to library.properties. Nothing else changes.
 * =============================================================================
 */

#ifndef ELECTROCSE_H
#define ELECTROCSE_H

/*
 * The shared half: the class, the parameter reader, the macros and the
 * check-in state machine. Included FIRST and by name rather than left to the
 * transport headers, so every branch below starts from the same declaration
 * and none of them can quietly grow its own.
 */
#include "ElectroCSE_Core.h"

/* -------------------------------------------------------------------------- */
/*  The routing table                                                         */
/* -------------------------------------------------------------------------- */

#if defined(ESP8266) || defined(ESP32)

  /*
   * Espressif. One header for both: an ESP8266 and an ESP32 differ in which
   * WiFi and HTTP headers their cores publish, and in nothing else this
   * library does - so the difference is a handful of #if lines inside that
   * file rather than two near-identical files that drift apart.
   */
  #include "ElectroCSE_ESP.h"

#elif defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_ARCH_SAM) || defined(ARDUINO_ARCH_MBED)

  /*
   * WiFiNINA boards: Nano 33 IoT, MKR WiFi 1010, Nano RP2040 Connect.
   *
   * Keyed on the ARCHITECTURE and not on a board name, because the thing that
   * decides the transport is the co-processor radio and the core's API, and
   * those are the same across every board in the family. A list of board
   * defines would need editing every time Arduino ships another one.
   *
   * ARDUINO_ARCH_MBED covers the Nano RP2040 Connect, which is mbed rather
   * than SAMD but carries the same NINA-W102 module and the same library.
   */
  #include "ElectroCSE_WiFiNINA.h"

#else

  /*
   * A compile-time refusal, deliberately, and worth the words.
   *
   * The alternative - letting it through and failing at link time - produces
   * "undefined reference to ElectroCseClass::checkIn()" from a file the user
   * never opened. This says what is wrong and what to do about it, at the line
   * that caused it.
   *
   * An AVR board (Uno, Nano, Mega) reaches here and always will: it has no
   * network hardware of its own, and an Uno with an ESP-01 wired to it is
   * programming the ESP, not the Uno.
   */
  #error "ElectroCSE: this board is not supported. Select an ESP8266, an ESP32, or a WiFiNINA board (Nano 33 IoT, MKR WiFi 1010, Nano RP2040 Connect) under Tools -> Board. A plain Uno/Nano/Mega has no network hardware and cannot be used."

#endif

#endif  // ELECTROCSE_H
