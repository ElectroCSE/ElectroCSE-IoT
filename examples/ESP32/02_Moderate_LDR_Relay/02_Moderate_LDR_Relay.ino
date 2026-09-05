/*
 * ElectroCSE IoT — ESP32 · 02 Moderate · LDR + Relay
 * =============================================================================
 * A sensor going up and a switch coming down, at the same time.
 *
 * ON THE DASHBOARD
 *   Line chart   on channel  light
 *   Toggle       on channel  relay
 *
 * WIRING
 *   LDR      one leg to GPIO 34, the other to 3V3
 *   10k      from GPIO 34 to GND     (the divider - without it the pin floats
 *                                     and reads noise that looks like a fault)
 *   Relay IN to GPIO 5
 *
 * GPIO 34 IS INPUT-ONLY AND THAT IS WHY IT IS USED HERE. On an ESP32 the ADC2
 * pins stop working the moment WiFi is on - a reading from one silently
 * returns garbage or blocks, and every symptom points at the sensor. GPIO 32-39
 * are ADC1 and are safe. This is the single most common ESP32 analog mistake.
 *
 * THE ADC IS 12-BIT (0-4095), not 10-bit like an ESP8266's. Copying an ESP8266
 * sketch across without changing the map() gives readings that never rise above
 * a quarter scale.
 *
 * Dashboard : https://iot.electrocse.com
 */

#define ELECTROCSE_IOT_TOKEN "PASTE_YOUR_TOKEN_HERE"

// #define ELECTROCSE_IOT_SERVER "http://192.168.1.50"

#include <ElectroCSE.h>

char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

#define PIN_LDR    34      // ADC1, input-only. See the note above.
#define PIN_RELAY   5

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
   * would feel broken.
   */
  ElectroCSE.every(5000, []() {
    const int raw = analogRead(PIN_LDR);           // 0..4095 on an ESP32

    ElectroCSE.send("light", map(raw, 0, 4095, 0, 100), "%");
  });

  ElectroCSE.every(30000, []() {
    ElectroCSE.send("rssi", WiFi.RSSI(), "dBm");
  });
}

void loop() {
  ElectroCSE.loop();
}
