/*
 * ElectroCSE IoT — ESP32 · 01 Basic · Blink
 * =============================================================================
 * The smallest sketch that does something you can see: one switch on your
 * dashboard turns the on-board LED on and off.
 *
 * ON THE DASHBOARD
 *   Add a Toggle switch widget on the channel  relay
 *
 * WIRING
 *   None on most dev boards - LED_BUILTIN is GPIO 2.
 *   If nothing lights up, your board has no user LED (plenty do not). Put an
 *   LED and a 220R resistor on GPIO 2 to GND, or change PIN_LED below.
 *
 * UNLIKE A NodeMCU, THE ESP32's LED IS THE RIGHT WAY ROUND: HIGH lights it.
 * The ESP8266 examples in this library invert it, and that difference is the
 * board, not the library.
 *
 * Dashboard : https://iot.electrocse.com
 * Support   : https://support.electrocse.com
 */

#define ELECTROCSE_IOT_TOKEN "PASTE_YOUR_TOKEN_HERE"

// Testing against your own machine? Uncomment and change. Otherwise the
// library already points at the dashboard.
// #define ELECTROCSE_IOT_SERVER "http://192.168.1.50"

#include <ElectroCSE.h>

char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

/*
 * Not every ESP32 variant defines LED_BUILTIN - an ESP32-C3 or a bare module
 * does not - and referring to it directly would fail to compile there with an
 * error about an undeclared identifier, which reads as a broken example.
 */
#ifdef LED_BUILTIN
  #define PIN_LED LED_BUILTIN
#else
  #define PIN_LED 2
#endif

/*
 * "When the dashboard changes `relay`, run this."
 *
 * You never call this yourself. The library also records what you applied and
 * echoes it back on the next check-in, which is what clears the card's
 * "Pending" badge - so there is no acknowledgement to remember to send.
 */
ELECTROCSE_LISTEN(relay) {
  digitalWrite(PIN_LED, param.isOn() ? HIGH : LOW);
  Serial.print("relay -> ");
  Serial.println(param.isOn() ? "on" : "off");
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  ElectroCSE.connect(ELECTROCSE_IOT_TOKEN, ssid, pass);

  // Something to look at while you wait. every() does not block, unlike delay().
  ElectroCSE.every(10000, []() {
    ElectroCSE.send("rssi", WiFi.RSSI(), "dBm");
  });
}

void loop() {
  ElectroCSE.loop();
}
