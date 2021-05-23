/******************************************************************************
 * This file, along with all other files and components of this project, is   *
 * subject to the Apache 2.0 license included at the root of the project.     *
 *                                                                            *
 * (c) 2021 Teaglu, LLC                                                       *
 ******************************************************************************/

#pragma once


/*
 * This file defines the binary structures passed between the service and the
 * front-end user interface.
 */

/*
 * Since this is all done on the same host we don't have to worry about big-end
 * little-end stuff.
 *
 * I believe we should explicitly set the packing because the default might
 * change down the line.  Right now we're not doing 32-bit builds but that may
 * be something people ask for.  The default for 32-bit builds is 8 and the
 * default for 64-bit builds is 16.
 *
 * Some of the members are two-byte shorts, but space isn't a premium here and
 * my bigger concern is getting unalignment faults.  So I'm using 16 to be on
 * the safe side.
 *
 * I guess I could call SetErrorMode() on startup but it feels wrong.
 *
 * -DAW 201201
 */

#pragma pack(push, autovpn, 16)

#define AV_VERSION					0x01

#define AV_MESSAGE_STATUS			0x01
#define AV_MESSAGE_SUGGESTION		0x02

typedef struct _AutoVPNHeader {
	unsigned char version;
	unsigned char opcode;
	short length;
} AutoVPNHeader;

#define AVS_UNKNOWN					0x00		// Unknown
#define AVS_DISCONNECTED			0x01		// No IP4 connection
#define AVS_NETWORK					0x02		// Network connected
#define AVS_INTRANET				0x03		// Onsite network
#define AVS_INTERNET				0x04		// Offsite with internet
#define AVS_VPN_ENABLED				0x05		// Offsite with VPN trying
#define AVS_VPN_CONNECTED			0x06		// Offsite with VPN connected
#define AVS_VPN_DISABLED			0x07		// Disabled by headend

typedef struct _AutoVPNStatus {
	short state;
	char ssid[32];
	short wifiProblem;
	short signalQuality;
	unsigned long rxRate;
	unsigned long txRate;
} AutoVPNStatus;

#pragma pack(pop, autovpn)
