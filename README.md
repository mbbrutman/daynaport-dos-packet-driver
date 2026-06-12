# DaynaPORT DOS Packet Driver

This is a DOS packet driver for the DaynaPORT SCSI Ethernet Adapter.
These were mostly used on Macintosh systems; Amigas and other machines
could use them as well. No DOS drivers were ever available for them.

Recently SCSI device emulators such as ZuluSCSI and BlueSCSI have
incorporated emulation for the DaynaPORT devices, making it possible
to get Ethernet (via WiFi) over SCSI if you have one of these
devices on your SCSI chain. This packet driver allows you to use
the DaynaPORT emulation in DOS.

## Building
1. Download and install Borland Turbo C++ 3.0 for DOS.
2. Set the compiler options:
  * Compact memory model (small code, far pointers)
  * No floating point (not used, reduces code size)
  * 8088 or 8086 code generation
3. Compile and you will have an EXE to run.

All code is present in the one CPP file and the H files.  Porting to other
toolchains is possible, but beware of the funny tricks that were required
to work around this particular compiler.

## Running
Simple! 
1. Install the ASPI drivers for your SCSI card.  (ASPI is required.)
2. `dayna.exe packet_vector scsi_id [-adapter <adapter_id>] [-idlehook]`
  * Valid packet vector numbers range from 0x60 to 0x80. Chose an open one.
  * SCSI ID is the device ID on your SCSI chain.
  * Adapter id is optional and defaults to zero.
  * The -idlehook flag enables mTCP to run faster. (See the discussion below.)
  * Example command: `dayna.exe 0x60 4`
3. From there you can configure mTCP or your favorite program that uses a packet driver!

## Unloading
The packet driver is not a TSR but it can be unloaded. When it loads it will
install itself and then call COMMAND.COM, giving you a secondary DOS prompt.
You can then run your networking programs. To unload it type "exit" at that
secondary DOS prompt.

This approach uses more memory than a TSR packet driver written in assembly
but it is useful while the driver is under development. Stay tuned for a
proper TSR version of this code.

## Performance notes
By default the driver only looks for new packets while the timer interrupt
is running.  This limits the number of packets processed per second to 18,
which is the number of times the timer interrupt fires per second.  For
casual usage that is fine but it significantly limits the speed of file
transfers.

Using the -idlehook flag on the command line tells the driver to also hook
the DOS Idle interrupt (int 0x28).  DOS normally calls this interrupt when
it is waiting for keyboard input, giving TSRs a chance to run more often.
mTCP programs also call this interrupt when they are polling for new
packets.  This allows mTCP programs (or other programs that call int 0x28
when they are idle) to process packets much more quickly than just relying
on the timer interrupt alone.  WATTCP programs can be modified to do this
too, but mTCP supports it "out of the box."

My Pentium 133 can transfer files using FTP at over 500KB/sec when
using the -idlehook option.  Without that you can expect 25KB/sec.

## Known Issues
1. Limited performance compared to a dedicated Ethernet card. Under DOS the
interface has to be polled, which is less performant than an interrupt driven
device. But it is still more than adequate.
2. This code uses too much memory due to the non-TSR nature of the program.
The second copy of COMMAND.COM and the overhead of the C runtime add up.
3. This code is not compatible with mTCP NetDrive because it uses too much
stack space during the interrupts. (If this is a problem email me and I'll
give you an updated mTCP NetDrive that allocates more stack space.)
4. I have on occasion had my development system freeze up while running this
code and using the -idlehook option. I suspect it is a race condition between
the timer interrupt and the DOS idle interrupt, or perhaps a stack overflow.
A new version of the code rewritten in assembly eliminates the problem. (See
"Coming soon" for details.)
5. Needs testing with more SCSI cards.

## Coming soon

Recently I have rewritten this driver entirely in x86 assembler. The new version
of the code has the following benefits:
* Overall memory usage is greatly reduced, as it doesn't have the overhead of
the C run-time library and it is a proper TSR. (It consumes less than 4KB in
RAM now.)
* The freezing problem when using the -idlehook flags has been eliminated.
* Less code runs under the timer interrupt, making it less likely that
system time will be skewed when you are receiving lots of network packets.
* It works with mTCP NetDrive because it uses far less stack space and it
also switches to private stacks when needed.

I'm actively testing and filling in the last 5% of that code now. It will be
posted when it is ready. I am updating the current C version of the code
to keep up with firmware changes in the devices while the new version
of the code is being finished.

## Credits / References
The original code for this packet driver was written by RetroTech Chris.
See his original repo at cml37/daynaport-dos-packet-driver.

[Rabbit Hole Computing](https://rabbitholecomputing.com) designs
the ZuluSCSI emulator, and they pressed one into my hands at a
recent retrocomputing event. That led me down this particular
rabbit hole - improving the existing packet driver.

The most excellent references that made this driver possible!
* [DaynaPORT Command Set](https://github.com/PiSCSI/piscsi/wiki/Dayna-Port-Command-Set)
* [Adaptec ASPI SDK](https://tinkerdifferent.com/threads/adaptec-aspi-sdk-dos-windows-3-x-16bit-scsi-development.3466)
* [PC/TCP Packet Driver Specification](https://web.archive.org/web/20221127060523/http://crynwr.com/packet_driver.html)
* [DaynaPORT BlueSCSI Code](https://github.com/BlueSCSI/BlueSCSI-v2/blob/main/lib/SCSI2SD/src/firmware/network.c)
