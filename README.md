# Luminoctopus

## Introduction

This repository hosts the Luminoctopus firmware as well as libraries to use the device from various
software environments (e.g. TouchDesigner). The Luminoctopus is an 8-channel LED controller that can
be controlled over USB. It makes it easy to pilot RGB or RGBW LED arrays, strips or matrices (WS2811
/ WS2812 / WS2812B / WS2813 LED arrays or strips.

At its core, it uses an [OctoWS2811](https://www.pjrc.com/store/octo28_adaptor.html)-enabled 
[Teensy 4.1](https://www.pjrc.com/store/teensy41.html) board. 

## How many LEDs can be controlled?

This project allows you to reliably control a maximum of 1365 addressable RGB LEDs per channel at a 
refresh rate of 24Hz. This yields a total of 10920 LEDs for all 8 channels. However, if you want to 
maintain a refresh rate of 30Hz, you will have to lower the number of LEDs to 1101 per channel (8808
total). These threshold apply to 800kHz LEDs. If you use 400kHz LEDs, you will get half that. 

It's always a tradeoff between frame rate, type of LED (RGB vs. RGBW), protocol speed, and number of
LEDs. If you use the TouchDesigner component, the maximum number of LEDs given the current 
parameters is shown on the OctoWS2811 parameter page.

Note that there is a hard maximum of 1365 RGB LEDs and 1023 RGBW LEDs per channel (no matter the 
frame rate). This is due to limits imposed by the size of a single DMA transfer (32kbits).

The USB communication speed usually is not an issue. The Teensy supports either USB 1.1 Full Speed 
(12Mbits/s) and USB 2.0 High Speed (480Mbits/s). To update 1365 LEDs on each of the 8 channels, you
must send about 44KB of data. If you want to do this at 30Hz, your going to need a bandwidth of 
1250KB/s or 10Mbps. This is below the 12Mbps limit of USB 1.1 but you probably are using USB 2.0 
anyway.

## How can I use it?

As of now, the only library available is for the TouchDesigner environment. You can find the 
`Luminoctopus.tox` file in `libraries/touchdesigner/`. To use it, simply drag and drop the `.tox` 
file to your project and enter the appropriate settings.

To install the firmware on the Luminoctopus device (if not already present), use the 
[Arduino IDE](https://www.arduino.cc/en/software/) to upload `Luminoctopus.ino` to the Teensy 4.1
device. Note that you will have to install the Teensy board in the Arduino IDE. To do so, follow 
these [instructions](https://www.pjrc.com/teensy/td_download.html). 

## Luminoctopus Outputs

The layout for the two RJ-45 output ports on the Luminoctopus are as follows (for details, check 
out the documentation for the [OctoWS2811 adapter](https://www.pjrc.com/store/octo28_adaptor.html)):

|Port A (top)      | Port B (bottom)      |
|------------------|----------------------|
| 0. Orange        | 4. Orange            |
| 1. Blue          | 5. Blue              |
| 2. Green         | 6. Green             |
| 3. Brown         | 7. Brown             |

Note that, within each twisted pair, the full-color wire is for data and the color+white wire is for 
ground (GND). 

## Protocol

> [!NOTE]  
> If you just want to use the device and library, you do not need to read anything beyond this point.
> However, if you want to use or understand the protocol itself, you will find some info below.

#### General Messsage Format

|START MARKER|COMMAND|LENGTH |PAYLOAD        |CHECKSUM|
|------------|-------|-------|---------------|--------|
|1 byte      |1 byte |2 bytes|variable length|1 byte  |

Available commands are:

* Configure Device (`0x10`)
* Assign Colors (`0x20`)
* Fill Color (`0x21`)

#### Configure Device

This is a system-specific command. Only the system with the specified system ID will listen to it.
The payload for this message is: 

|START MARKER|COMMAND|LENGTH       |SYSTEM ID    |COLOR ORDER|SPEED      |CHECKSUM|
|------------|-------|-------------|-------------|-----------|-----------|--------|
|`0x00`      |`0x10` |`0x00` `0x02`|`0x00` `0x01`| see below | see below | modulo |

System ID `0x00` `0x01` is for the Luminoctopus. Perhaps others will be added.

Available color orders are:

  * WS2811_RGB  =  0 (0x00)
  * WS2811_RBG  =  1 (0x01)
  * WS2811_GRB  =  2 (0x02)
  * WS2811_GBR  =  3 (0x03)
  * WS2811_BRG  =  4 (0x04)
  * WS2811_BGR  =  5 (0x05)
  * WS2811_RGBW =  6 (0x06)
  * WS2811_RBGW =  7 (0x07)
  * WS2811_GRBW =  8 (0x08)
  * WS2811_GBRW =  9 (0x09)
  * WS2811_BRGW = 10 (0x0A)
  * WS2811_BGRW = 11 (0x0B)
  * WS2811_WRGB = 12 (0x0C)
  * WS2811_WRBG = 13 (0x0D)
  * WS2811_WGRB = 14 (0x0E)
  * WS2811_WGBR = 15 (0x0F)
  * WS2811_WBRG = 16 (0x10)
  * WS2811_WBGR = 17 (0x11)
  * WS2811_RWGB = 18 (0x12)
  * WS2811_RWBG = 19 (0x13)
  * WS2811_GWRB = 20 (0x14)
  * WS2811_GWBR = 21 (0x15)
  * WS2811_BWRG = 22 (0x16)
  * WS2811_BWGR = 23 (0x17)
  * WS2811_RGWB = 24 (0x18)
  * WS2811_RBWG = 25 (0x19)
  * WS2811_GRWB = 26 (0x1A)
  * WS2811_GBWR = 27 (0x1B)
  * WS2811_BRWG = 28 (0x1C)
  * WS2811_BGWR = 29 (0x1D)

Available speeds are: 

  * WS2811_800kHz =  0 (0x00)
  * WS2811_400kHz = 64 (0x40)
  * WS2813_800kHz = 80 (0x80)


#### Assign Colors

This allows assigning the color of all LEDs on a channel. If the controller has been configured to 
use 4-component colors (RGBW), you can send 4 bytes per color. Otherwise, it defaults to 3 bytes 
(RGB).

|START MARKER|COMMAND|LENGTH    |      PAYLOAD           |CHECKSUM|
|------------|-------|----------|------------------------|--------|
|`0x00`      |`0x20` | variable |CH + RGB... or RGBW...  | modulo |

#### Fill Color

This assigns the same color to all the LEDs on a channel (or all channels if 255 is specified)

|START MARKER|COMMAND|LENGTH    |      PAYLOAD         |CHECKSUM|
|------------|-------|----------|----------------------|--------|
|`0x00`      |`0x20` | variable |CH + RGB or CH + RGBW | modulo |

This can be used to turn off the lights by sending a color of (0, 0, 0).

## Caveat

This is alpha software and might not be production ready. I'm having good success with it but your 
mileage may vary. Report issues if you find any. Cheers!
