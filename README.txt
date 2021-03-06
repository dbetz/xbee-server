This project contains a loader that uses an Xbee S6B Wi-Fi module to load a Parallax Propeller
using a simple HTTP server. It needs some refinement primarily to use a higher baud rate between
the Xbee and the Propeller. It is currently using the default 9600 baud rate which is very slow.

It accepts to main HTTP requests:

XPOST /eeprom-wr/<addr>

and

XPOST /load/<addr>/<byte-count>

The first writes the bytes in the content part of the message to eeprom at the specified address.

The second loads and starts code from the specified eeprom address. It does this by copying the
specified number of bytes from <addr> to hub address zero and then starts the code in hub memory.

I've tested this using a Propeller ActivityBoard with the following connections:

Xbee DO -> P13
Xbee DI -> P12
Xbee RTS -> P11

It seems to work with the fibo and expr demos from propgcc. The fibo demo isn’t very large but the
expr demo is and it seems to work.

The loader requires a 64K eeprom and loads in two stages first writing to the upper 32K and then
copying that to hub memory and starting it. I have a simple program called xbee-load that can be
built for the Mac, Linux, or Windows using MinGW that takes a binary file and packages it up into
small HTTP requests to send to the HTTP server running on the Propeller. The idea would be to put
the HTTP server in EEPROM on the Propeller so that it starts on reset. Then the xbee-load program
can be used to load and start a Propeller binary. The HTTP requests that are used by xbee-load
could also be generated by a browser as long as it is able to put binary in the content part of
the request.

I’m currently sending only 512 bytes at a time but it may be possible to send more especially
if a future firmware release for the Xbee provides a larger receive buffer.

The included Makefile builds both .elf files and .binary files for xbee-config and xbee-server.
The .elf files can be loaded using propeller-load and the .binary files can be loaded with
any program that can handle a Spin binary file. I've been using the default branch from Google Code
but it may build okay under the release_1_0 branch that is distributed with SimpleIDE as well. There
is currently no SimpleIDE project for building this.

1) xbee-config will setup your Xbee S6B with the correct settings to run the HTTP server. This
is a Propeller program and the Makefile creates both a .elf and .binary version of it.

2) xbee-server is a Propeller program that you would normally write to the first 32K of EEPROM.
It implements a simple HTTP server that knows how to write to EEPROM and start a program from
EEPROM. This is a Propeller program and the Makefile creates both a .elf and .binary version of it.

3) xbee-load is a program that runs on the PC (Macintosh, Linux, Windows). It reads a Spin binary
and sends it to the Propeller over a Wi-Fi connection. It uses the XPOST /eeprom-wr/ and /load/
requests to load and start a program.

4) eeprom-dump is a program that isn't really needed except for debugging. It dumps data from
the upper 32k of the EEPROM. It is a Propeller program but I'm in the process of adding this
feature to the HTTP protocol as well.

In addition to all of this there is the beginnings of an HTTP server library that could be used
to create other HTTP servers. The main interface to this is in xbee-server.c and it uses a PASM
frame driver to talk to the S6B module which is in the xbeeframe.c and xbeeframe_driver.spin files.

