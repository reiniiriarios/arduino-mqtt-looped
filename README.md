# Arduino MQTT Looped Library

Based on the [Adafruit MQTT library](https://github.com/adafruit/Adafruit_MQTT_Library), this
library enables async-like behavior for MQTT and WiFi on single-threaded microcontrollers.

Most Arduino network libraries are filled with `delay()` and `while(true)` loops in order to
ensure delivery and receipt of packet information. This library instead reads packets and
completes tasks over multiple loops.

:warning: Because packet processing is happening over multiple loops, this library will likely
not function well (or at all) with code where additional data is being sent to and from your
network outside of this library.

## Compatibility

This library is a work-in-progress and is currently only tested with the
[WiFiNiNA](https://www.arduino.cc/reference/en/libraries/wifinina/) library. Other WiFi libraries
will likely work as well, possibly with minor changes. Because the WiFi connection loop cannot
be abstracted to a single `connect()` method, wider compatibility is not easy. If you'd like to
adapt this library for a different WiFi driver, look to the `wifiSetup()`, `wifiConnect()`, and
`mqttConnect()` and related methods. If you'd like to adapt the library for ethernet, these same
methods could likely be simplified or removed from the loop.

## Usage

See [examples](./examples/basic/basic.ino) for example usage.
