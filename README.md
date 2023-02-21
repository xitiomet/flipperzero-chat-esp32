# FlipperZero SubGhz Chat Bridge

This project will allow you to communicate with [FlipperZero](https://flipperzero.one/)'s SubGhz chat feature using an ESP32 and a CC1101.

The chat bridge provides a web interface allowing multiple user's with different nicknames to connect and communicate with nearby flipper devices.

In order to build this you will need:

 * ESP32 - https://www.amazon.com/gp/product/B09GK74F7N/
 * CC1101 - https://www.amazon.com/gp/product/B01DS1WUEQ/
 * Computer with Arduino 1.8.18
	 * ESP32 library support (https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json)
	 * SmartRC-CC1101-Driver-Lib (https://github.com/LSatan/SmartRC-CC1101-Driver-Lib)
	 * Ability to upload FatFS image (https://github.com/lorol/arduino-esp32fs-plugin)


 Hookup between devices:

 ESP-32 | CC1101
 -------|------------
 3.3v   | VIN
 GND    | GND
 5      | CS
 18     | SCLK
 19     | MISO / SO
 23     | MOSI / SI

Please note: The GDO0 and GDO2 pins are left unconnected.

Once you've downloaded the source and properly setup your IDE for flashing, edit the settings.json file in the data directory. This will be loaded at startup.

```json
{
    "startFrequency": 433.92,
	"apMode": true,
	"apSSID": "FlipperChat",
	"apPassword": "changeme",
    "wifi": [
        {
          "ssid": "Your network name",
          "password": "wifi pass"
        }
    ]
}

```
if "apMode" is set to true the ESP32 will create a network with the settings labeled "apSSID" and "apPassword". Otherwise please add the wifi networks that should be searched for to the json array named "wifi"

Now you are all set to flash the sketch and upload the FatFS image.