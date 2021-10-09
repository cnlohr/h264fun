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

#define _H264FUN_H_IMPL
#include "h264fun.h"

#include "os_generic.h"

// Notes:
//  You can test RTSP streams with vlc by making a rtsp server with:
//   cvlc -vvv testbase.h264 --rtsp-tcp --sout '#rtp{sdp=rtsp://:8554/}'
//  and connecting with:
//   vlc -vvv rtsp://localhost:8554/
//  Then using wireshark to capture the packets.


#define TX_MTU 1400000
struct connection
{
	int tx_buffer_place;
	int sock;
	int fault;
	int slotid;
	uint8_t tx_buffer[TX_MTU];
	int playing;
	int connected;
	int seqid;
	og_thread_t thread;
};

void DataCallback( void * opaque, uint8_t * data, int bytes )
{	
	struct connection * conn = (struct connection*)opaque;
	int sock = conn->sock;
	conn->connected = 1;
	// If bytes == -1, then, it indicates a new NAL -> YOU as the USER must emit the 00 00 00 01 (or other header)
	// If bytes == -2, then that indicates a flush.

	if( bytes == -1 )
	{
	//	data = "\x00\x00\x00\x01";
	//	bytes = 4;
		bytes = -2;
	}
	if( bytes == -2 )
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
			*((uint32_t*)(&conn->tx_buffer[12])) = htonl( 0x3188a09b );

			printf( "Sending %d to %d\n", conn->tx_buffer_place, sock );
			if( send( sock, conn->tx_buffer, conn->tx_buffer_place, MSG_NOSIGNAL ) < 0 ) conn->fault = 1;

			static FILE * fdump;
			if( !fdump ) fdump = fopen( "rtsptest.h264", "wb" );
			*((uint32_t*)(&conn->tx_buffer[12])) = htonl( 0x00000001 );
			fwrite( conn->tx_buffer+12, conn->tx_buffer_place-12, 1, fdump );
			fflush( fdump );
			conn->tx_buffer_place = 16;
		}
		return;
	}

//	while( bytes > 0 )
	{
		int willendat = bytes + conn->tx_buffer_place;
		if( willendat > TX_MTU ) willendat = TX_MTU;
		int tosend = willendat - conn->tx_buffer_place;
		memcpy( conn->tx_buffer + conn->tx_buffer_place, data, tosend );
		conn->tx_buffer_place += tosend;
		bytes -= tosend;
		data += tosend;
//		if( conn->tx_buffer_place >= TX_MTU )
//		{
//			printf( "Sending %d to %d\n", conn->tx_buffer_place, sock );
//			if( send( sock, conn->tx_buffer, conn->tx_buffer_place, MSG_NOSIGNAL ) < 0 ) conn->fault = 1;
//			conn->tx_buffer_place = 0;
//		}
	}

	return;
}


