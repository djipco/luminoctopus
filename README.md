# Luminoctopus

## Introduction

This repository hosts the **Luminoctopus** firmware as well as a component to control the device 
from [TouchDesigner](https://derivative.ca/). The Luminoctopus is an 8-channel LED controller that 
uses a fast serial-over-USB connection. It makes it easy to pilot RGB or RGBW LED arrays, strips or 
matrices (WS2811 / WS2812 / WS2812B / WS2813).

At its core, it uses an [OctoWS2811](https://www.pjrc.com/store/octo28_adaptor.html)-enabled 
[Teensy 4.1](https://www.pjrc.com/store/teensy41.html) board. 

> [!CAUTION]  
> _This is alpha software and might not be production ready. I'm having good success with it but your
> mileage may vary. Report issues if you find any. Cheers!_

## How many LEDs can be controlled?

This project allows you to reliably control a maximum of 1365 addressable RGB LEDs per channel at a 
refresh rate of 24Hz. This yields a total of 10 920 LEDs for all 8 channels. **At a refresh rate of 
30Hz, it supports 1101 LEDs per channel (8 808 total)**. These threshold apply to standard 800kHz 
RGB LEDs. If you use 400kHz LEDs, you will get half those numbers. 

The maximum number of supported LEDs depend on desired refresh rate, type of LED (RGB vs. RGBW), and
protocol speed (400kHz vs. 800kHz). There is a **hard maximum of 1365 RGB LEDs and 1023 RGBW LEDs 
per channel** (no matter the refresh rate). This is due to limits imposed by the size of a single 
DMA transfer (32kbits) on the microcontroller.

> [!NOTE]  
> _To help you figure out the usable maximum, the TouchDesigner component computes and displays the 
> maximum number of LEDs per channel given the currently selected settings._

To be on the safe side, you should use USB 2.0 or more recent. Luminoctopus connects at either **USB 
1.1 Full Speed** (12Mbits/s) or **USB 2.0 High Speed** (480Mbits/s). To update 1365 LEDs on each of 
the 8 channels, it must send about 44KB of data. To do this at 30Hz, it needs a bandwidth of 
1250KB/s (~10Mbps). This is below the 12Mbps limit of USB 1.1, but a little close.

## How can I use it?

#### Firmware

If it has not already been installed, you will need to install the firmware on the Luminoctopus 
device. To do so, use the [Arduino IDE](https://www.arduino.cc/en/software/) to upload 
`Luminoctopus.ino` to the device. 

> [!NOTE]
> To update the formware on the Luminoctopus, you need to install the Teensy board extension (if
> not present) in the Arduino IDE. To do so, follow these
> [instructions](https://www.pjrc.com/teensy/td_download.html). 

#### TouchDesigner Component

As of now, the only library available is for the TouchDesigner environment. You can find the 
`Luminoctopus.tox` file in `libraries/touchdesigner/`. To use it, simply drag and drop the `.tox` 
file to your project and enter the appropriate settings.

Then, you can connect a TOP's output to each of the Luminoctopus' input channels and the colors from
the TOP will be applied to the LEDs connected to the device. If you want a 1-to-1 correspondance, use
a TOP with a resolution of 1px high and Npx wide, where N is the number of LEDs in your LED strip.

## Luminoctopus Outputs

The layout for the two RJ-45 output ports on the Luminoctopus are as follows (for details, check 
out the documentation for the [OctoWS2811 adapter](https://www.pjrc.com/store/octo28_adaptor.html)):

|Port A (top)      | Port B (bottom)      |
|------------------|----------------------|
| 0. Orange        | 4. Orange            |
| 1. Blue          | 5. Blue              |
| 2. Green         | 6. Green             |
| 3. Brown         | 7. Brown             |

Note that, within each of the Ethernet cable's twisted pairs, usually, the full-color wire is for 
data and the color+white wire is for ground (GND). 

## Protocol

This is advanced information about the binary format used to send commands to Luminoctopus using its 
native protocol.

> [!IMPORTANT]  
> _If you just want to use the device and library, you do not need to read anything beyond this 
> point. However, if you want to use or understand the protocol itself, you will find some info 
> below._

#### General Messsage Format

This is the general message format. **Length** identifies the length of the payload. For commands 
that do not have a payload (such as **Update**), the length is omitted. The checksum is always 
present. It is the modulo of `command` + `payload length` + `payload`.

|START MARKER          |COMMAND                         |PAYLOAD LENGTH                               |PAYLOAD                                              |CHECKSUM                        |
|----------------------|--------------------------------|---------------------------------------------|-----------------------------------------------------|--------------------------------|
|**1 byte**<br>(`0x00`)|**1 byte**<br>(`0x01` to `0xFF`)|**2 bytes**<br>(omitted for certain commands)|**variable length**<br>(omitted for certain commands)|**1 byte**<br>(`0x01` to `0xFF`)|

#### Currently Available Commands

* **Configure** (`0x01`)
* **Assign Colors** (`0x10`)
* **Fill Color** (`0x11`)
* **Update** (`0x20`)

#### Configure Device Command

This is usually the first command sent since it allows to specify the type of LEDs you wish to 
control, the protocol speed and the number of LEDs per channel. If this command is not sent, the 
device is configured by default to use 300 LEDs per channel, using GRB order, and a speed of 800kHz.

|START MARKER|COMMAND|PAYLOAD LENGTH|COLOR ORDER|SPEED   |LEDS PER CHANNEL|CHECKSUM|
|------------|-------|--------------|-----------|--------|----------------|--------|
|`0x00`      |`0x01` |`0x00` `0x04` |  1 byte   | 1 byte |    2 bytes     | 1 byte |

Available color orders are:

  * WS2811_RGB  =  0 (`0x00`)
  * WS2811_RBG  =  1 (`0x01`)
  * WS2811_GRB  =  2 (`0x02`)
  * WS2811_GBR  =  3 (`0x03`)
  * WS2811_BRG  =  4 (`0x04`)
  * WS2811_BGR  =  5 (`0x05`)
  * WS2811_RGBW =  6 (`0x06`)
  * WS2811_RBGW =  7 (`0x07`)
  * WS2811_GRBW =  8 (`0x08`)
  * WS2811_GBRW =  9 (`0x09`)
  * WS2811_BRGW = 10 (`0x0A`)
  * WS2811_BGRW = 11 (`0x0B`)
  * WS2811_WRGB = 12 (`0x0C`)
  * WS2811_WRBG = 13 (`0x0D`)
  * WS2811_WGRB = 14 (`0x0E`)
  * WS2811_WGBR = 15 (`0x0F`)
  * WS2811_WBRG = 16 (`0x10`)
  * WS2811_WBGR = 17 (`0x11`)
  * WS2811_RWGB = 18 (`0x12`)
  * WS2811_RWBG = 19 (`0x13`)
  * WS2811_GWRB = 20 (`0x14`)
  * WS2811_GWBR = 21 (`0x15`)
  * WS2811_BWRG = 22 (`0x16`)
  * WS2811_BWGR = 23 (`0x17`)
  * WS2811_RGWB = 24 (`0x18`)
  * WS2811_RBWG = 25 (`0x19`)
  * WS2811_GRWB = 26 (`0x1A`)
  * WS2811_GBWR = 27 (`0x1B`)
  * WS2811_BRWG = 28 (`0x1C`)
  * WS2811_BGWR = 29 (`0x1D`)

Available speeds are: 

  * WS2811_800kHz =  0 (`0x00`)
  * WS2811_400kHz = 64 (`0x40`)
  * WS2813_800kHz = 80 (`0x80`)

The number of LEDs per channel must be 1365 or less for RGB and 1023 or less for RGBW. Two bytes are
used to express this number.

#### Assign Colors Command

This allows to individually assigning the color of all LEDs on a channel. If the controller has been 
configured to use 4-component colors (RGBW), you can send 4 bytes per color. Otherwise, it defaults 
to 3 bytes (RGB).

|START MARKER|COMMAND|PAYLOAD LENGTH|PAYLOAD                                             |CHECKSUM|
|------------|-------|--------------|----------------------------------------------------|--------|
|`0x00`      |`0x10` | 2 bytes      |Channel number + 3 (or 4) bytes for each LED's color| 1 byte |

#### Fill Color Command

This assigns the same color to all the LEDs on a channel (or all channels, if channel 255 is 
specified). It can be used to turn off the lights by sending a color of (0, 0, 0).

|START MARKER|COMMAND|PAYLOAD LENGTH|PAYLOAD                               |CHECKSUM|
|------------|-------|--------------|---------------------------------------|--------|
|`0x00`      |`0x11` | 2 bytes      |Channel number + R + G + B (or R+G+B+W)| 1 byte |

#### Update Command

After assigning colors with **Assign** or **Fill**, you must send the **Update** message to trigger 
the actual update of the LEDs. This allows you to prepare more than one channel beforehand and 
synchronize their update to happen at the same time.

|START MARKER|COMMAND|CHECKSUM|
|------------|-------|--------|
|`0x00`      |`0x20` | 1 byte |

This command always updates all channels.
