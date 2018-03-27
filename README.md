# Beninca Core WiFi addon

## Overview

This device adds WiFi connectivity and remote control to a
[Beninca](http://beninca.com/)
[CORE](https://www.beninca.com/en/accessories/control-panels/swinging-gates.html)
control panel installed in a number of sliding and swinging gate controllers.

## Hardware

The brain is an ESP32 in an [ESP32 MiniKIT](http://forum.mhetlive.com/topic/8/new-mh-et-live-minikit-for-esp32).

Power comes from the CORE board itself, supply is rated at 24VAC, up to 200mA.
This gets rectified and stepped down twice:

 * Amtec AMSRW-78Z, quite pricy but tiny and to specs, steps down to 12V which are used for the relays
 * A cheap ebay-sourced DSN-MINI-360 board steps it down further to 3.3V which feed the ESP32.

This power scheme is flawed -- the on-board LDO responsible for powering the
devkit through USB gets fried if you feed it 3.3V. Don't replicate that.
Feed the devkit with 5V instead, or use a bare ESP32 module.

The module has outputs for `STOP` and `P.P` on the controller via relays and
senses the `SCA` from CORE to know the position of the gate.

## Interface

JSON messages are exchanged over MQTT with a homeassistant component which implements a `cover` device.

TODO: add homeassistant code


## License

Copyright 2017 Kiril Zyapkov

This code is released under GPLv2, see `LICENSE` for details