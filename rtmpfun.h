/*
	RTMP Transmission Client - i.e. this connects to a remote host and
	sends it an RTMP stream.
*/


#ifndef _RTMPFUN_H
#define _RTMPFUN_H


#include "os_generic.h"
#include <stdint.h>

#define RTMP_DEFAULT_PORT 1935

// about 300 I_PCM macroblocks.
#define TX_MTU 131072


typedef int (*RTMPCallback)( void * connection, void * opaque ); 

// Function does not return, but, locks.
// Returns when callback returns nonzero.

int InitRTMPConnection( RTMPCallback cb, void * opaque, int port );
int RTSPSend( void * connection, uint8_t * buffer, int len );


#ifdef _RTSPFUN_H_IMPLEMENTATION
#include "rtmpfun.c"
#endif

#endif

