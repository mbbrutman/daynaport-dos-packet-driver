/*

daynaport.c - MS-DOS Packet Driver for DaynaPORT SCSI/Link

Running this:

 * Load ASPI manager for your card (e.g. DEVICE=ASPI7DOS.SYS in CONFIG.SYS).
 * Run the driver: DAYNAPORT.EXE
 * The driver installs and stays by starting a new command processor.
 * Type "exit" at the DOS command prompt to unload the driver.


Build notes:

 * This program uses the compact memory model: not a lot of code, far data pointers.


Todo:

 * Test with other SCSI cards.  (This has been primarily tested using an Adaptec 2940.)
 * Turn this into a TSR with unloading like other packet drivers.


Resources:

 * DaynaPORT Command Set: https://github.com/PiSCSI/piscsi/wiki/Dayna-Port-Command-Set
 * Adaptec ASPI SDK: https://tinkerdifferent.com/threads/adaptec-aspi-sdk-dos-windows-3-x-16bit-scsi-development.3466/
 * PC/TCP Packet Driver Specification: https://web.archive.org/web/20221127060523/http://crynwr.com/packet_driver.html
 * DaynaPORT BlueSCSI Code: https://github.com/BlueSCSI/BlueSCSI-v2/blob/main/lib/SCSI2SD/src/firmware/network.c


Original work by RetroTech Chris:
  github.com/cml37/daynaport-dos-packet-driver

Mostly rewritten by Michael Brutman (mbbrutman@gmail.com):
  github.com/mbbrutman/daynaport-dos-packet-driver

*/


#include <bios.h>
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <malloc.h>

#include "intregs.h"
#include "packet.h"
#include "scsi.h"
#include "types.h"



// === Constants ===

const int Interrupt_Timer =   0x08;  // Hardware timer, not the BIOS tick.
const int Interrupt_DosIdle = 0x28;  // DOS Idle interrupt.

// Ethernet Header (14), payload (1500), CRC (4), and optional VLAN tag (4).
const int MaxPacketSize = 1522;

// The maximum number of packet handles to support.
const MaxPacketHandles = 10;



// Packet driver stuff

// Structure for the packet driver header, placed at the start of
// the driver as required by the specification.

typedef struct PacketDriverHeader {
  unsigned char jmp_instruction[3]; // JMP to entry point (3 bytes)
  char signature[9];                // "PKT DRVR" signature
  unsigned char jmp;                //
  uint16_t jmp_offset;
  uint16_t jmp_segment;
};

// The actual data of the packet driver header.  Note, the offset and
// segment get patched at run-time.

static struct PacketDriverHeader packet_driver_header = {
  { 0xEB, 0x0A, 0x90 }, // JMP short to next jump, plus a NOP.
  { "PKT DRVR" },       // The required signature.
  { 0xEA },             // JMP to the actual packet driver handler.
  { 0x0000 },           // Offset: To be set at run-time
  { 0x0000 }            // Segment: To be set at run-time
};

/* The name of the driver */
static const char *driver_name = "RTC DaynaPort";



// === Global variables ===

// Ensure we have a reasonably sized stack to operate with.  (I don't know
// what we actually need, but we need enough to cover the ASPI layer too.)
extern unsigned _stklen = 4096U;


// User supplied parameters

static uint8_t PacketVector = 0;  // Software interrupt to use for the driver
static uint8_t ScsiId = 0;        // SCSI ID of the DaynaPort device
static uint8_t AdapterId = 0;     // Optional: Host adapter number
static uint8_t IdleHook = 0;      // Optional: Are we also using the Idle interrupt?


// Internal state

// We only ever execute one command at a time, so just allocate it
// statically to keep it off of the stack.
static ASPI_SRB Srb;

// Reentrancy flags
static volatile uint8_t PacketSoftwareIntActive = 0;
static volatile uint8_t PacketPollingIntActive = 0;

/* ASPI entry point */
static void far (*Aspi_entry_point)(uint16_t SRBOffset, uint16_t SRBSegment) = 0;

// The original ISR routines that we are going to chain to.
static void interrupt (far *ExistingTimerInterrupt)(...);
static void interrupt (far *ExistingIdleInterrupt)(...);



