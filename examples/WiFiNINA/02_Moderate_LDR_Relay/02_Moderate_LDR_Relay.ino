/*
 * ElectroCSE IoT — WiFiNINA · 02 Moderate · LDR + Relay
 * =============================================================================
 * Arduino Nano 33 IoT · MKR WiFi 1010 · Nano RP2040 Connect
 *
 * A sensor going up and a switch coming down, at the same time.
 *
 * ON THE DASHBOARD
 *   Line chart   on channel  light
 *   Toggle       on channel  relay
 *
 * WIRING
 *   LDR      one leg to A0, the other to 3V3
 *   10k      from A0 to GND        (the divider - without it A0 floats and
 *                                   reads noise that looks like a broken sensor)
 *   Relay IN to D2                 (or an LED with a 220R resistor to GND)
 *
 * THESE BOARDS ARE 3.3V, NOT 5V. A Nano 33 IoT looks like a Nano and is not
 * one: 5V on any pin damages it. A relay module rated for 5V logic often will
 * not trigger from 3.3V either - check the module, or drive it through a
 * transistor.
 *
 * THE ADC IS 10-BIT BY DEFAULT (0-1023) but this chip can do 12. The default is
 * kept here so the map() matches every other 10-bit example; analogReadResolution(12)
 * changes it, and then the 1023 below has to become 4095 or every reading is
 * quartered.
 *
 * INSTALL FIRST: WiFiNINA, ArduinoHttpClient, ArduinoJson.
 *
 * Dashboard : https://iot.electrocse.com
 */

#define ELECTROCSE_IOT_TOKEN "PASTE_YOUR_TOKEN_HERE"

// #define ELECTROCSE_IOT_SERVER "http://192.168.1.50"

#include <ElectroCSE.h>

char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

#define PIN_LDR    A0
#define PIN_RELAY  2

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
    const int raw = analogRead(PIN_LDR);           // 0..1023 at the default resolution

    ElectroCSE.send("light", map(raw, 0, 1023, 0, 100), "%");
  });

  ElectroCSE.every(30000, []() {
    ElectroCSE.send("rssi", WiFi.RSSI(), "dBm");
  });
}

void loop() {
  ElectroCSE.loop();
}
