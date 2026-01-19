# BITCHAT ESP
<!-- ALL-CONTRIBUTORS-BADGE:START - Do not remove or modify this section -->
[![Contributors](https://img.shields.io/github/contributors/TenebrisOS/bitchat-esp)](https://github.com/TenebrisOS/bitchat-esp/graphs/contributors) [![Stars](https://img.shields.io/github/stars/TenebrisOS/bitchat-esp)](https://github.com/TenebrisOS/bitchat-esp/stargazers) [![Downloads](https://img.shields.io/github/downloads/TenebrisOS/bitchat-esp/total)](https://github.com/TenebrisOS/bitchat-esp/releases) [![Release](https://img.shields.io/github/v/release/TenebrisOS/bitchat-esp)](https://github.com/TenebrisOS/bitchat-esp/releases/latest) [![License](https://img.shields.io/github/license/TenebrisOS/bitchat-esp)](https://github.com/TenebrisOS/bitchat-esp/blob/main/LICENSE)


<!-- ALL-CONTRIBUTORS-BADGE:END -->

> [!WARNING]
> This is experimental software
## Fast Travel
<!--toc:start-->
- [What is this about](#what-is-this-about)
- [Features](#features)
- [Supported Devices](#supported-esp32-models)
- [Repository](#bitchat-esp-repository)
- [Installing](#installing)
- [Building](#building-from-source)
- [Credits](#credits)
- [Contributors](#contributors-)
- [License](#license)
<!--toc:end-->
 
## What is this about
We want to bring Bitchat to the ESP32. It would work as a peer or relay for the bitchat mesh. Making it a cheap way to make a bitchat network

## Features

| Features | Progress |
|------|-------------|
| Basic mesh peer | Priority work|
| Texting using monitor | Not started yet |
| Logging to storage | Not started yet |


> [!NOTE]
> You can request features in [issues](https://github.com/TenebrisOS/bitchat-esp/issues).

## Supported ESP32 Models

| ESP32 Devices              | Status  |
|----------------------------|------------------|
| ESP32-WROOM-32             | Working on it!   |
| ESP32-WROOM-32U            | ❌ Not Released   |
| ESP32-WROVER-32            | ❌ Not Released   |
| ESP32-WROVER-32U           | ❌ Not Released   |
| ESP32-PICO-D4              | ❌ Not Released   |
| ESP32-S2                   | ❌ Not Released   |
| ESP32-S3                   | ❌ Not Released   |
| ESP32-C3                   | ❌ Not Released   |

## Installing
Check [Releases](https://github.com/TenebrisOS/bitchat-esp/releases) page and download the assets you need : 
`bitchat-esp.bin` `bootloader.bin` `partition-table.bin` `flasher_args.json`

First of, check the authenticity of the files you acquired
``` bash
sha256sum -c sha256sums.txt
```
Then use `esptool.py` to flash using this command (make sure your ESP is plugged) :
``` bash
esptool.py --chip esp32 write_flash -z \
  0x1000  bitchat-esp.bin \
  0x8000  partition-table.bin \
  0x10000 firmware.bin
```
Or using `flasher_args.json`
``` bash
python $IDF_PATH/components/esptool_py/esptool/esptool.py \
  --chip esp32 write_flash -z @flasher_args.json
```

## Building from Source
> [!NOTE]
> Make sure you have ESP-IDF installed or checkout [Setting Up ESP-IDF Development Environment](https://espressif.github.io/esp32-c3-book-en/chapter_4/4.2/index.html) 
 
To build this project, you will need to clone the source first :
```bash
git clone --depth=1 https://github.com/TenebrisOS/bitchat-esp.git
```
Then enter the directory
```bash
cd bitchat-esp
```

Build the project using ESP-IDF
``` bash
idf.py build
```
Make sure your ESP32 is correctly visible
``` bash
lsusb
```

Flash your ESP32 
``` bash
idf.py flash
```

Monitor it 
``` bash
idf.py monitor
```

You can use ```idf.py build flash monitor``` instead.
> [!NOTE]
> Help is welcome. C/C++ ESP-IDF Programmers or just testers
 
## Credits
- Original bitchat mesh repo : [permissionlesstech/bitchat](https://github.com/permissionlesstech/bitchat)
- Android version : [permissionlesstech/bitchat-android](https://github.com/permissionlesstech/bitchat-android)
- Created by [TenebrisOS](https://github.com/TenebrisOS)
- Support : 
  - [Rudi](https://github.com/Mayo-png)

## License 
This project is under the GNU GPLv3 License, check the [License](LICENSE) file.