// Forward declare of the software int routine (needed for patching)
void interrupt packetDriverSoftwareInt(...);



void clearSrb( void ) { memset(&Srb, 0, sizeof(Srb)); }

/* Send SCSI command via ASPI */
int scsi_command(uint8_t command) {

  // Set fields that all commands use.  The SRB and CDB should have been
  // cleared and have had specialized fields set before calling here.

  Srb.SRB_Cmd = SRB_Cmd_ExecuteIOCmd;
  Srb.SRB_HaId = AdapterId;
  Srb.SRB_Target = ScsiId;
  Srb.SRB_SenseLen = ASPI_SenseLen; /* Request up to SENSE_LEN bytes of sense data */
  Srb.SRB_CDBLen = ASPI_CDBLen;

  Srb.CDBByte[0] = command;

  switch ( command ) {

    case SCSI_CDB_Cmd_Inquiry:
    case SCSI_CDB_Cmd_GetStatus:
      Srb.CDBByte[4] = Srb.SRB_BufLenLo; /* Allocation length */
      break;
    case SCSI_CDB_Cmd_EnableInterface:
      _fmemcpy(Srb.CDBByte, MK_FP(Srb.SRB_BufSegment, Srb.SRB_BufOffset), 6); /* Copy enable/disable command */
      break;
    case SCSI_CDB_Cmd_Read:
      Srb.CDBByte[3] = (Srb.SRB_BufLenLo >> 8) & 0xFF;
      Srb.CDBByte[4] = Srb.SRB_BufLenLo & 0xFF;
      Srb.CDBByte[5] = 0xc0;
      break;
    case SCSI_CDB_Cmd_Write:
      Srb.CDBByte[3] = (Srb.SRB_BufLenLo >> 8) & 0xFF;
      Srb.CDBByte[4] = Srb.SRB_BufLenLo & 0xFF;
      Srb.CDBByte[5] = 0x00;
      break;
  }

  uint16_t seg = FP_SEG(&Srb);
  uint16_t off = FP_OFF(&Srb);

  (*Aspi_entry_point)(off, seg);


  // Voodoo to be debuged and removed ...  my P133 freezes up on occasion
  // and I isolated it to the ASPI layer.  Adding a small delay seems to
  // help but this is not great since we are often doing this under an
  // interrupt, thus slowing down the machine.

  delay( 4 );


  while ( Srb.SRB_Status == SRB_Status_Pending ) { };

  if ( command == SCSI_CDB_Cmd_Read ) {
    // Always return 0, even if it was an error.  That's because when
    // no packets are available it will return SRB_Status_Error probably
    // because it timed out.
    return 0;
  }

  if ( Srb.SRB_Status != SRB_Status_Complete ) {
    // For other commands, indicate an error if we actually got one.
    return -1;
  }

  return 0;
}


static uint8_t CDB_Enable_Bytes[] = {
  SCSI_CDB_Cmd_EnableInterface, 0, 0, 0, 0, 0x80
};

/* Enable or Disable the DaynaPORT Interface */
int enable_interface(int enable, int pause) {

  clearSrb( );

  // 0x80 means enable, 0x00 means disable
  CDB_Enable_Bytes[5] = enable << 7;

  /* Enable DaynaPORT interface */
  Srb.SRB_BufSegment = FP_SEG(CDB_Enable_Bytes);
  Srb.SRB_BufOffset = FP_OFF(CDB_Enable_Bytes);
  Srb.SRB_BufLenLo = sizeof(CDB_Enable_Bytes);

  if (scsi_command(SCSI_CDB_Cmd_EnableInterface)) {
    return -1;
  }

  // The documentation says to sleep for at least half a second after
  // doing this.  Let the caller control that.  (Also, don't do it while
  // you are under the timer interrupt as this is going to block.

  if (pause) sleep(1);

  return 0;
}


// Get the DaynaPORT MAC Address
//
// This command can be used for getting the MAC address or
// the MAC address and optional statistics.  The user doesn't
// know that, so they are going to pass in a 6 byte buffer and
// expect 66 bytes back.  But maybe we want to use the statistics
// someday.

