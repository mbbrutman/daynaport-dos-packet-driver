# DaynaPORT DOS Packet Driver

This is a DOS packet driver for the DaynaPORT SCSI Ethernet Adapter.
These were mostly used on Macintosh systems; Amigas and other machines
could use them as well. No DOS drivers were ever available for them.

Recently SCSI device emulators such as ZuluSCSI and BlueSCSI have
incorporated emulation for the DaynaPORT devices, making it possible
to get Ethernet (via WiFi) over SCSI for free if you have one of these
devices on your SCSI chain. And so now it becomes more interesting for
DOS users who have these devices. And now we have a packet driver!.

## Building
1. Download and install Borland Turbo C++ 3.x for DOS.
2. Set the compiler options:
  * Compact memory model (small code, far pointers)
  * No floating point (not used, reduces code size)
  * 8088 or 8086 code generation
3. Compile and you will  have an EXE to run.

All code is present in the one CPP file and the H files.  Porting to other
toolchains is possible, but beware of the funny tricks that were required
to work around this particular compiler.

## Running
Simple! 
1. Install the ASPI drivers for your SCSI card.  (ASPI is required.)
2. `dayna.exe packet_vector scsi_id [-adapter <adapter_id>] [-idlehook]`
  * Valid packet vector numbers range from 0x60 to 0x80.
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
on the timer interrupt alone.

Performance is still limited to about 200KB a second while I chase down
a problem that freezes my system. (I think the problem is in the
Adaptec ASPI layer but I haven't proven that yet.) If you are brave you
may remove the delay() statement in the code to speed things up.

## Known Issues
1. Limited network performance
2. Uses too much memory due to the non-TSR nature of the program.
3. Needs testing with more SCSI cards.

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
