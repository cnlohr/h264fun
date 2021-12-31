/*
	RTMP Transmission Client - i.e. this connects to a remote host and
	sends it an RTMP stream.

    RTMP IS NOT YET FUNCTIONAL
*/


#ifndef _RTMPFUN_H
#define _RTMPFUN_H


#include "os_generic.h"
#include <stdint.h>

#define RTMP_DEFAULT_PORT 1935
#define RTMP_SEND_BUFFER (131072*8)

struct RTMPSession
{
	int sock;
	void * opaque;
	uint8_t * nalbuffer;
	int bookmarksize;
	int nallen;
	int tmsgid;
	int remote_chunk_size;
	void * thread_handle;
};

// Function returns upon successful connection or failure.
int InitRTMPConnection( struct RTMPSession * rt, void * opaque, const char * uri, const char * streamkey );
void RTMPClose( struct RTMPSession * rt );
int RTMPSend( struct RTMPSession * rt, uint8_t * buffer, int len );


#ifdef _RTSPFUN_H_IMPLEMENTATION
#include "rtmpfun.c"
#endif

#endif

