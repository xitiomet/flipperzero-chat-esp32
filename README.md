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
	 * ArduinoJson 6.19.3 (https://arduinojson.org/?utm_source=meta&utm_medium=library.properties)


### Building your own bridge

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

### Using the web interface

Navigate a web browser to the ESP32's ip (output on the serial console) and you will be prompted to enter a nickname. Multiple users and computers are able to connect to the same ESP32 with distinct nicknames, they will see each others messages, as well as flipper users.

Technically you could build two bridges and they would be able to communicate with each other as well.

The catch is there is the CC1101 can only tune into one frequency, so everyone must be on the same frequency and if someone changes the frequency it effects all users on that bridge. You can see the current tuned frequency in the upper right hand corner.

At any point while in the chat screen a connected user may type "/freq 433.92" to change the frequency, the new frequency must be expressed in Mhz.