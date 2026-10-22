# Known issues

## IO4-Emulation: the game sometimes reports an error on the LED boards at start

**Symptom.** With the device in the IO4-Emulation mode, CHUNITHM NEW (CVT, LED boards on `COM2` and `COM3`) stops at start with
"Initialization failure with the LED board 1" and "2". It happens with no game window left open. Starting the game again gives
the same error. Unplugging and plugging the device back in makes the next start work, and the game then runs without any problem.

**What is known.**

- The emulation itself is right: with the game closed, a request for the board information on each LED port gets the answer
  segatools gives (board `15093-06`, chip `6710 `, firmware `0x90`, receive buffer 204).
- A game with the ports on the wrong numbers shows the same error (the game never opens the ports of the device, see
  the table of COM numbers in the README), so check that first.
- The boards keep their state when the game closes a port. A first attempt to clear it (queued bytes, address, "no answers"
  setting and boot loader mode are reset when a port is opened after 10 seconds without data, see `io4_led_board.c`) did not change
  the behavior: the error still comes on the first start after a plug in.

**Not yet known.** What differs between a start that fails and one that works. Ideas that were not checked:

- the game starts before the device is ready after a plug in (the touch startup of the controller takes a few seconds);
- the game opens the ports while Windows is still loading the serial driver for them;
- a frame or a reply that is lost at the very first exchange (the first packet in an endpoint buffer, a lost zero length packet).

**How to find it.** After a failed start, do not unplug the device. Open the Toolkit, *Memory dump*, and download only the
*SRAM* section. The state of the boards is in it, with the last 256 bytes the game sent (`rx`) and the last 256 bytes of the answers
(`tx`) of each board, which show where the exchange stops. The state of the IO4 personality starts at `0x20022000`, the board on
the left is at `+0x604` and the one on the right at `+0x904` (768 bytes each). In a board: address at +7, answers enabled at +8, boot loader
mode at +9, parser state at +11, `tx_head` and `tx_tail` at +18 and +20, `rx_head` and `rx_tail` at +22 and +24, `rx` at +46 and `tx` at +302.
If `rx` holds nothing of the game, the game never wrote to the port: look at the COM numbers and at the serial driver of Windows.
