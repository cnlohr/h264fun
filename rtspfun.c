#include "rtspfun.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>


struct RTSPSystem;



// Notes:
//  You can test RTSP streams with vlc by making a rtsp server with:
//   cvlc -vvv testbase.h264 --rtsp-tcp --sout '#rtp{sdp=rtsp://:8554/}'
//  and connecting with:
//   vlc -vvv rtsp://localhost:8554/
//  Then using wireshark to capture the packets.

void RTSPSend( void * opaque, uint8_t * buffer, int bytes )
{
	struct RTSPConnection * conn = (struct RTSPConnection *)opaque;
	int sock = conn->sock;
	conn->connected = 1;
	// If bytes == -1, then, it indicates a new NAL -> YOU as the USER must emit the 00 00 00 01 (or other header)
	// If bytes == -2, then that indicates a flush.  -3 = flush and we're a header. -4 = header with cork.

	if( bytes == -1 )
	{
	//	data = "\x00\x00\x00\x01";
	//	bytes = 4;
		bytes = -2;
	}
	if( bytes < -1 )
	{
		if( conn->tx_buffer_place != 16 )
		{
			int pll = conn->tx_buffer_place-4;
			conn->tx_buffer[0] = 0x24;
			conn->tx_buffer[1] = 0x00;
			conn->tx_buffer[2] = pll>>8;
			conn->tx_buffer[3] = pll&0xff;

			conn->tx_buffer[4] = 0x80; // RTPHeader
			conn->tx_buffer[5] = 0x60; // Dynamic
			*((uint16_t*)(&conn->tx_buffer[6])) = htons( conn->seqid++ ); //Seq
			*((uint32_t*)(&conn->tx_buffer[8])) = htonl( OGGetAbsoluteTime()*90000 ); //Timestamp
			*((uint32_t*)(&conn->tx_buffer[12])) = htonl( 0 ); // ????

//			printf( "Sending %d to %d\n", conn->tx_buffer_place, sock );
			if( send( sock, conn->tx_buffer, conn->tx_buffer_place, MSG_NOSIGNAL ) < 0 ) conn->fault = 1;

		//	static FILE * fdump;
		//	if( !fdump ) fdump = fopen( "rtsptest.h264", "wb" );
		//	*((uint32_t*)(&conn->tx_buffer[12])) = htonl( 0x00000001 );
		//	fwrite( conn->tx_buffer+12, conn->tx_buffer_place-12, 1, fdump );
		//	fflush( fdump );
			conn->tx_buffer_place = 16;
		}
		return;
	}

//	while( bytes > 0 )
	{
		int willendat = bytes + conn->tx_buffer_place;
		if( willendat > TX_MTU )
		{
			fprintf( stderr, "Error: buffer overflow in RTSP send socket.\n" );
			close( sock );
			return;
		}
		int tosend = willendat - conn->tx_buffer_place;
		memcpy( conn->tx_buffer + conn->tx_buffer_place, buffer, tosend );
		conn->tx_buffer_place += tosend;
	}

	return;
}

