
#ifndef _Interrupt_Regs
#define _Interrupt_Regs

#include "types.h"

// Sorcery for being able to read and write registers across the interrupt.
//
// On entry the registers are saved at these offsets from BP
//
// 0x16 Flags at interrupt time
// 0x14 (Return address
// 0x12 (Return address)
// 0x10 AX, 0x0E BX, 0x0C CX, 0x0A DX
// 0x08 ES, 0x06 DS, 0x04 SI, 0x02 DI
// 0x00 BP
//
// The flags and return address are put there by the CPU as part of the
// interrupt processing.  The other registers are put there by the compiler.
// That code never changes, so the offsets are stable.
//
// For convenience we are going to copy the saved registers to a data
// structure that we will use in the code.  On exit we'll copy that data
// structure back to the stack over the values that were saved.  The
// epilog code will restore them for us.

#define ClearCarry( ) asm { and byte ptr [bp+0x16], 0xfe }
#define SetCarry( ) asm { or byte ptr [bp+0x16], 0x01 }

struct InterruptWordRegs {
  uint16_t DI;
  uint16_t SI;
  uint16_t DS;
  uint16_t ES;
  uint16_t DX;
  uint16_t CX;
  uint16_t BX;
  uint16_t AX;
};

struct InterruptByteRegs {
  uint8_t  reserved[8];
  uint8_t  DL;
  uint8_t  DH;
  uint8_t  CL;
  uint8_t  CH;
  uint8_t  BL;
  uint8_t  BH;
  uint8_t  AL;
  uint8_t  AH;
};

union InterruptRegs {
  struct  InterruptWordRegs  x;
  struct  InterruptByteRegs  h;
  uint8_t rawBytes[16];
};

// These two defines must be inline.  If they are turned into
// calls BP will change and mess things up.

#define InitRegs( ) { \
  uint8_t *source = (uint8_t *)MK_FP(_SS, _BP+2); \
  memcpy( regs.rawBytes, source, 16 );            \
}

#define CopyRegs( ) { \
  uint8_t *target = (uint8_t *)MK_FP(_SS, _BP+2); \
  memcpy( target, regs.rawBytes, 16 );            \
}

#endif