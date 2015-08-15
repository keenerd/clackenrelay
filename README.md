
## Clackenrelay

This little program is a controller for the $30 FT245RL USB relay boards you can pick up online.  It was made to be fast, flexible and *Just Works* out of the box on Linux systems, without messing with permissions or kernel modules.

## Prerequisites

* libftdi
* gcc

## Building

    gcc -Wall -Os -lftdi relay.c -o relay
    strip relay

*(todo: add makefile)*

## Bugs

* No error handling or warnings (patches welcome)
* Doesn't support multiple boards (need to buy a 2nd)
* Untested on non-linux systems (probably won't work, patches welcome)
* Untested on 2 & 4 channel boards (probably will work on anything with a FT245RL)

## Reference

Each relay is represented by a bit and all eight relays are a single byte combined.  Individual relays can be opened, closed or toggled.  There are several different schemes by which individual relays can be addressed.

Formats:

* `-R` (raw) The control byte is a literal character.  The letter `k` would set the relays to `0x6B`.  Not too useful but very fun.
* `-X` (hex) The default format.  Bytes are represented by strings such as `0x6B`.  Capitalization does not matter, leading `0x` is optional.
* `-D` (decimal) Same as hex but in base 10.  For example, `103`.
* `-I` (integer) The byte is represented by several given bits where `0` is the LSB and `7` is the MSB.  For example `01256`.  Order does not matter.
* `-1` (one-indexed) Same as `-I` except LSB is 1 and MSB is 8, and people will tease you.

Commands:

* `-c XX` Close the relays in the given byte.
* `-o XX` Open the relays in the given byte.
* `-s XX` Set all relays.  True means closed and false means open.
* `-t XX` Toggle the relays in the given byte.
* `-r` Read off the current state of the relays to stdout.

The format of the given/displayed byte argument is determined by the most recently set format option.  Multiple commands and formats can be used in the same call.

Streaming:

Control input is normally taken from the command line arguments.  So any change to the relays requires another call and initialization.  For faster response and less overhead a single instance may be left open in "streaming" mode, where the program acts on commands from stdin.  There are two streaming modes:

* `-C` Character-based.  Immediately updates as a character is received.  Only works with `raw`, `integer` and `one-indexed` formats.  May work with `hex` on a four-channel board.  IO format cannot be change in this mode.
* `-L` Line based.  Updates on a newline.  Uses the exact same syntax as the usual CLI argments.

## Examples

Close relay 0 and 1, open all others:

    relay -s 3

Open relay 6 and 7, close relay 0 and 1, toggle relay 3 and 5, (2 and 4 are left unchanged), read out final value:

    relay -I -o 67 -c 01 -t 35 -r

Same, but using several formats:

    relay -X -o 0xC0 -D -c 3 -R -t '(' -I -r

Create a simulated blinkenlight clackenrelay keyboard (type furiously, `^C` to exit):

    relay -t 0 -CR




