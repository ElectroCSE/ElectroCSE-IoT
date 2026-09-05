/*
 * ElectroCSE IoT — WiFiNINA · 03 Advanced · Servo Dashboard
 * =============================================================================
 * Arduino Nano 33 IoT · MKR WiFi 1010 · Nano RP2040 Connect
 *
 * Several readings going up, two kinds of control coming down, and a hook that
 * fires the moment the board first reaches the dashboard.
 *
 * ON THE DASHBOARD
 *   Line chart   on  light
 *   Status card  on  rssi     and on  uptime
 *   Toggle       on  relay
 *   Slider       on  servo     (range 0 to 180)
 *
 * WIRING
 *   LDR + 10k divider to A0
 *   Relay IN          to D2
 *   Servo signal      to D3
 *   Servo power       to an external 5V supply and GND - NOT to the board
 *
 * POWER IS THE THING THAT BITES HERE. A servo pulls a few hundred milliamps
 * when it moves and these boards cannot supply that. Powered from the board,
 * the servo twitches and the processor browns out and resets - which presents
 * as the board randomly going offline, not as a power problem. Give the servo
 * its own supply and join the grounds.
 *
 * -----------------------------------------------------------------------------
 * ONE NAME TO AVOID ON THIS ARCHITECTURE
 * -----------------------------------------------------------------------------
 * Do not call anything `PORT`. On SAMD it is a register defined by Atmel's CMSIS
 * headers, and shadowing it fails deep inside samd21g18a.h with errors that
 * name a file you have never opened and say nothing about your sketch. The same
 * goes for `PM`, `DAC` and `TC3`. It is the one gotcha that costs an afternoon.
 *
 * INSTALL FIRST: WiFiNINA, ArduinoHttpClient, ArduinoJson.
 * Servo ships with the Arduino IDE.
 *
 * Dashboard : https://iot.electrocse.com
 */

#define ELECTROCSE_IOT_TOKEN "PASTE_YOUR_TOKEN_HERE"

// #define ELECTROCSE_IOT_SERVER "http://192.168.1.50"

#include <ElectroCSE.h>
#include <Servo.h>

char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

#define PIN_LDR    A0
#define PIN_RELAY   2
#define PIN_SERVO   3

Servo arm;

/* ----------------------------------------------------------- coming down -- */

ELECTROCSE_LISTEN(relay) {
  digitalWrite(PIN_RELAY, param.isOn() ? HIGH : LOW);
}

/*
 * A slider, not a switch - so read the number rather than asking isOn().
 *
 * constrain() is not defensive padding: the channel carries whatever the
 * dashboard sends, and a widget configured 0-1000 by mistake would otherwise
 * drive the servo past its stop and stall it against the end of its travel,
 * which is how a servo cooks itself.
 */
ELECTROCSE_LISTEN(servo) {
  const int angle = constrain(param.asInt(), 0, 180);
  arm.write(angle);

  Serial.print("servo -> ");
  Serial.println(angle);
}

/* ----------------------------------------------------------- going up ----- */

ELECTROCSE_LIVE() {
  /*
   * Runs ONCE, the first time this board reaches the dashboard.
   *
   * Different from isLive(), which you can ask at any moment: this is the
   * event, that is the state. It is the right place for a startup announcement
   * or for parking hardware in a known position, because until it fires the
   * dashboard has never heard from this board.
   */
  Serial.println("We are live - the dashboard can see this board now.");

  arm.write(90);                                   // centre the servo
  ElectroCSE.send("status", "ready");              // a text reading
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  arm.attach(PIN_SERVO);

  ElectroCSE.connect(ELECTROCSE_IOT_TOKEN, ssid, pass);

  // Three streams on two schedules. Light changes fast and is worth watching;
  // signal and uptime are context and do not need the same resolution.
  ElectroCSE.every(2000, []() {
    ElectroCSE.send("light", map(analogRead(PIN_LDR), 0, 1023, 0, 100), "%");
  });

  ElectroCSE.every(15000, []() {
    ElectroCSE.send("rssi",   WiFi.RSSI(), "dBm");
    ElectroCSE.send("uptime", (millis() / 1000), "s");
  });
}

void loop() {
  ElectroCSE.loop();
}
