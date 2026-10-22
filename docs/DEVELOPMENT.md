# Supernova - notes for developers

This page is for people who work on the firmware: how each feature works, with the commands and the memory it uses. The user documentation is in the [README](../README.md). Paths below are relative to the folder of the project (`src/`, `build-tools/`).

Supernova is a custom firmware for the TASOLLER Plus controller. Its goal is to extend the native functionality
of the official firmware: it starts from a readable C reimplementation of the official firmware
`SS321_APP_050_20250102`, so that new features can be added on top of it.

It can be configured with the official options application (`TASOLLER_Options_V2.8.exe`), but the best way to use
it is the **Tasoller+ Toolkit** (see below), a web page that lets you configure and flash your device, just like
the official executable does.

## Extra features

Each feature below is managed from [Tasoller+ Toolkit](https://github.com/YttrisL/tasoller-plus-web-tools). The tabs and menus that only work with Supernova are hidden when the Toolkit is connected to the official firmware.

### Chip dump

Supernova can read out its own microcontroller (Nuvoton M482, 512 KiB flash, 160 KiB RAM) over the USB command channel, so the whole chip can be saved from the **Memory dump** tab of the Toolkit, without any wiring. It is the same data that the UART-based dump firmware of the "dump and restore" project produces, and the zip it downloads has the same layout (one `.bin` per section, `manifest.json`, `info.txt`).

To use it, connect the device in the Toolkit, open **Memory dump**, click **Dump everything** (it takes about a minute) and then **Download all as .zip**. Nothing is written to the device and it keeps working as a controller while it is read. The unique chip ID (`UID0` to `UID2`) is replaced by `XXXXXXXX` in the download unless you untick *Hide my unique chip ID*.

What is read:

| Section | What it is |
| --- | --- |
| `SYS`, `CLK`, `GPIO`, `SYSTICK`, `NVIC_ISER`, `SCB` | The registers as the bootloader left them when it started Supernova. They are copied by the very first instruction of the firmware (`boot_snapshot_take`, called from `Reset_Handler`) into RAM at `0x20020000`, above the stack, which the firmware never clears. |
| `SRAM` | All 160 KiB of RAM, live: it is Supernova's own variables and stack, not what the bootloader left. |
| `CONFIG`, `XOM`, `OTP`, `KPROM` | User configuration words (including the lock bit), execute-only region setup, the one-time-programmable area and the key store. They are read with the flash controller's ISP read command; the chip always refuses the key store, which is normal. |
| `LDROM` | The loader ROM (4 KiB, blank on this device). |
| `APROM` | The 512 KiB application flash: the vendor bootloader, slot A (the running firmware, so Supernova itself), slot B (the download buffer), the settings page and the hardware version page. |

`info.txt` also lists the device identifiers (`CID`, `PID`, `PDID`, the unique IDs), the flash controller and execute-only status, and the boot values (`BOOT_RSTSTS`, `BOOT_VTOR`, `BOOT_CLKSEL0`, `BOOT_PLLCTL`).

Note that a dump taken from Supernova holds Supernova in slot A: to capture the official firmware, dump a device that still runs it with the UART dump tool.

How it works, for developers. The feature is command `0x40` (`CMD_DUMP`) of the AES-encrypted command channel, implemented in `src/dump.c`. The host pulls one piece at a time, so the firmware needs no buffer and keeps running between two requests. The first byte after the command header is the sub-command:

| Sub-command | Request | Reply |
| --- | --- | --- |
| `0` hello | - | `[0, protocol, section count, info count]` |
| `1` section | `[1, index]` | `[1, index, mode, address, length, name]` |
| `2` info | `[2, index]` | `[2, index, status, value, name]` (status 1 = the chip refused) |
| `3` read | `[3, mode, address, n]` | `[3, status, address, n, data, crc16]` (`n` is 4 to 48, a multiple of 4) |

Numbers are little endian. The read modes are `M` (plain memory reads; a bus fault reads as zero instead of crashing), `I` (ISP read, reaches the special areas and refuses what the chip will not give) and `S` (the boot snapshot). Each piece carries a CRC-16/CCITT-FALSE over its address and data; the Toolkit asks again for a piece that arrives damaged and also reports a CRC-32 per section. The stock firmware answers this command with an error, which is how the Toolkit tells the two firmwares apart as well.

### Settings backup

The **Config** tab of the Toolkit gets a **Settings backup (Supernova)** card with two buttons. **Export settings** saves the settings page of the device to a `.snvset` file: thresholds, key maps for every mode, USB mode, input delay, brightness, touch tuning and the device name, which is the 731 bytes the firmware keeps in flash at `0x38000`. **Import settings** reads such a file and puts it back: the device replaces all its settings, saves them and restarts, so the Toolkit asks you to reconnect afterwards. It is a way to back up a tuned controller, or to copy the settings to another one.

A `.snvset` file is `SNVSET01`, the length of the block (16 bits, little endian), the block itself and a CRC-32 of everything before it. The Toolkit refuses a file with the wrong marker, a bad CRC-32 or another length, and the firmware checks again before it changes anything.

How it works, for developers. Command `0x41` (`CMD_SETTINGS`, `src/settings_transfer.c`): sub-command `0` gives the protocol and the block size, `1` reads up to 48 bytes of the settings in use at an offset, `2` writes up to 48 bytes at an offset into a staging area in RAM (`0x20021000`, above the stack), and `3` sends the CRC-32 of the staged block. On commit the firmware checks the CRC-32 and the settings-page marker (status 0 done, 1 bad CRC-32, 2 not a settings page), then runs the same range checks as at start-up (`settings_sanitize` and `settings_apply`, split out of `settings_load`) and marks the settings to be saved. The Toolkit then sends the existing save-and-reset command (`0x28`).

### Individual tower LEDs

Each air tower has 24 LEDs, but the LED packet of the official firmware only has three colours per tower, one for each group of eight LEDs. With Supernova the **LED test** tab of the Toolkit can set each of the 24 LEDs of both towers on its own. It is automatic when a Supernova device is connected in the top bar: click LEDs to select them (shift-click selects a range on a tower), or click the bar beside a group of eight to select or deselect that group at once, which is a zone of the official LED packet, then pick their colour as usual.

This does not change anything for the official tool, the game or any software that sends the usual 114-byte LED packet: that packet is still handled exactly as before, and nothing happens differently unless a host sends the new packets described below.

How it works, for developers (`src/led_ext.c`). The host sends an *extended LED packet* to the same bulk endpoint as the LED packet. It is always exactly 64 bytes: `'D' 'X'`, the version (1), the tower (0 left, 1 right), the first LED counted from the bottom of the tower (0 to 23), the number of colours that follow (1 to 19), then each colour as red, green, blue, padded with zeros. The official packet is sent as two transfers of 64 and 50 bytes and starts with `'D' 'L'`, so it can never be mistaken for an extended packet. The USB interrupt only stores the colours; the main loop converts and dims them like the official ones. While extended packets keep arriving (the last one less than 200 ms ago) the towers ignore the three colours of the official packet, which still drives the slider; when they stop the towers follow the official packet again. A host first asks whether the firmware knows the packets with command `0x42` (sub-command 0, reply `[0, protocol, 24]`), since an older firmware would not understand them. The left tower is wired from its top and the right one from its bottom; the firmware takes care of that, so the host always counts from the bottom.

### IO4-Emulation mode

A USB mode, **IO4-Emulation**, is the sixth position of the *USB mode* slider of the **Config** tab (the slider only offers it with Supernova). It reproduces the way official hardware behaves towards the game: in this mode there is no need for a custom driver, the game recognizes the device automatically. It is the default mode (see below). The device shows up in Windows as `Supernova IO4 emulation mode`, with four parts:

- **An I/O board** (SEGA's IO4, a HID device with the USB ID `0CA3:0021` of the real board, which the game requires). It reports the six air beams (a beam's bit is set while it is free), the coins, and the TEST and SERVICE buttons, and takes the commands of the game, among them the outputs that light the light gate of the towers.
- **The slider on a virtual serial port** (a standard Windows `USB Serial Device`). The game reads the pressure of the 32 pads there and sets the 31 slider LEDs.
- **The two LED boards of the side devices** (the "SIDE DEVICE LED L" and "SIDE DEVICE LED R" of the game), each on a virtual serial port of its own, speaking the protocol of SEGA's 837-15093-06 LED controller board. The air towers are lit by what the game sends to them; the "GATE LED", which is the cabinet's light gate, is received and ignored.
- **The command channel of Supernova**, as in every mode, so the Toolkit can still configure the device.

Set the game up as it is for the arcade hardware: segatools' own emulation of the I/O board and of the slider has to be switched off (`[io4] enable=0` and `[slider] enable=0`) the slider must be on `COM1`, the left LED board on `COM2` and the right one on `COM3` (the CVT cabinet, the default of segatools; see the table below for the other games) (Device Manager, the `USB Serial Device` entries, *Port Settings*, *Advanced*, *COM Port Number*; in the properties of each port, *Details*, *Bus reported device description* says which number it needs: `Supernova Slider - COM1`, `Supernova Side LED L - COM2` and `Supernova Side LED R - COM3`), and `[led15093]` has to be switched off (`enable=0`) as well.

The numbers of the LED board ports depend on the game (the slider is `COM1` in all of them). The name the device gives the ports (`Supernova Side LED L - COM2`) is the one for the CVT cabinet:

| Game | Left LED board | Right LED board |
| --- | --- | --- |
| CHUNITHM (original, segatools `chunihook`) | `COM10` | `COM11` |
| CHUNITHM NEW, CVT cabinet (`dipsw3=1` in `segatools.ini`) | `COM2` | `COM3` |
| CHUNITHM NEW, SP cabinet (`dipsw3=0`) | `COM20` | `COM21` |

The device also asks Windows for these numbers itself (a Microsoft OS 2.0 descriptor with the `PortName` of each port: `COM1`, `COM2` and `COM3`). It is a convenience that Windows may ignore, for instance when it already knows the device or when a number is taken: then set the numbers by hand.

Like a cabinet that was switched off between two games, an LED board port that is opened after 10 seconds without data starts from scratch (its queued answers, its address and its settings are reset), so a game that starts after another one does not meet what the previous one left.

When the game reports "Initialization failure with the LED board 1" and "2", the ports are on the wrong numbers: the game never opens the ports of the device.

Buttons: FN1 is the TEST button and FN2 the SERVICE button, for as long as they are pressed. The coin counters of the board stay at zero for now: there is no way to insert a coin yet.

Lights: the slider LEDs follow what the game sends, with the brightness settings of the Toolkit. The air towers show the colours the game sends to the two LED boards (the last three LEDs of each board are the three groups of the tower on that side); the game gives them on a scale that stops at about 61, so they are multiplied by four. As with the official firmware, the device lights the slider and the towers by itself again when the game has not sent anything for three seconds.

What is different from the other modes: the live data and LED interfaces of the other modes are not there, because their endpoints are used by the I/O board and the serial port. The Toolkit's **Input test** still works, with the game closed: it reads the pads on the serial port of the slider with Web Serial and the air beams and buttons from the I/O board with WebHID (it asks for both permissions once), but it does not show the DIFF values. The **LED test** page also works, the way the game lights the LEDs: it sends the slider colours to the serial port and the colours of the two towers (three groups each, as the game has them) on the command channel; there is no per-LED control in this mode. The official tool does not find the device, because the USB ID is the one of the I/O board. Use the Toolkit to go back: *Config*, *USB mode*, WINUSB. If you cannot, hold FN1 and FN2 while plugging the device in to enter the update mode of the bootloader and flash a firmware. The game also needs to be started after the device is plugged in.

It is the default mode. When Supernova is flashed on a device that has never run it, the device starts in IO4-Emulation whatever mode it was in before, and so does a new or reset device. After that, the mode you choose is kept, also when you flash a newer Supernova over it. The Flasher of the Toolkit goes one step further: when it installs Supernova on a device that is not running it, the device is switched to IO4-Emulation once it is back, even if an earlier Supernova had left it in another mode.

Going back to the official firmware is safe: the official firmware and tool only know the modes 0 to 4. IO4-Emulation is stored as WINUSB (mode 0) in the settings page, plus a marker in one of the spare bytes of the page (byte 27 of the unused block, which the official firmware keeps as it is: `0x49` is IO4-Emulation, `0x53` means a mode was chosen with Supernova or the official tool). The official firmware therefore starts as a plain WINUSB device. The Toolkit gets the real mode by adding `0xA5` after the read of the mode command (`0x27`), and sets IO4-Emulation by writing mode 5. The official firmware ignores the extra byte and refuses 5. A settings file exported before this mode existed keeps its mode when imported.

How it works, for developers (`src/io4.c`, `src/io4_slider.c`). The two LED boards are interfaces 4 to 7, with bulk endpoints only (OUT 2 and IN 8, OUT 9 and IN 10): there is no endpoint left for their notifications, so their communication interfaces have none, which the USB specification allows. If Windows refuses these two ports, the rest of the device is not affected. The USB descriptors are in flash: interface 0 is the I/O board (IN 4, 64 bytes every 8 ms; the commands of the game, report 16, arrive as `SET_REPORT` on the control endpoint, so there is no OUT endpoint), interface 1 the command channel with the same endpoints as the other modes (IN 5, OUT 6), interfaces 2 and 3 the serial port (OUT 3, IN 1 and the unused notification endpoint IN 7). The state is in RAM above the stack (`0x20022000`). The pads come from the stream packet of the official firmware (so the pad values are the ones the official game software gets), the buttons and beams from the same inputs, and the LEDs are put in the official LED packet and go through the same path as the packets of the official software. The coin counter, the TEST/SERVICE bits and the status bits follow the dump of the real board in the documentation of the host-aprom and tasoller-plus projects.

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
