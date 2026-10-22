# Supernova - a custom firmware for the TASOLLER Plus

Supernova is a custom firmware for the TASOLLER Plus. It does everything the official firmware does, and adds the features below.
You install it and configure it with the [Tasoller+ Toolkit](https://github.com/YttrisL/tasoller-plus-web-tools), a web page: no
program to install. To go back to the official firmware, just flash it back using the same tool (see below).

## Features

- **IO4-Emulation mode.** The device shows up to the game like the arcade hardware: the I/O board, the slider and the two LED
  boards. The game finds it by itself and no driver has to be installed. This is the default mode.
- **Settings backup.** **Export settings** saves the settings of the device (thresholds, key maps, USB mode, brightness...) to a file,
  **Import settings** puts them back, on the same device or on another one. It is in the **Config** tab.
- **Memory dump.** Saves the whole content of the microcontroller to a zip file from the **Memory dump** tab, without any wiring.
  Nothing is written to the device. The unique chip ID is hidden in the file unless you ask for it.

The Toolkit only shows the tabs and buttons of these features when the device runs Supernova.

## Tasoller+ Toolkit

The [Tasoller+ Toolkit](https://github.com/YttrisL/tasoller-plus-web-tools) does what the official `TASOLLER_Options_V2.8.exe` does
(live view of the sensors, LED test, settings, firmware update) in your browser, and it also handles the Supernova features.
It works with the official firmware too. It needs Chrome or Edge on a computer.

## How to install

1. Download the latest release of Supernova: the file ends with `.skystar`.
2. Open the [Tasoller+ Toolkit](https://github.com/YttrisL/tasoller-plus-web-tools), connect your device and go to the **Flasher** tab.
3. Select the `.skystar` file, tick the confirmation box and click **Flash the firmware**.
4. Do not unplug the device. It restarts by itself after a few seconds.
5. Click **Connect** again. The device now shows up as Supernova, in IO4-Emulation mode.

The official `TASOLLER_Options_V2.8.exe` can flash the same file, but its exclusive settings are only available in Tasoller+ Toolkit.

**Going back to the official firmware:** download the [official firmware](https://dj-dao.com/SUPPORT/TASOLLER-plus-option-V2.8.zip),
extract the `.skystar` file and flash it the same way.

## Using IO4-Emulation mode

In the game's `segatools.ini`, turn off the emulation of the hardware the device now provides: `[io4] enable=0`, `[slider] enable=0`
and `[led15093] enable=0`.

The device has three serial ports: the slider and the two LED boards. The game expects them on given COM numbers. In Windows, the
Device Manager lists them as `USB Serial Device`; to change a number, open the port, then *Port Settings*, *Advanced*,
*COM Port Number*. The slider is always `COM1`. The LED boards depend on the game:


| Game                                                                   | Left LED board | Right LED board |
| ---------------------------------------------------------------------- | -------------- | --------------- |
| CHUNITHM NEW, CVT cabinet (`dipsw3=1` in `segatools.ini`, the default) | `COM2`         | `COM3`          |
| CHUNITHM NEW, SP cabinet (`dipsw3=0`)                                  | `COM20`        | `COM21`         |
| CHUNITHM (original)                                                    | `COM10`        | `COM11`         |

The device asks Windows for `COM1`, `COM2` and `COM3` by itself, so with the default setup there is often nothing to change. If the
game says "Initialization failure with the LED board 1" and "2", the ports are on the wrong numbers.

## How to build

You need Python 3.8 or newer and the Arm GNU Toolchain (`arm-none-eabi-gcc`, `-objcopy`, `-nm`; tested with 14.2). The toolchain
is found through `--toolchain`, the `ARM_GNU_TOOLCHAIN_BIN` environment variable or `PATH`.

Run `build.cmd`. It runs the tests, builds the firmware and packs it. If a test fails, nothing is published. The result is in
`release/`.

How the firmware and its features work, for developers: [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md).

## Use of AI

This firmware was written with the help of AI. Claude (Anthropic) took part in the reverse engineering of the
official firmware and in the base layout of this source: it analyzed the official image and gave pointers for the firmware recreation,
checked the result against the official firmware, and renamed and organized the code into readable modules.
The new features offered by the custom firmware are the author's creation, and every revision is extensively tested by humans on real hardware.
