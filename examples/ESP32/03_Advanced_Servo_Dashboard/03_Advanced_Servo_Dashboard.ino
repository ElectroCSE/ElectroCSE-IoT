/*
 * ElectroCSE IoT — ESP32 · 03 Advanced · Servo Dashboard
 * =============================================================================
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
 *   LDR + 10k divider to GPIO 34   (ADC1 - see 02_Moderate_LDR_Relay for why)
 *   Relay IN          to GPIO 5
 *   Servo signal      to GPIO 18
 *   Servo power       to 5V and GND - NOT to the 3V3 pin, and join the grounds
 *
 * -----------------------------------------------------------------------------
 * WHY THIS DRIVES THE SERVO BY HAND INSTEAD OF USING Servo.h
 * -----------------------------------------------------------------------------
 * The Servo library bundled with the AVR and SAMD cores does not support the
 * ESP32 - Arduino-ESP32 ships no Servo at all, and the usual answer is to
 * install a third-party ESP32Servo library. This example uses the LEDC hardware
 * the chip already has, so there is nothing to install and nothing to go stale.
 *
 * A hobby servo wants a pulse every 20ms (50Hz) whose WIDTH carries the angle:
 * roughly 500us at 0 degrees to 2500us at 180. At 16-bit resolution one period
 * is 65536 counts, so a pulse of `us` microseconds is us * 65536 / 20000 counts.
 * That is the whole of angleToDuty() below.
 *
 * (ledcAttach(pin, freq, bits) is the Arduino-ESP32 3.x API. On 2.x it was
 * ledcSetup(channel, freq, bits) plus ledcAttachPin(pin, channel) - if your
 * core is older, that is the change, and it is the only one.)
 *
 * Dashboard : https://iot.electrocse.com
 */

#define ELECTROCSE_IOT_TOKEN "PASTE_YOUR_TOKEN_HERE"

// #define ELECTROCSE_IOT_SERVER "http://192.168.1.50"

#include <ElectroCSE.h>

char ssid[] = "Your_WiFi_SSID";
char pass[] = "Your_WiFi_Password";

#define PIN_LDR     34
#define PIN_RELAY    5
#define PIN_SERVO   18

// 50Hz, 16-bit. Both are read by angleToDuty(), so change them together.
#define SERVO_HZ    50
#define SERVO_BITS  16

/** Angle in degrees -> LEDC duty counts. See the header for the arithmetic. */
static uint32_t angleToDuty(int angle) {
  const int us = map(constrain(angle, 0, 180), 0, 180, 500, 2500);
  const uint32_t periodUs = 1000000UL / SERVO_HZ;          // 20000

  return (uint32_t) ((uint64_t) us * ((1UL << SERVO_BITS) - 1) / periodUs);
}

/* ----------------------------------------------------------- coming down -- */

ELECTROCSE_LISTEN(relay) {
  digitalWrite(PIN_RELAY, param.isOn() ? HIGH : LOW);
}

/*
 * A slider, not a switch - so read the number rather than asking isOn().
 *
 * constrain() lives inside angleToDuty() and is not defensive padding: the
 * value is whatever the dashboard sends, and a widget configured 0-1000 by
 * mistake would otherwise drive the servo past its stop and stall it against
 * the end of its travel, which is how a servo cooks itself.
 */
ELECTROCSE_LISTEN(servo) {
  const int angle = param.asInt();
  ledcWrite(PIN_SERVO, angleToDuty(angle));

  Serial.print("servo -> ");
  Serial.println(constrain(angle, 0, 180));
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

  ledcWrite(PIN_SERVO, angleToDuty(90));           // centre the servo
  ElectroCSE.send("status", "ready");              // a text reading
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);

  ledcAttach(PIN_SERVO, SERVO_HZ, SERVO_BITS);

  ElectroCSE.connect(ELECTROCSE_IOT_TOKEN, ssid, pass);

  // Three streams on two schedules. Light changes fast and is worth watching;
  // signal and uptime are context and do not need the same resolution.
  ElectroCSE.every(2000, []() {
    ElectroCSE.send("light", map(analogRead(PIN_LDR), 0, 4095, 0, 100), "%");
  });

  ElectroCSE.every(15000, []() {
    ElectroCSE.send("rssi",   WiFi.RSSI(), "dBm");
    ElectroCSE.send("uptime", (millis() / 1000), "s");
  });
}

void loop() {
  ElectroCSE.loop();
}
