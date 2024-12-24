# A Demo That Demostrate c++20 coroutine usage in MCU env


This demo uses [PlatformIO](https://platformio.org/) as IDE

Strongly recommend  vscode + pio extension as development IDE.

Quick begin:

Fire up vscode, install [This Platformio Extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)。

Then  "Open Folder" this cloned repo.


Hit ![alt text](doc/image/vscode_pio_build.png) to build this demo.

This demo uses stm32g431 MCU.

# How To use µcoro with other MCU:

create new folder under  src/boards/ , name it with MCU type or  devboard.

eg. src/boards/esp32

then register new board in `platformio.ini`.


Remember to use
 ![alt text](doc/image/vscode_pio_switch_env.png)

this tool button to switch to your newly created board.



If you have any questions about PlatformIO, visit [esp32 PlatformIO quick guide](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/third-party-tools/platformio.html)