void * GThread( void * v )
{
	struct RTSPConnection * conn = (struct RTSPConnection*)v;
	int sock = conn->sock;
	int r;
	int timeout = 0;
    
	conn->fault = 0;

	struct timeval timeoutrx;
	timeoutrx.tv_sec = 0;
	timeoutrx.tv_usec = conn->rxtimedelay; //10ms
    if( setsockopt( sock, SOL_SOCKET, SO_RCVTIMEO, &timeoutrx, sizeof timeoutrx ) < 0 )
	{
        fprintf( stderr, "Error, couldn't set timeout on rx socket.\n" );
		goto closeconn;
	}

	int flag = 1;
	setsockopt(sock, IPPROTO_TCP, /*TCP_NODELAY*/1, (char *) &flag, sizeof(int));

	H264Funzie funzie;
	uint8_t rx_cmd_buffer[4096] = { 0 };
	int rx_cmd_place = 0;
	int emitting = 0; //If this doesn't match "playing" need to change stream state.

	while( !conn->fault )
	{
		uint8_t buf[2048];
		r = recv( sock, buf, sizeof( buf ), 0 );
		if( r < 0 )
		{
			int e = errno;
			if( e != EAGAIN && e != EWOULDBLOCK )
			{
				fprintf( stderr, "R: %d / %d\n", r, errno );
				goto closeconn;
			}
		}
		if( r > 0 )
		{
			timeout = 0;
			int torecord = r + rx_cmd_place;
			if( torecord + rx_cmd_place >= sizeof( rx_cmd_buffer ) )
			{
				fprintf( stderr, "Error: overflow from client (%d)\n", torecord );
				//goto closeconn;
				rx_cmd_place = 0;
				continue;
			}
			memcpy( rx_cmd_buffer + rx_cmd_place, buf, torecord );
			rx_cmd_place += torecord;
			rx_cmd_buffer[rx_cmd_place] = 0;
			//printf( "rx_cmd_place = %d\n", rx_cmd_place );

			if( conn->rxmode == 1 )
			{

				//int i;
				//for( i = 0; i < torecord; i++ )
				//{
				//	int c = rx_cmd_buffer[i];
				//	printf( "%02x '%c' ", c, (c>32 && c<128)?c:' ' );
				//}
				//printf( "\n" );

				if( rx_cmd_buffer[0] == '0' )
					conn->rxmode = 0;
				else if( rx_cmd_buffer[0] == '$' ) //0x24 packet.
				{
					//RTP packet.
					int len = (rx_cmd_buffer[2]<<8) | rx_cmd_buffer[3];
					int remain = rx_cmd_place - (len+4);
					if( remain >= 0 )
					{
						//int i;
						//for( i = 0; i < torecord; i++ )
						//{
						//	int c = rx_cmd_buffer[i];
						//	printf( "%02x '%c' ", c, (c>32 && c<128)?c:' ' );
						//}
						//printf( "\n" );

						
						if( remain > 0 )
						{
							int k, l = len+4;;
							for( k = 0; k < len+4; k++, l++ )
							{
								rx_cmd_buffer[k] = rx_cmd_buffer[l];
							}
						}
						rx_cmd_place = remain;
					}
				}
				else conn->rxmode = 0;
			}
			if( conn->rxmode == 0 && rx_cmd_place > 4 && strncmp( rx_cmd_buffer + rx_cmd_place - 4, "\r\n\r\n", 4 ) == 0 )
			{
				puts( rx_cmd_buffer );
				char sendbuff[1024];
				char * uri = 0;
				int cseq = 0;
				int sessid = 0;

				char * tstr = strstr( rx_cmd_buffer, "\r\nCSeq: " );
				if( tstr ) cseq = atoi( tstr + 8 );
				char * cseqstr = strstr( rx_cmd_buffer, "\r\nSession: " );
				if( tstr ) sessid = atoi( tstr + 11 );
				uri = (rx_cmd_place>15)?strstr( rx_cmd_buffer, " "):0;
				if( uri )
				{
					uri++;
					char * uriend = uri;
					while( *uriend != ' ' && *uriend )
					{
						uriend++;
					}
					*uriend = 0;
				}

				//puts( rx_cmd_buffer );
				//printf( "URI: \"%s\"\n", uri );

				if( strncmp( rx_cmd_buffer, "GET", 3 ) == 0 )
				{
					close( sock );
				}
				else if( strncmp( rx_cmd_buffer, "PAUSE", 5 ) == 0 )
				{
					int n = sprintf( sendbuff, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\n\r\n", cseq, sessid );
					send( sock, sendbuff, n, MSG_NOSIGNAL );
					conn->playing = 0;
				}
				else if( strncmp( rx_cmd_buffer, "OPTIONS", 5 ) == 0 )
				{
					int n = sprintf( sendbuff, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\nPublic: DESCRIBE, SETUP, PLAY, PAUSE, OPTIONS\r\n\r\n", cseq, sessid );
					send( sock, sendbuff, n, MSG_NOSIGNAL );
				}
				else if( strncmp( rx_cmd_buffer, "PLAY", 4 ) == 0 )
				{
					int n = sprintf( sendbuff, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\nRTP-Info: url=%s\r\n\r\n", cseq, sessid, uri );
					send( sock, sendbuff, n, MSG_NOSIGNAL );
					conn->playing = 1;
					conn->rxmode = 1;
				}
				else if( strncmp( rx_cmd_buffer, "SETUP", 5 ) == 0 )
				{
					puts( uri + strlen( uri ) + 1 );
					int n = sprintf( sendbuff, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\nTransport: RTP/AVP/TCP;interleaved=0-1;ssrc=000001F6\r\nx-Dynamic-Rate: 1\r\nx-Transport-Options: late-tolerance=1.400000\r\n\r\n", cseq, conn->slotid );
					send( sock, sendbuff, n, MSG_NOSIGNAL );
/* Example:

SETUP rtsp://ipvmdemo.dyndns.org:5541/onvif-media/media.amp/stream=0?profile=profile_1_h264&sessiontimeout=60&streamtype=unicast RTSP/1.0
CSeq: 5
Authorization: Digest username="demo", realm="AXIS_WS_ACCC8E2105E2", nonce="004a7345Y951522c1e3ba51dd0566d20e09583f0aea9aa9", uri="rtsp://ipvmdemo.dyndns.org:5541/onvif-media/media.amp/", response="2ad90c0094f46b1da6fdfe97ddf2a1b0"
User-Agent: LibVLC/3.0.9.2 (LIVE555 Streaming Media v2020.01.19)
Transport: RTP/AVP/TCP;unicast;interleaved=0-1

RTSP/1.0 200 OK
CSeq: 5
Transport: RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=FB269F08;mode="PLAY"
Server: GStreamer RTSP server
Session: ByjhuWDUZKcbKpqs;timeout=60
Date: Sat, 22 May 2021 02:10:58 GMT

or

RTSP/1.0 200 OK
CSeq: 4
Date: Sat, Oct 09 2021 09:14:11 GMT
Server: VRCDN Media Server 1.0.3(built on Jul 22 2021 18:35:23)
Session: 4fJ65ofdMEaw
Transport: RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=000001F6
x-Dynamic-Rate: 1
x-Transport-Options: late-tolerance=1.400000



*/



				}
				else if( strncmp( rx_cmd_buffer, "DESCRIBE", 8 ) == 0 )
				{
					const char * stream_description = stream_description = "\
v=0\r\n\
o=- 0 0 IN IP4 0.0.0.0\r\n\
s=Unnamed\n\
i=N/A\n\
c=IN IP4 0.0.0.0\n\
t=0 0\n\
a=type:broadcast\n\
m=video 0 RTP/AVP 96\n\
b=RR:0\n\
a=rtpmap:96 H264/90000\n\
a=cliprect:0,0,256,128\n\
a=fmtp:96 packetization-mode=2;profile-level-id=42e01f;sprop-parameter-sets=Z0IAKY3gQAgmAovAAAD6AAAdTAJIUL4=,aM46gA==;\n\
";

//XXX TODO: sprop-parameter-sets should be pps/sps of encoded stream.


//a=control:rtsp://127.0.0.1:8554/trackID=0\n\

//o=- 16504144009441403338 16504144009441403338 IN IP4 cnlohr-1520\n\




					int n = sprintf( sendbuff, "\
RTSP/1.0 200 OK\r\n\
CSeq: %d\r\n\
Content-Base: %s\r\n\
Content-Type: application/sdp\r\n\
Content-Length: %d\r\n\r\n%s", cseq, uri, (int)strlen( stream_description ), stream_description );
					send( sock, sendbuff, n, MSG_NOSIGNAL );
				}
/* Example valid stream: (webcam)
v=0
o=- 13456116403499583314 1 IN IP4 192.168.1.51
s=Session streamed with GStreamer
i=rtsp-server
t=0 0
a=tool:GStreamer
a=type:broadcast
a=range:npt=now-
a=control:rtsp://ipvmdemo.dyndns.org:5541/onvif-media/media.amp?profile=profile_1_h264&sessiontimeout=60&streamtype=unicast
m=video 0 RTP/AVP 96
c=IN IP4 0.0.0.0
b=AS:50000
a=rtpmap:96 H264/90000
a=fmtp:96 packetization-mode=1;profile-level-id=4d001f;sprop-parameter-sets=Z00AH5pkAoAt//+H/4gANwEBAUAAAPpAAB1MOhgBOcABOcLvLjQwAnOAAnOF3lw31A==,aO48gA==
a=ts-refclk:local
a=mediaclk:sender
a=recvonly
a=control:rtsp://ipvmdemo.dyndns.org:5541/onvif-media/media.amp/stream=0?profile=profile_1_h264&sessiontimeout=60&streamtype=unicast
a=framerate:15.000000
a=transform:1.000000,0.000000,0.000000;0.000000,1.000000,0.000000;0.000000,0.000000,1.000000

-or- from cvlc
o=- 16504144009441403338 16504144009441403338 IN IP4 cnlohr-1520
s=Unnamed
i=N/A
c=IN IP4 0.0.0.0
t=0 0
a=tool:vlc 3.0.9.2
a=recvonly
a=type:broadcast
a=charset:UTF-8
a=control:rtsp://127.0.0.1:8554/
m=video 0 RTP/AVP 96
b=RR:0
a=rtpmap:96 H264/90000
a=fmtp:96 packetization-mode=1;profile-level-id=420029;sprop-parameter-sets=Z0IAKY3gQAgmAovAAAD6AAAdTAJIUL4=,aM46gA==;
a=control:rtsp://127.0.0.1:8554/trackID=0

-or- from VRCDN
v=0
o=- 0 0 IN IP4 0.0.0.0
s=Streamed by VRCDN Media Server 1.0.3(built on Jul 22 2021 18:35:17)
c=IN IP4 0.0.0.0
t=0 0
a=range:npt=now-
a=control:*
m=video 0 RTP/AVP 96
a=rtpmap:96 H264/90000
a=fmtp:96 packetization-mode=1; profile-level-id=640028; sprop-parameter-sets=Z2QAKKwspQHgCJ+XAWoEBAqAAAH0AAB1MHAAAB6EgAAPQkN3lwU=,aOuPLA==
a=control:trackID=0
a=framesize:96 1920-1080
a=cliprect:0,0,1080,1920
a=framerate:30.00
m=audio 0 RTP/AVP 98
a=rtpmap:98 mpeg4-generic/44100/2
a=fmtp:98 streamtype=5;profile-level-id=1;mode=AAC-hbr;sizelength=13;indexlength=3;indexdeltalength=3;config=1210
a=control:trackID=1

*/

				rx_cmd_place = 0;
				rx_cmd_buffer[rx_cmd_place] = 0;
			}
		}

		if( conn->playing != emitting )
		{
			if( conn->playing )
			{
				if( conn->system->ctrl( conn, RTSP_INIT ) )
				{
					fprintf( stderr, "Error: H264FunInit returned %d\n", r );
					goto closeconn;
				}
				else
				{
					conn->system->ctrl( conn, RTSP_PLAY );
				}
			}
			else
			{
				conn->system->ctrl( conn, RTSP_PAUSE );
				conn->system->ctrl( conn, RTSP_DEINIT );
			}
			emitting = conn->playing;
		}
		
		if( emitting )
		{
			conn->system->ctrl( conn, RTSP_TICK );
			timeout = 0;
		}

		timeout++;
		if( timeout > 6000 )
		{
		//	fprintf( stderr, "Timeout\n" );
			goto closeconn;
		}
	}
/*
	struct connection * conn = malloc( sizeof( struct connection ) );
	conn->tx_buffer_place = 0;
	conn->sock = sock;


	int frame;
	for( frame = 0; frame < 400; frame++ )
	{
	}
*/
closeconn:
	conn->system->ctrl( conn, RTSP_TERMINATE );
	printf( "Closing socket on connection ID %d\n", conn->slotid );
	if( emitting ) H264FunClose( &funzie );
	close( sock );
	conn->connected = 0;
	return 0;
}



//returns 0 on success, otherwise negative.
int StartRTSPFun( struct RTSPSystem * system, int port, RTSPControl ctrl, int max_connections )
{
	if( !system )
	{
		return -9;
	}

	memset( system, 0, sizeof( struct RTSPSystem ) );
	
	struct sockaddr_in serveraddr;
	system->max_connections = max_connections;
	system->ctrl = ctrl;
	system->sock = socket( AF_INET, SOCK_STREAM, 0 );

	if( system->sock < 0 )
	{
		fprintf( stderr, "Error, couldn't create rtsp socket.\n" );
		exit( -1 );
	} 

	{
		int optval = 1;
		setsockopt( system->sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int) );
	}

	memset( &serveraddr, 0, sizeof(serveraddr) );

	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons( port );

	if( bind( system->sock, (struct sockaddr *) &serveraddr, sizeof(serveraddr) ) < 0 )
	{
		fprintf( stderr, "Error, couldn't bind rtsp socket.\n" );
		exit( -2 );
	}

	if( listen( system->sock, 5 ) < 0 )
	{
		fprintf( stderr, "Error, couldn't listen rtsp socket.\n" );
		exit( -2 );
	}

	system->connections = calloc( sizeof( struct RTSPConnection ), system->max_connections );
	system->success = 1;

	int retries = 0;

	while( 1 )
	{
		int clientlen;
		struct sockaddr_in clientaddr;
		clientlen = sizeof( clientaddr );
		int rxsock = accept( system->sock, (struct sockaddr *) &clientaddr, &clientlen );
  
		if( rxsock < 0 )
		{
			fprintf( stderr, "Error, couldn't accept rtsp socket.\n" );
			if( retries > 10 )
			{
				close( system->sock );
				break;
			}
			retries++;
			continue;
		}
		retries = 0;
  		int i;
		for( i = 0; i < max_connections; i++ )
		{
			struct RTSPConnection * conn = system->connections + i;
			if( conn->connected == 0 )
			{
				conn->sock = rxsock;
				conn->playing = 0;
				conn->connected = 1;
				conn->tx_buffer_place = 16;
				conn->slotid = i;
				conn->system = system;
				memset( conn->working_url, 0, sizeof( conn->working_url ) );
				conn->rxtimedelay = 10000;
				conn->opaque = 0;
				if( conn->thread ) OGJoinThread( conn->thread );
				conn->thread = OGCreateThread( GThread, (void*)conn );
				break;
			}
		}
		if( i == max_connections )
		{
			fprintf( stderr, "Error: Too many connections\n" );
			close( rxsock );
		}
	}
	system->sock = 0;
}

void RTSPFunStop( struct RTSPSystem * system )
{
	if( !system ) return;

	close( system->sock );

	if( system->connections )
	{
		int i;
		for( i = 0; i < system->max_connections; i++ )
		{
			struct RTSPConnection * conn = system->connections + i;
			conn->fault = 1;
			close( conn->sock );
		}
		for( i = 0; i < system->max_connections; i++ )
		{
			struct RTSPConnection * conn = system->connections + i;
			OGJoinThread( conn->thread );
		}
	}
}

