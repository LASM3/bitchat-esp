
<!-- ALL-CONTRIBUTORS-BADGE:START - Do not remove or modify this section -->
[![All Contributors](https://img.shields.io/badge/all_contributors-0-orange.svg?style=flat-square)](#contributors-)
<!-- ALL-CONTRIBUTORS-BADGE:END -->

> [!WARNING]
> This is experimental software
## Fast Travel
<!--toc:start-->
- [What is this about](#what-is-this-about)
- [Features](#features)
- [Supported Devices](#supported-esp32-models)
- [Repository](#bitchat-esp-repository)
- [Credits](credits)
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
- Created by [TenebrisOS]("https://github.com/TenebrisOS")
- Support : 
  - [Rudi](https://github.com/Mayo-png)

## License 
This project is under the GNU GPLv3 License, check the [License](LICENSE) file.
