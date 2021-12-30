/* 
  Minimal RTSP server, allowing multi connections, and custom, per-connection RTSP streams.
*/

#ifndef _RTSPFUN_H
#define _RTSPFUN_H


#include "os_generic.h"
#include <stdint.h>

struct RTSPSystem;

// Note: Normally 554, but 8554 lets you run as a user.
#define RTSP_DEFAULT_PORT 8554
#define DEFAULT_MAX_CONNS 128

// about 300 I_PCM macroblocks.
#define TX_MTU 131072

struct RTSPConnection
{
	int tx_buffer_place;
	int sock;
	int fault;
	int slotid;
	uint8_t tx_buffer[TX_MTU];
	int playing;
	int connected;
	int seqid;
	int rxmode;
	og_thread_t thread;
	struct RTSPSystem * system;
	int  rxtimedelay; //Implicitly controls the frame rate.
	char working_url[1024];
	void * opaque;
};

enum RTSPControlMessage
{
	RTSP_INIT = 0,
	RTSP_DEINIT,

	//TRICKY: When PLAYing a stream, we have to delay.  Some sort of bug with Live555 + VLC + VRChat.
	// Recommend at least 50ms delay.  Because this is threadded, you can sleep.
	RTSP_PLAY,
	RTSP_TICK,
	RTSP_PAUSE,
	RTSP_TERMINATE,
};


//return nonzero on init to fail out.
typedef int (*RTSPControl)( struct RTSPConnection * conn, enum RTSPControlMessage event );

struct RTSPSystem
{
	int success;
	int max_connections;
	int sock;
	struct RTSPConnection * connections;
	RTSPControl ctrl;
};

// Only returns if fault or terminate.
// port RTSP_DEFAULT_PORT is default for RTSP.
int RTSPFunStart( struct RTSPSystem * system, int port, RTSPControl ctrl, int max_connections );
void RTSPSend( void * connection, uint8_t * buffer, int len );

void RTSPFunStop( struct RTSPSystem * system );

#ifdef _RTSPFUN_H_IMPLEMENTATION
#include "rtspfun.c"
#endif

#endif

