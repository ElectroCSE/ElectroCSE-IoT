/*
 * ElectroCSE IoT — ESP8266 · 02 Moderate · LDR + Relay
 * =============================================================================
 * A sensor going up and a switch coming down, at the same time.
 *
 * Reads a light-dependent resistor on a timer and reports it, while a relay
 * output stays controllable from the dashboard.
 *
 * ON THE DASHBOARD
 *   Line chart   on channel  light
 *   Status card  on channel  light
 *   Toggle       on channel  relay
 *
 * WIRING
 *   LDR      one leg to A0, the other to 3V3
 *   10k      from A0 to GND        (the divider - without it A0 floats and
 *                                   reads noise that looks like a broken sensor)
 *   Relay IN to D1                 (or an LED with a 220R resistor to GND)
 *
 * WHY AN LDR AND NOT A DHT11. This needs no library at all. A DHT11 is the
 * obvious swap once this works: install "DHT sensor library" by Adafruit, then
 * send dht.readTemperature() and dht.readHumidity() from the same timer.
 *
 * A0 ON A NodeMCU IS 0-1023 AND TOPS OUT AT 3.3V ON THE BOARD, even though the
 * ESP8266 chip itself only tolerates 1V - the NodeMCU has a divider fitted. On
 * a bare ESP-12 module it does not, and 3.3V into A0 destroys the pin.
 *
 * Dashboard : https://iot.electrocse.com
 */

#define ELECTROCSE_IOT_TOKEN "PASTE_YOUR_TOKEN_HERE"

// #define ELECTROCSE_IOT_SERVER "http://192.168.1.50"

#include <ElectroCSE.h>

char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

#define PIN_LDR    A0
#define PIN_RELAY  D1

ELECTROCSE_LISTEN(relay) {
  digitalWrite(PIN_RELAY, param.isOn() ? HIGH : LOW);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  ElectroCSE.connect(ELECTROCSE_IOT_TOKEN, ssid, pass);

  /*
   * every() rather than delay().
   *
   * delay(5000) stops the whole sketch, including the check-in that carries
   * your commands - so a relay would take up to five seconds to respond and
   * would feel broken. This runs the block when it is due and returns
   * immediately the rest of the time.
   */
  ElectroCSE.every(5000, []() {
    const int raw = analogRead(PIN_LDR);           // 0..1023

    // Sent as a percentage rather than raw counts: "62" means something to
    // somebody reading the dashboard, "634" does not.
    ElectroCSE.send("light", map(raw, 0, 1023, 0, 100), "%");
  });

  ElectroCSE.every(30000, []() {
    ElectroCSE.send("rssi", WiFi.RSSI(), "dBm");
  });
}

void loop() {
  ElectroCSE.loop();
}