int get_mac_address(uint8_t *result, uint16_t length) {

  clearSrb( );

  // MAC address (6 bytes) and three 4 byte counters
  unsigned char buffer[18];

  Srb.SRB_Flags = SRB_Flags_DirectionIn;
  Srb.SRB_BufLenLo = sizeof(buffer);
  Srb.SRB_BufSegment = FP_SEG(buffer);
  Srb.SRB_BufOffset = FP_OFF(buffer);
  if (scsi_command(SCSI_CDB_Cmd_GetStatus)) {
    return 0;
  }
  memcpy(result, buffer, length);
  return length;
}



/* Initialize the driver */
int initDaynaPort( void ) {

  union REGS inregs, outregs;
  struct SREGS segregs;


  // Open a file ("SCSIMGR$") via a handle (DOS Int 21, function 3D).
  const char scsi_manager[] = "SCSIMGR$";
  inregs.x.ax = 0x3D00;
  inregs.x.dx = FP_OFF(scsi_manager);
  segregs.ds  = FP_SEG(scsi_manager);
  int86x( 0x21, &inregs, &outregs, &segregs);

  if ( outregs.x.cflag ) {
    puts( "ASPI driver not loaded - please load it." );
    return -1;
  }

  uint16_t handle = outregs.x.ax;

  // Get the entry point for the far call to the ASPI driver using an IOCTL.
  inregs.x.bx = handle;
  inregs.x.ax = 0x4402;
  inregs.x.dx = FP_OFF(&Aspi_entry_point);
  segregs.ds  = FP_SEG(&Aspi_entry_point);
  inregs.x.cx = 4;
  int86x( 0x21, &inregs, &outregs, &segregs);

  if ( outregs.x.cflag ) {
    puts( "IOCTL to ASPI driver failed." );
    return -2;
  }

  // Close the SCSI Manager file handle - we have what we need.
  inregs.x.bx = handle;
  inregs.x.ax = 0x3E00;
  int86x( 0x21, &inregs, &outregs, &segregs);

  // Additional safety check; ensure the entry point was set.
  if (!FP_SEG(Aspi_entry_point)) {
    puts( "ASPI entry point was not set." );
    return -1;
  }

  /* Send SCSI Inquiry to confirm DaynaPORT */
  uint8_t inquiry_data[36];

  clearSrb( );
  memset(inquiry_data, 0, sizeof(inquiry_data));

  Srb.SRB_Flags = SRB_Flags_DirectionIn;
  Srb.SRB_BufLenLo = sizeof(inquiry_data);
  Srb.SRB_BufOffset = FP_OFF(inquiry_data);
  Srb.SRB_BufSegment = FP_SEG(inquiry_data);

  if (scsi_command(SCSI_CDB_Cmd_Inquiry)) {
    puts( "SCSI Inquiry command failed." );
    return -3;
  }

  if (strncmp((char*)inquiry_data + 8, "Dayna", 5) != 0) {
    puts( "Incorrect response from SCSI inquiry." );
    return -4;
  }

  if (enable_interface(1, 1)) {
    puts( "Enable interface failed." );
    return -5;
  }


  // The first instructions of a packet driver are a jump to code.  We
  // don't know where the packet driver interrupt handler is going to land
  // so the first jump will go to a fixed place and we'll patch in the
  // actual code address at that fixed place.

  packet_driver_header.jmp_offset = FP_OFF(packetDriverSoftwareInt);
  packet_driver_header.jmp_segment = FP_SEG(packetDriverSoftwareInt);

  return 0;
}


/* Send a packet */
int send_packet(uint8_t *buffer, uint16_t length) {

  if (length > MaxPacketSize) return -1;

  clearSrb( );

  Srb.SRB_Flags = SRB_Flags_DirectionOut;
  Srb.SRB_BufLenLo = length;
  Srb.SRB_BufSegment = FP_SEG(buffer);
  Srb.SRB_BufOffset = FP_OFF(buffer);

  return scsi_command(SCSI_CDB_Cmd_Write);
}


// Receive a packet
//
// Return  0 if there was nothing to receive.
// Return  n (n is the length of the packet) if we have something
// Return -1 if we are told there is a lost packet.

// The SCSI read gives us Length (2) + Flags (4) + Payload + CRC (4).
// We're adding 10 bytes for the non-Payload bytes.
static uint8_t recvBuffer[MaxPacketSize + 10];

