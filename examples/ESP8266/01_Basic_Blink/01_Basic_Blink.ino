/*
 * ElectroCSE IoT — ESP8266 · 01 Basic · Blink
 * =============================================================================
 * The smallest sketch that does something you can see: one switch on your
 * dashboard turns the NodeMCU's built-in LED on and off.
 *
 * ON THE DASHBOARD
 *   Add a Toggle switch widget on the channel  relay
 *
 * WIRING
 *   None. The built-in LED is already on the board.
 *
 * NOTE THE LED IS BACKWARDS. On a NodeMCU the built-in LED is wired to 3.3V
 * and pulled DOWN to light, so LOW is on and HIGH is off. Getting this the
 * usual way round is the commonest surprise on this board, and it looks like
 * the command arriving inverted rather than like the wiring it is.
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
 * "When the dashboard changes `relay`, run this."
 *
 * You never call this yourself. The library also records what you applied and
 * echoes it back on the next check-in, which is what clears the card's
 * "Pending" badge - so there is no acknowledgement to remember to send.
 */
ELECTROCSE_LISTEN(relay) {
  digitalWrite(LED_BUILTIN, param.isOn() ? LOW : HIGH);   // LOW = lit, see above
  Serial.print(F("relay -> "));
  Serial.println(param.isOn() ? F("on") : F("off"));
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);      // start dark

  ElectroCSE.connect(ELECTROCSE_IOT_TOKEN, ssid, pass);

  // Something to look at while you wait: signal strength, every ten seconds.
  // every() does not block, unlike delay().
  ElectroCSE.every(10000, []() {
    ElectroCSE.send("rssi", WiFi.RSSI(), "dBm");
  });
}

void loop() {
  ElectroCSE.loop();
}
