# Supernova - A TASOLLER Plus custom firmware

Supernova is a custom firmware for the TASOLLER Plus controller. Its goal is to extend the native functionality
of the official firmware: it starts from a readable C reimplementation of the official firmware
`SS321_APP_050_20250102`, so that new features can be added on top of it.

It can be configured with the official options application (`TASOLLER_Options_V2.8.exe`), but the best way to use
it is the **Tasoller+ Toolkit** (see below), a web page that lets you configure and flash your device, just like
the official executable does.

## Extra features

Soon :eyes:

## Tasoller+ Toolkit

[Tasoller+ Toolkit](https://github.com/YttrisL/tasoller-plus-web-tools) is a web tool that lets you configure your Tasoller+ just like the official `TASOLLER_Options_V2.8.exe` application does, except that you do not need to run an .exe file. Everything is handled through your browser. Tasoller+ Toolkit can be used with the official Tasoller+ firmware, but with Supernova it also lets you use and configure all of the extra features listed above. Its goal is to completely replace the old .exe application and greatly simplify the maintenance and configuration of your device.

## How to build

You need Python 3.8 or newer and the Arm GNU Toolchain (`arm-none-eabi-gcc`, `-objcopy`, `-nm`; tested with
14.2). The toolchain is found through `--toolchain`, the `ARM_GNU_TOOLCHAIN_BIN` environment variable or `PATH`.

```
build.cmd
```

`build.cmd` runs the tests first, then compiles the firmware and packs it. If a test fails, nothing is
published. The result is in `release/`.

## How to install

First, get the latest release of Supernova: you want the `.skystar` file. You can flash it on your device with the official `TASOLLER_Options_V2.8.exe` application, but the best way is to use [Tasoller+ Toolkit](https://github.com/YttrisL/tasoller-plus-web-tools).

On Tasoller+ Toolkit, go to the last tab at the top of the page, "Flasher".

- Make sure your Tasoller+ is connected (top right of the page).
- Select your `.skystar` file as Firmware package.
- Tick the checkbox "I understand that this writes new firmware to my Tasoller, and I will not unplug it until it has restarted."
- Click "Flash the firmware".
- The installation takes a few seconds, then your Tasoller+ reboots.
- After a few seconds, click "Connect" at the top right again. The device now shows up as "Supernova".

**How to return to the official firmware?**

- Download the [stock firmware](https://dj-dao.com/SUPPORT/TASOLLER-plus-option-V2.8.zip).
- Extract the `.skystar` file.
- Flash it the same way you flashed the custom firmware.
- You are now back on the stock firmware.

## Use of AI

This firmware was written with the help of AI. Claude (Anthropic) took part in the reverse engineering of the
official firmware and in the base layout of this source: it analyzed the official image and gave pointers for the firmware recreation,
checked the result against the official firmware, and renamed and organized the code into readable modules.
The new features offered by the custom firmware are the author's creation, and every revision is extensively tested by humans on real hardware.