int receive_packet( void ) {

  clearSrb( );

  Srb.SRB_Flags = SRB_Flags_DirectionIn;
  Srb.SRB_BufLenLo = MaxPacketSize + 10;
  Srb.SRB_BufSegment = FP_SEG(recvBuffer);
  Srb.SRB_BufOffset = FP_OFF(recvBuffer);

  // We always return 0 for a read command, as they either have something
  // or it times out after a quick period.  The length will be 0 if it
  // was a timeout.
  scsi_command( SCSI_CDB_Cmd_Read );


  // Length includes the Payload and the ending CRC, but not the
  // six bytes of the Length field and Flags.

  uint16_t length = (recvBuffer[0] << 8) | recvBuffer[1];
  if ( length == 0 ) return 0;


  uint32_t *flags = (uint32_t *)(recvBuffer+2);
  if (*flags == 0xFFFFFFFF) {

    // They DaynaPort is supposed to set flags to this when we lose a
    // packet, but the emulated DaynaPort doesn't do this.

    return -1;  // Lost packets
  }

  /* We do not want to send back the 4-byte CRC, so discard it */
  length = length - 4;

  // Return the actual length of the Payload.  The caller is responsible
  // for skipping the first six bytes of Length and Flags in the buffer.
  return length;
}



// Packet driver handle table code
//
// * Support up to 10 handles
// * If an application wants to process everything then only
//   the first handle is used and no others are allowed.
// * The handle we pass back to the user has an arbitrary offset greater
//   than zero to catch errors where they forget to send the handle to us.
//
// Special values:
//
// * 1501 to indicate the handle is not in use.
// * 1502 to indicate handling all Ethertypes. (Will never appear more than once.)
//
// Both of these are safe with Ethernet or Ethernet II framing.

const uint16_t Handle_Available = 1501;
const uint16_t Handle_All       = 1502;

const uint16_t HandleUserOffset = 21;

struct HandlerRec_t {
  uint16_t type;
  void far (*userCode)();
};

static HandlerRec_t HandleTable[MaxPacketHandles];

static int HandlesInUse;

void initHandleTable( void ) {
  for ( int i=0; i < 10; i++ ) {
    HandleTable[i].type = Handle_Available;
  }
  HandlesInUse = 0;
}

int findHandleSlot( uint16_t target ) {
  for (int i=0; i < MaxPacketHandles; i++ ) {
    if ( HandleTable[i].type == target ) {
      return i;
    }
  }
  return -1;
}





void interrupt pollPacket( ... ) {

  if ( PacketSoftwareIntActive || PacketPollingIntActive ) return;
  if ( HandlesInUse == 0 ) return;

  // If you do an early return be sure to set this back to 0.
  PacketPollingIntActive = 1;

  int length = receive_packet( );

  if ( length < 1 ) {
    // 0 means no data, -1 means lost packets.
    //
    // For lost packets we should disable and re-enable the interface,
    // but this is probably not safe to do under the timer interrupt.
    // And the emulated DaynaPort doesn't even return the flags value to
    // cause this, so do nothing for now.
    PacketPollingIntActive = 0;
    return;
  }

  // This is going to need fixing ..  it will work for Ethernet II though.
  uint16_t etherType = *((uint16_t *)(recvBuffer + 6 + 12));

  // If somebody is accepting all packets they are in the first handler
  // slot and the if check in the foor loop will match immediately.
  // Otherwise, find the slot that matches so we know where to call.
  int i;
  for (i=0; i < MaxPacketHandles; i++ ) {
    if ( (HandleTable[i].type == Handle_All) ||
	 (HandleTable[i].type == etherType) ) {
      break;
    }
  }


  if ( i < MaxPacketHandles ) {

    // The following code gets tricky because we need to set registers for
    // the user receiver routine, but the compiler likes to use registers
    // too.  In general, do the complicated stuff first so that when it
    // comes time to set register those are memory moves, not calculations.

    void far (*driver_handle)() = HandleTable[i].userCode;
    uint16_t handle = i + HandleUserOffset;

    // First call; tell them we want a buffer.
    _BX = handle;
    _CX = length;
    _AX = 0;
    (*driver_handle)();

    uint16_t saveES = _ES;
    uint16_t saveDI = _DI;

    if (_ES !=0 || _DI != 0) {

      // 6 is the magic offset that skips the Length and Flags field
      // on a packet read from the device.
      memcpy(MK_FP(saveES, saveDI), recvBuffer + 6, length);

      // Save DS, and set DS last before the call because if you set it
      // earlier you lose addressability to your variables.  Restore it
      // immediately after the call.

      asm push ds

      _BX = handle;
      _CX = length;
      _AX = 1;
      _SI = saveDI;
      _DS = saveES;
      (*driver_handle)();

      asm pop ds
    }

  }

  PacketPollingIntActive = 0;
}


