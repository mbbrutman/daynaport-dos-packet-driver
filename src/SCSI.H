
#ifndef _SCSI_H
#define _SCSI_H

#include "types.h"

// SCSI Stuff

const int ASPI_CDBLen = 6;           // SCSI Command Descriptor Block len
const int ASPI_SenseLen = 14;        // SCSI Sense data length


// SCSI command opcodes

const uint8_t SCSI_CDB_Cmd_Read            = 0x08;
const uint8_t SCSI_CDB_Cmd_GetStatus       = 0x09;
const uint8_t SCSI_CDB_Cmd_Write           = 0x0A;
const uint8_t SCSI_CDB_Cmd_EnableMulticast = 0x0D;
const uint8_t SCSI_CDB_Cmd_EnableInterface = 0x0E;
const uint8_t SCSI_CDB_Cmd_Inquiry         = 0x12;


// ASPI/SCSI SRB stuff

const uint8_t SRB_Cmd_ExecuteIOCmd   = 0x02;  // Execute a command

const uint8_t SRB_Flags_DirectionIn  = 0x08;  // Receive from device to host
const uint8_t SRB_Flags_DirectionOut = 0x10;  // Send from host to device

const uint8_t SRB_Status_Pending     = 0x00;  // In progress
const uint8_t SRB_Status_Complete    = 0x01;  // Completed without error
const uint8_t SRB_Status_Aborted     = 0x02;  // Aborted by host
const uint8_t SRB_Status_UnableAbort = 0x03;  // Unable to abort
const uint8_t SRB_Status_Error       = 0x04;  // Completed with an error
const uint8_t SRB_Status_InvalidCmd  = 0x80;  // Invalid command
const uint8_t SRB_Status_InvalidHost = 0x81;  // Invalid Host Adapter number
const uint8_t SRB_Status_NoDevice    = 0x82;  // SCSI device not installed


/* Offset Structure for 6-byte CDBs        */
/* Shamelessly stolen from the Adaptec SDK */

typedef struct {
  uint8_t  SRB_Cmd;                /* 00/000 ASPI command code = SC_EXEC_SCSI_CMD */
  uint8_t  SRB_Status;             /* 01/001 ASPI command status byte */
  uint8_t  SRB_HaId;               /* 02/002 ASPI host adapter number */
  uint8_t  SRB_Flags;              /* 03/003 ASPI request flags */
  uint8_t  SRB_Hdr_Rsvd[4];        /* 04/004 Reserved, MUST = 0 */

  uint8_t  SRB_Target;             /* 08/008 Target's SCSI ID */
  uint8_t  SRB_Lun;                /* 09/009 Target's LUN number */

  uint16_t SRB_BufLenLo;           /* 0A/010 Data Allocation Length */
  uint16_t SRB_BufLenHi;           /* 0C/010 Data Allocation Length */

  uint8_t  SRB_SenseLen;           /* 0E/014 Sense Allocation Length */

  uint16_t SRB_BufOffset;          /* 0F/015 Data Buffer Pointer */
  uint16_t SRB_BufSegment;         /* 11/015 Data Buffer Pointer */

  uint16_t SRB_NextOffset;         /* 13/026 Link to next SRB - Not used */
  uint16_t SRB_NextSegment;        /* 15/026 Link to next SRB - Not used */

  uint8_t  SRB_CDBLen;             /* 17/023 CDB Length = 6 */
  uint8_t  SRB_HaStat;             /* 18/024 Host Adapter Status */
  uint8_t  SRB_TargStat;           /* 19/025 Target Status */

  uint16_t SRB_PostOffset;         /* 1A/025 Target Status */
  uint16_t SRB_PostSegment;        /* 1C/025 Target Status */

  uint8_t  SRB_Rsvd2[34];          /* 1E/030 Reserved, MUST = 0 */
  uint8_t  CDBByte[ASPI_CDBLen];   /* 40 SCSI CDB */
  uint8_t  SenseArea6[ASPI_SenseLen];  /* 46 Request Sense buffer */
} ASPI_SRB;



#endif