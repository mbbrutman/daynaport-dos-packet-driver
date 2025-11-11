
#ifndef _PACKET_H
#define _PACKET_H

#include "types.h"


// From crynwr.com/packet_driver.html, version 1.09 of the specification.


// Commands

const uint8_t PKT_CMD_DRIVER_INFO     = 1;
const uint8_t PKT_CMD_ACCESS_TYPE     = 2;
const uint8_t PKT_CMD_RELEASE_TYPE    = 3;
const uint8_t PKT_CMD_SEND_PKT        = 4;
const uint8_t PKT_CMD_TERMINATE       = 5;
const uint8_t PKT_CMD_GET_ADDRESS     = 6;
const uint8_t PKT_CMD_RESET_INTERFACE = 7;


// Error codes

const uint8_t PKT_ERR_BAD_HANDLE     =  1;  // Invalid handle
const uint8_t PKT_ERR_NO_CLASS       =  2;  // No interfaces with this class
const uint8_t PKT_ERR_NO_TYPE        =  3;  // No interfaces with this type
const uint8_t PKT_ERR_NO_NUMBER      =  4;  // No interfaces with this number
const uint8_t PKT_ERR_BAD_TYPE       =  5;  // Bad packet type
const uint8_t PKT_ERR_NO_MULTICAST   =  6;  // Interface doesn't do multicast
const uint8_t PKT_ERR_CANT_TERMINATE =  7;  // Can't terminate the driver
const uint8_t PKT_ERR_BAD_MODE       =  8;  // Invalid receiver mode
const uint8_t PKT_ERR_NO_SPACE       =  9;  // Operation failed due to lack of space
const uint8_t PKT_ERR_TYPE_IN_USE    = 10;  // Type is already in use
const uint8_t PKT_ERR_BAD_COMMAND    = 11;  // Command not implemented
const uint8_t PKT_ERR_CANT_SEND      = 12;  // Packet could not be sent
const uint8_t PKT_ERR_CANT_SET       = 13;  // Can't set the hardware address
const uint8_t PKT_ERR_BAD_ADDRESS    = 14;  // Bad hardware address format
const uint8_t PKT_ERR_CANT_RESET     = 15;  // Unable to reset the interface


#endif