// Packet driver interrupt service routine

void interrupt packetDriverSoftwareInt( ... ) {

  // This code is not re-entrant, so set a flag so the other interrupt
  // service routines know we are active.
  PacketSoftwareIntActive = 1;


  // Trickery to enable us to work with the incoming registers more easily.
  InterruptRegs regs;
  InitRegs( );

  switch (regs.h.AH) {

    case PKT_CMD_DRIVER_INFO: {

      // The spec says that AL should be set to 0xFF by the caller, but we
      // don't need to enforce that.  It allows the caller to detect that
      // the call failed independently of checking the carry flag because
      // AL will normally be set by this call.
      //
      // The caller also passes us a handle in BX, but we don't check it;
      // nothing in this call requires the handle to be correct.

      regs.x.BX = 1;                   // Version
      regs.h.CH = 1;                   // Class (Ethernet)
      regs.x.DX = 0xff;                // Type (our own)
      regs.h.CL = 0;                   // Interface number
      regs.h.AL = 1;                   // Basic functions only
      regs.x.DS = FP_SEG(driver_name); // Driver name segment
      regs.x.SI = FP_OFF(driver_name); // Driver name offset
      ClearCarry( );
      break;
    }

    case PKT_CMD_ACCESS_TYPE: {

      // Insist on the Class being Ethernet and the interface number being 0
      // but don't bother looking at the Ethernet interface type; if they
      // used our software interrupt then they are getting our device.

      if ( regs.h.AL != 1 ) {
	regs.h.DH = PKT_ERR_NO_CLASS;
	SetCarry( );
	break;
      }

      if ( regs.h.DL != 0 ) {
	regs.h.DH = PKT_ERR_NO_NUMBER;
	SetCarry( );
	break;
      }

      // TypeLen = 0 means match all packets.
      //
      // If we are matching all then no other handles can be active.
      // We are guaranteed to be using the first slot in the handle table.
      if ( (regs.x.CX == 0) && (HandlesInUse == 0) ) {
	HandleTable[0].type = Handle_All;
	HandleTable[0].userCode = (void far (*)())MK_FP(regs.x.ES, regs.x.DI);
	HandlesInUse++;
	regs.x.AX = HandleUserOffset;
	ClearCarry( );
	break;
      }

      // If they didn't ask for everything, then the TypeLen must be 2.
      if ( regs.x.CX != 2 ) {
	regs.h.DH = PKT_ERR_BAD_TYPE;
	SetCarry( );
	break;
      }

      // Don't bother swapping the bytes to network order; if we do then
      // we have to swap the bytes of the EtherType on every incoming
      // packet.  Just don't be surprised if you are debugging and you
      // see the byte order is reversed.
      uint16_t targetType = *((uint16_t *)MK_FP(regs.x.DS, regs.x.SI));
      int alreadyUsed = findHandleSlot( targetType );
      if ( alreadyUsed != -1 ) {
	regs.h.DH = PKT_ERR_TYPE_IN_USE;
	SetCarry( );
	break;
      }

      int handleSlot = findHandleSlot( Handle_Available );
      if ( handleSlot == -1 ) {
	regs.h.DH = PKT_ERR_NO_SPACE;
	SetCarry( );
	break;
      }

      HandleTable[handleSlot].type = targetType;
      HandleTable[handleSlot].userCode = (void far (*)())MK_FP(regs.x.ES, regs.x.DI);
      HandlesInUse++;
      regs.x.AX = handleSlot + HandleUserOffset;
      ClearCarry( );

      break;
    }

    case PKT_CMD_RELEASE_TYPE: {

      int slot = regs.x.BX - HandleUserOffset;

      if ( (slot < 0) || (slot >= MaxPacketHandles) ||
	   (HandleTable[slot].type == Handle_Available) ) {
	regs.h.DH = PKT_ERR_BAD_HANDLE;
	SetCarry( );
	break;
      }

      HandleTable[slot].type = Handle_Available;
      HandlesInUse--;
      ClearCarry( );
      break;
    }

    case PKT_CMD_SEND_PKT: {
      int rc = send_packet((uint8_t *)MK_FP(regs.x.DS, regs.x.SI), regs.x.CX);
      if ( rc ) {
	SetCarry( );
	regs.h.DH = PKT_ERR_CANT_SEND;
      } else {
	ClearCarry( );
      }
      break;
    }

    case PKT_CMD_TERMINATE: {
      // Interrupts will be restored and the memory released when the user
      // exits from the command shell that we started.
      //
      // Todo: This is supposed to wipe out the driver associated with the
      // passed in handle.  What do we do if multiple handles are active?
      ClearCarry( );
      break;
    }

    case PKT_CMD_GET_ADDRESS: {

      int slot = regs.x.BX - HandleUserOffset;

      if ( (slot < 0) || (slot >= MaxPacketHandles) ||
	   (HandleTable[slot].type == Handle_Available) ) {
	regs.h.DH = PKT_ERR_BAD_HANDLE;
	SetCarry( );
	break;
      }

      if ( regs.x.CX < 6 ) {
	regs.h.DH = PKT_ERR_NO_SPACE;
	SetCarry( );
      } else {
	get_mac_address( (uint8_t *)MK_FP(regs.x.ES, regs.x.DI), regs.x.CX );
	regs.x.CX = 6;
	ClearCarry( );
      }

      break;
    }

    case PKT_CMD_RESET_INTERFACE: {

      int slot = regs.x.BX - HandleUserOffset;

      if ( (slot < 0) || (slot >= MaxPacketHandles) ||
	   (HandleTable[slot].type == Handle_Available) ) {
	regs.h.DH = PKT_ERR_BAD_HANDLE;
	SetCarry( );
	break;
      }

      // The spec says that if multiple handles are in use that we should
      // return CANT_RESET.  It doesn't say "must", so we're just going to
      // honor their request.

      enable_interface(0, 1);
      enable_interface(1, 1);

      ClearCarry( );
      break;
    }

    default: {
      regs.h.DH = PKT_ERR_BAD_COMMAND;
      SetCarry( );
    }

  }

  // If you forget to do this you are going to have a bad time.
  PacketSoftwareIntActive = 0;

  // Setup the outgoing register values for the caller.
  CopyRegs( );
}



