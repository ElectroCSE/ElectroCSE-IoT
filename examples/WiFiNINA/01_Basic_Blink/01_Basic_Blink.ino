/*
 * ElectroCSE IoT — WiFiNINA · 01 Basic · Blink
 * =============================================================================
 * Arduino Nano 33 IoT · MKR WiFi 1010 · Nano RP2040 Connect
 *
 * The smallest sketch that does something you can see: one switch on your
 * dashboard turns the on-board LED on and off.
 *
 * ON THE DASHBOARD
 *   Add a Toggle switch widget on the channel  relay
 *
 * WIRING
 *   None. LED_BUILTIN is on the board and is the right way round: HIGH lights it.
 *
 * INSTALL FIRST  (Tools -> Manage Libraries...)
 *   WiFiNINA           by Arduino
 *   ArduinoHttpClient  by Arduino
 *   ArduinoJson        by Benoit Blanchon
 *
 * Three, where an ESP needs one. The SAMD core ships no networking of its own -
 * the radio is a separate NINA-W102 co-processor and those are its drivers.
 *
 * IF NOTHING CONNECTS AND THE SERIAL MONITOR SAYS "no WiFi module found", the
 * NINA firmware is too old or the module is not responding. Tools -> WiFi101 /
 * WiFiNINA Firmware Updater will tell you which, and fix the first.
 *
 * Dashboard : https://iot.electrocse.com
 * Support   : https://support.electrocse.com
 */

#define ELECTROCSE_IOT_TOKEN "PASTE_YOUR_TOKEN_HERE"

// Testing against your own machine? Uncomment and change. Otherwise the
// library already points at the dashboard.
//
// NOTE FOR THIS BOARD: a WiFiSSLClient validates certificates against a root
// store burned into the NINA module and CANNOT be told to skip the check. A
// private server with a self-signed certificate will not connect at all. Use
// http:// for a machine on your bench.
// #define ELECTROCSE_IOT_SERVER "http://192.168.1.50"

#include <ElectroCSE.h>

char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

/*
 * "When the dashboard changes `relay`, run this."
 *
 * You never call this yourself. The library also records what you applied and
 * echoes it back on the next check-in, which is what clears the card's
 * "Pending" badge - so there is no acknowledgement to remember to send.
 */
ELECTROCSE_LISTEN(relay) {
  digitalWrite(LED_BUILTIN, param.isOn() ? HIGH : LOW);
  Serial.print("relay -> ");
  Serial.println(param.isOn() ? "on" : "off");
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  ElectroCSE.connect(ELECTROCSE_IOT_TOKEN, ssid, pass);

  // Something to look at while you wait. every() does not block, unlike delay().
  ElectroCSE.every(10000, []() {
    ElectroCSE.send("rssi", WiFi.RSSI(), "dBm");
  });
}

void loop() {
  ElectroCSE.loop();
}
