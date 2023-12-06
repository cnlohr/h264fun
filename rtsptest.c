#include <stdio.h>
#include <string.h>

#define _H264FUN_H_IMPL
#include "h264fun.h"

#define _RTSPFUN_H_IMPLEMENTATION
#include "rtspfun.h"

#include "os_generic.h"

int akey;

struct OpaqueDemo
{
	int frameno;
	H264Funzie funzie;
};


int RTSPControlCallback( struct RTSPConnection * conn, enum RTSPControlMessage event )
{
	int r, bk;
	struct OpaqueDemo * demo = (struct OpaqueDemo*)conn->opaque;
	printf( "EVENT: %d\n", event );
	switch( event )
	{
	case RTSP_INIT:

		printf( "Setup H264 Stream\n" );

		conn->tx_buffer_place = 16;
		conn->seqid = 0;
		if( !conn->opaque )
			conn->opaque = malloc( sizeof( struct OpaqueDemo ) );
		demo = conn->opaque;

		//{ H2FUN_TIME_ENABLE, 0 },
		//const H264ConfigParam params[] = { { H2FUN_TIME_NUMERATOR, 1000 }, { H2FUN_TIME_DENOMINATOR, 60000 }, { H2FUN_TERMINATOR, 0 } };
		const H264ConfigParam params[] = { { H2FUN_TIME_ENABLE, 0 }, { H2FUN_TERMINATOR, 0 } }; // Disable timing.  (Makes it so RTSP determines timing)
		r = H264FunInit( &demo->funzie, 256, 128, 1, RTSPSend, conn, params );
		if( !r )
			demo->frameno = 0;
		return r;

	case RTSP_DEINIT:
		printf( "Stop H264 Stream\n" );
		H264FunClose( &demo->funzie );
		return 0;

	case RTSP_PLAY:
		demo->frameno = 0;
		conn->rxtimedelay = 20000; // <50 Hz.
		OGUSleep( 500000 );
		return 0;

	case RTSP_TICK:
	{
		double dNow = OGGetAbsoluteTime();
		const int nr_to_send_per_frame = 2;
		// emitting
		for( bk = 0; bk < nr_to_send_per_frame; bk++ )
		{
			int mbx = rand()%(demo->funzie.w/16);
			int mby = rand()%(demo->funzie.h/16);
			int basecolor = rand()%150 + 1;
			uint8_t * buffer = malloc( 256 );

			if( akey ) basecolor = 254;

			if( bk == nr_to_send_per_frame-1 )
			{
				mbx = mby = 0;
				basecolor = akey?1:254;
			}

			const uint16_t font[] = //3 px wide, buffer to 4; 5 px high, buffer to 8.
			{
				0b111101101101111,
				0b010010010010010,
				0b111001111100111,
				0b111001011001111,
				0b001101111001001,
				0b111100111001111,
				0b111100111101111,
				0b111001001010010,
				0b111101111101111,
				0b111101111001001,
				0b000000000000010, //.
				0b000010000010000, //:
				0b000000000000000, // (space)
			};

			{
				char towrite[100];
				int cx, cy;
				int writepos = 0;
				for( cy = 0; cy < 2; cy++ )
				for( cx = 0; cx < 4; cx++ )
				{
					uint16_t pxls = 0;
					if( cy == 0 )
					{
						if(cx < 2)
							pxls = font[(((int)(dNow*100))/((cx==0)?10:1))%10];
						else
							pxls = font[(((int)dNow)/((cx==2)?10:1))%10];
					}
					else if( cy == 1 )
					{
						int tens = 1;
						int tc = cx;
						while( tc != 3) { tc++; tens*=10; }
						pxls = font[(((int)(dNow*100000))/100/tens)%10];
					}
					int px, py;
					for( py = 0; py < 8; py++ )
					for( px = 0; px < 4; px++ )
					{
						int color = basecolor;
						if( px < 3 ) color = ((pxls>>(14-(py*3-3+px)))&1)?(255-basecolor):basecolor;
						int pos = (py+cy*8)*16+px+cx*4;
						buffer[pos] = color;
					}
				}
			}

			H264FunAddMB( &demo->funzie, mbx,  mby, buffer, H264FUN_PAYLOAD_LUMA_ONLY );
		}
		H264FunEmitFrame( &demo->funzie );
		demo->frameno++;
		return 0;
	}
	case RTSP_PAUSE:
		printf( "Play\n" );
		return 0;
	case RTSP_TERMINATE:
		if( demo )
		{
			free( demo );
			conn->opaque = 0;
		}
		return 0;
	}
}

void * InputThread( void * v )
{
	while( 1 )
	{
		int c = getchar();
		if( c == 10 )
			akey = !akey;
	}
}

int main()
{
	OGCreateThread( InputThread, 0 );
	struct RTSPSystem system;
	if( StartRTSPFun( &system, RTSP_DEFAULT_PORT, RTSPControlCallback, DEFAULT_MAX_CONNS ) )
	{
		fprintf( stderr, "Error: StartRTSPFun failed.\n" );
	}
	return 0;
}