// The compiler is going to generate 26 instructions for an empty
// ISR, mostly just pushes and pops for saving register state.
// This is going to get patched to do the following:
//
//  * Push the flags
//  * Do a far call to the previously registered interrupt handler.
//  * Jump to our pollPacket ISR
//
// Why go through all of this?  Because the ASPI driver is behaving
// strangely when invoked under the BIOS tick interrupt, and honestly it is
// probably too much code to be running under that interrupt.  So this
// patch lets the entire timer interrupt chain run and exit, and then
// invokes our code so that while it is technically under the interrupt
// the interrupt servicing is already complete before we get control.
//
// I'm not a fan of self-modifying code or patching things like this so
// it is a proof of concept.  The correct way to do this is to use TASM
// and create another module that has this coded explicitly.

void interrupt realISR( ... ) { };

void patchRealISR( void ) {

  uint8_t *a = (uint8_t *)realISR;

  *a++ = 0x9c; // pushf
  *a++ = 0x2e; // Preface the call with the CS: segment
  *a++ = 0xff; // Far call, byte 1
  *a++ = 0x1e; // Far call, byte 2

  // Set the data offset for the far call.  The original ISR is copied
  // to the bytes right after this code.
  *((uint16_t *)a) = FP_OFF(realISR) + 0x0B;
  a = a + 2;

  *a++ = 0xea; // Far jump

  // Offset and segment for the Far jump
  *((uint16_t *)a) = FP_OFF(pollPacket);
  a = a + 2;
  *((uint16_t *)a) = FP_SEG(pollPacket);
  a = a + 2;

  // Offset and segment for the Far call
  *a++ = ((uint8_t *)&ExistingTimerInterrupt)[0];
  *a++ = ((uint8_t *)&ExistingTimerInterrupt)[1];
  *a++ = ((uint8_t *)&ExistingTimerInterrupt)[2];
  *a++ = ((uint8_t *)&ExistingTimerInterrupt)[3];

}



