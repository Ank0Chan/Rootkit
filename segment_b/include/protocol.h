/* SPDX-License-Identifier: GPL-2.0 */
/*
 * protocol.h - Command channel protocol between the companion (userland,
 *              segment D) and the kernel core (segment B).
 *
 * This header is shared between kernel space and user space. It is the
 * single source of truth for the command/response wire format exchanged
 * through the "companion" misc device. It must stay stable after the J1
 * milestone (see docs/interface_contract.md).
 */
#ifndef ROOTKIT_B_PROTOCOL_H
#define ROOTKIT_B_PROTOCOL_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
/* Portable aliases so the same struct definitions compile in user space. */
typedef uint8_t  u8;
typedef uint32_t u32;
typedef int32_t  s32;
#endif /* __KERNEL__ */

/*
 * Command identifiers.
 *
 * Values start at 0x01 and grow monotonically. 0x00 is reserved as an
 * "invalid / unset" sentinel so a zero-initialized buffer never maps to a
 * real command. The concrete behaviour of each command is implemented by
 * segment B in companion_dev.c; the table here only fixes the numbering.
 */
#define CMD_NONE          0x00u   /* reserved: no command / unset */
#define CMD_PING          0x01u   /* liveness check, no argument      */
#define CMD_GET_VERSION   0x02u   /* return module version string     */
#define CMD_SET_FLAG      0x03u   /* set an internal runtime flag     */
#define CMD_CLEAR_FLAG    0x04u   /* clear an internal runtime flag   */

/*
 * Status codes returned in cmd_response.status.
 *
 * These are small positive conventions carried in a signed field so a
 * negative value can still be used to surface a raw -errno if needed.
 */
#define CMD_STATUS_OK       0   /* command handled successfully         */
#define CMD_STATUS_ERR      1   /* command recognized but failed        */
#define CMD_STATUS_UNKNOWN  2   /* command id not recognized by the core */

/*
 * Request sent from the companion to the kernel core.
 *
 * The payload is a flexible array member: the caller allocates
 * sizeof(struct cmd_request) + arg_len bytes. arg_len MUST describe the
 * exact number of valid bytes in payload[]; the core validates it before
 * any access.
 */
struct cmd_request {
	u32 command;     /* one of the CMD_* identifiers above          */
	u32 arg_len;     /* number of valid bytes in payload[]          */
	u8  payload[];   /* command argument, arg_len bytes (may be 0)  */
};

/*
 * Response returned from the kernel core to the companion.
 *
 * data is a flexible array member, same contract as cmd_request.payload:
 * data_len gives the exact number of valid bytes in data[].
 */
struct cmd_response {
	s32 status;      /* one of the CMD_STATUS_* codes               */
	u32 data_len;    /* number of valid bytes in data[]             */
	u8  data[];      /* response body, data_len bytes (may be 0)    */
};

#endif /* ROOTKIT_B_PROTOCOL_H */