void * GThread( void * v )
{
	struct connection * conn = (struct connection*)v;
	int sock = conn->sock;
	int r;
	int timeout = 0;
    
	conn->fault = 0;

	struct timeval timeoutrx;
	timeoutrx.tv_sec = 0;
	timeoutrx.tv_usec = 1000;
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
			if( torecord >= sizeof( rx_cmd_buffer ) )
			{
				fprintf( stderr, "Error: overflow from client (%d)\n", torecord );
				goto closeconn;
			}
			memcpy( rx_cmd_buffer + rx_cmd_place, buf, torecord );
			rx_cmd_place += torecord;
			rx_cmd_buffer[rx_cmd_place] = 0;
			printf( "rx_cmd_place = %d\n", rx_cmd_place );
			if( rx_cmd_place > 4 && strncmp( rx_cmd_buffer + rx_cmd_place - 4, "\r\n\r\n", 4 ) == 0 )
			{
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

				puts( rx_cmd_buffer );
				printf( "URI: \"%s\"\n", uri );

				if( strncmp( rx_cmd_buffer, "OPTIONS", 7 ) == 0 )
				{
					int n = sprintf( sendbuff, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nPublic: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE\r\n\r\n", cseq );
					send( sock, sendbuff, n, MSG_NOSIGNAL );
				}
				else if( strncmp( rx_cmd_buffer, "PAUSE", 5 ) == 0 )
				{
					int n = sprintf( sendbuff, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\n\r\n", cseq, sessid );
					send( sock, sendbuff, n, MSG_NOSIGNAL );
					conn->playing = 0;
				}
				else if( strncmp( rx_cmd_buffer, "PLAY", 4 ) == 0 )
				{
					int n = sprintf( sendbuff, "RTSP/1.0 200 OK\r\nCSeq: %d\r\nSession: %d\r\nRTP-Info: url=%s\r\n\r\n", cseq, sessid, uri );
					send( sock, sendbuff, n, MSG_NOSIGNAL );
					conn->playing = 1;
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
o=- 16504144009441403338 16504144009441403338 IN IP4 cnlohr-1520\n\
s=Unnamed\n\
i=N/A\n\
c=IN IP4 0.0.0.0\n\
t=0 0\n\
a=type:broadcast\n\
m=video 0 RTP/AVP 96\n\
b=RR:0\n\
a=rtpmap:96 H264/90000\n\
a=fmtp:96 packetization-mode=2;profile-level-id=420029;sprop-parameter-sets=Z0IAKY3gQAgmAovAAAD6AAAdTAJIUL4=,aM46gA==;\n\
a=control:rtsp://127.0.0.1:8554/trackID=0\n\
";



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
				printf( "Setup H264 Stream\n" );

				conn->tx_buffer_place = 16;
				conn->seqid = 0;

				//{ H2FUN_TIME_ENABLE, 0 },
				//const H264ConfigParam params[] = { { H2FUN_TIME_NUMERATOR, 1000 }, { H2FUN_TIME_DENOMINATOR, 15000 }, { H2FUN_TERMINATOR, 0 } };
				const H264ConfigParam params[] = { { H2FUN_TIME_ENABLE, 0 }, { H2FUN_TERMINATOR, 0 } };
				r = H264FunInit( &funzie, 256, 256, 1, DataCallback, conn, params );
				if( r )
				{
					fprintf( stderr, "Error: H264FunInit returned %d\n", r );
					goto closeconn;
				}
			}
			else
			{
				printf( "Stop H264 Stream\n" );
				H264FunClose( &funzie );
			}
			emitting = conn->playing;
		}
		
		if( emitting )
		{
			static int frameno;
			frameno++;

			if( frameno % 10 )
			{
				// emitting
				int bk;
				for( bk = 0; bk < 100; bk++ )
				{
					uint8_t * buffer = malloc( 256 );
					int i;
					for( i = 0; i < 256; i++ )
					{
						memset( buffer, (i&1)*255, 256 );
					}
					H264FunAddMB( &funzie, rand()%(funzie.w/16), rand()%(funzie.h/16), buffer, H264FUN_PAYLOAD_LUMA_ONLY );
				}
				H264FunEmitFrame( &funzie );
			}
			else
			{
				H264FakeIFrame(&funzie);
			}
			timeout = 0;
		}
		printf( "...\n" ); fflush( stdout );
		timeout++;
		if( timeout > 6000 )
		{
			fprintf( stderr, "Timeout\n" );
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
	printf( "Closing socket on connection ID %d\n", conn->slotid );
	if( emitting ) H264FunClose( &funzie );
	close( sock );
	conn->connected = 0;
	return 0;
}

int main()
{
	struct sockaddr_in serveraddr;
	int sock = socket(AF_INET, SOCK_STREAM, 0);

	if( sock < 0 )
	{
		fprintf( stderr, "Error, couldn't create rtsp socket.\n" );
		exit( -1 );
	} 

	{
		int optval = 1;
		setsockopt( sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int) );
	}

	memset( &serveraddr, 0, sizeof(serveraddr) );

	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons( 554 );

	if( bind( sock, (struct sockaddr *) &serveraddr, sizeof(serveraddr) ) < 0 )
	{
		fprintf( stderr, "Error, couldn't bind rtsp socket.\n" );
		exit( -2 );
	}

	if( listen( sock, 5 ) < 0 )
	{
		fprintf( stderr, "Error, couldn't listen rtsp socket.\n" );
		exit( -2 );
	}

	int retries = 0;

	#define MAX_CONNECTIONS 1024
	struct connection * connections = calloc( sizeof( struct connection ), MAX_CONNECTIONS );

	while( 1 )
	{
		int clientlen;
		struct sockaddr_in clientaddr;
		clientlen = sizeof( clientaddr );
		int rxsock = accept( sock, (struct sockaddr *) &clientaddr, &clientlen );
  
		if( rxsock < 0 )
		{
			fprintf( stderr, "Error, couldn't accept rtsp socket.\n" );
			if( retries > 10 ) exit( -5 );
			retries++;
			continue;
		}
		retries = 0;
  		int i;
		for( i = 0; i < MAX_CONNECTIONS; i++ )
		{
			struct connection * conn = connections + i;
			if( conn->connected == 0 )
			{
				conn->sock = rxsock;
				conn->playing = 0;
				conn->connected = 1;
				conn->tx_buffer_place = 16;
				conn->slotid = i;
				if( conn->thread ) OGJoinThread( conn->thread );
				conn->thread = OGCreateThread( GThread, (void*)conn );
				break;
			}
		}
		if( i == MAX_CONNECTIONS )
		{
			fprintf( stderr, "Error: Too many connections\n" );
			close( rxsock );
		}
	}

	close( sock );

}