void installPacketDriver( void ) {

  initHandleTable( );

  // Install the packet driver.
  void interrupt far (*oldPacketVector)(...) = getvect(PacketVector);
  setvect(PacketVector, (void interrupt (*)(...))&packet_driver_header);


  // Save the existing Timer and DOS Idle interrupts.
  ExistingTimerInterrupt = getvect( Interrupt_Timer );
  ExistingIdleInterrupt = getvect( Interrupt_DosIdle );

  patchRealISR( );


  // Install the Timer ISR.  (The shim actually.)
  setvect( Interrupt_Timer, realISR );

  // If requested, install the DOS Idle ISR.
  if ( IdleHook ) {
    setvect( Interrupt_DosIdle, pollPacket );
  }

  printf( "Driver installed at Int 0x%02X (%p) for SCSI ID %d and Adapter ID %d\n",
	  PacketVector, &packet_driver_header, ScsiId, AdapterId);
  puts( "Starting a new DOS shell.  Use \"exit\" to end and unload the driver." );


  // This little piece of gratuitous code alters the prompt slightly
  // to remind people that the driver is loaded.
  strcpy( recvBuffer, "PROMPT=\xFE " );
  char *currentPrompt = getenv( "PROMPT" );
  if ( currentPrompt != NULL ) {
    strcpy( recvBuffer + 9, currentPrompt );
  } else {
    strcpy( recvBuffer + 9, "$n$g" );
  }
  putenv( recvBuffer );


  system("command.com");


  // Restore what we took over.
  //
  // If something else hooked these interrupts while we were active
  // you are going to have a bad time.  We could be fancy and unlink
  // ourselves from the chain, but that's effort ... just stop loading
  // TSRs and unloading the ones in the middle!

  setvect( Interrupt_Timer, ExistingTimerInterrupt );
  setvect( Interrupt_DosIdle, ExistingIdleInterrupt );

  setvect( PacketVector, oldPacketVector);

  puts("DaynaPort packet driver: unloaded!");
};



int parseArgs( int argc, char *argv[] ) {

  if (argc < 3) return -1;

  PacketVector = (unsigned char) strtol(argv[1], NULL, 16);
  if (PacketVector < 0x60 || PacketVector > 0x80) return -1;

  ScsiId = atoi(argv[2]);

  for (int i=3; i < argc; i++ ) {

    if ( stricmp(argv[i], "-adapter") == 0 ) {
      i++;
      if ( i == argc ) return -1;
      AdapterId = atoi(argv[i]);
    } else if ( stricmp(argv[i], "-idlehook") == 0 ) {
      IdleHook = 1;
    } else {
      return -1;
    }

  }

  return 0;
}


/* Prints program usage and... well... exits! I know, right? */
void usage( void ) {

  puts(
    "Usage: dayna.exe <packet_int> <scsi_id> [-adapter <adapter_id>] [-idlehook]\n"
    "\n"
    "Where:\n"
    "  <packet_int>  Is the software interrupt to install the driver at.\n"
    "  <scsi_id>     Is the SCSI ID of the Daynaport device.\n"
    "  <adapter_id>  Is the adapter number if you have more than one adapter.\n"
    "\n"
    "Example: dayna.exe 0x60 3\n"
    "\n"
    "Use the -idlehook option to improve performance with mTCP.\n"
    "\n"
    "Loading the driver starts a new command shell.  To unload the driver\n"
    "use the \"exit\" command at the command prompt.\n"
  );
  exit(1);
}


void main(int argc, char *argv[]) {

  puts( "\nDaynaPORT SCSI/Link Packet Driver, " __DATE__ "\n" );

  if ( parseArgs( argc, argv ) ) usage( );

  if (initDaynaPort( ) != 0) {
    printf( "Initialization failed\n" );
    exit(1);
  }

  installPacketDriver( );

}
