//NOT YET FUNCTIONAL

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

#define _RTSPFUN_H_IMPLEMENTATION
#include "rtmpfun.h"

#include "os_generic.h"

int g_mbw, g_mbh;

int akey = 0;

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
	int r;
	OGCreateThread( InputThread, 0 );
	struct RTMPSession rtmp;
	{
		FILE * f = fopen( ".streamkey", "rb" );
		if( !f )
		{
			fprintf( stderr, "Error: could not open .streamkey\n" );
			return -5;
		}
		char streamkey[256] = { 0 };
		if( fscanf( f, "%255s\n", streamkey ) != 1 )
		{
			fprintf( stderr, "Error: could not parse .streamkey\n" ); 
			return -6;
		}
		fclose( f );
//ingest.vrcdn.live
//localhost
		r = InitRTMPConnection( &rtmp, 0, "rtmp://ingest.vrcdn.live/live", streamkey );
		memset( streamkey, 0, sizeof( streamkey ) );
		if( r )
		{
			return r;
		}
	}

	printf( "RTMP Server connected.\n" );

	OGUSleep( 500000 );

	H264Funzie funzie;
	{
		int w = 256;
		int h = 128;
		g_mbw = w/16;
		g_mbh = h/16;
		const H264ConfigParam params[] = { { H2FUN_TIME_ENABLE, 1 }, { H2FUN_TIME_NUMERATOR, 500 }, { H2FUN_TIME_DENOMINATOR, 60000 }, { H2FUN_TERMINATOR, 0 } };
		r = H264FunInit( &funzie, w, h , 1, (H264FunData)RTMPSend, &rtmp, params );
		if( r )
		{
			fprintf( stderr, "Closing due to H.264 fun error.\n" );
			return r;
		}
	}

	usleep(500000);
	int frameno = 0;
	int cursor;
	while( 1 )
	{
		int bk;
		frameno++;

		if( ( frameno % 100 ) == 1 )
		{
			H264FakeIFrame( &funzie );
			//H264FunEmitIFrame( &funzie );
		}
		else
		{
			for( bk = 0; bk < 10; bk++ )
			{
				int mbx = 0;
				int mby = 0;

				int basecolor = akey?254:1;
				uint8_t * buffer = malloc( 256 );
				memset( buffer, 0xff, 256 );
				if( bk == 0 )
				{
					mbx = mby = 0;
					basecolor = 1;
				}
				else
				{
					mbx = cursor%g_mbw;
					mby = (cursor/g_mbw)%g_mbh;
					cursor++;
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
					struct timeval tv;
					time_t t;
					struct tm *info;

					gettimeofday(&tv, NULL);
					t = tv.tv_sec;
					info = localtime(&t);
					char towrite[100];
					int cx, cy;
					int writepos = 0;
					for( cy = 0; cy < 2; cy++ )
					for( cx = 0; cx < 2; cx++ )
					{
						uint16_t pxls = 0;
						if( cy == 0 )
						{
							if(cx < 2)
								pxls = font[(info->tm_min/((cx==0)?10:1))%10];
							else
								pxls = font[(info->tm_sec/((cx==2)?10:1))%10];
						}
						else if( cy == 1 )
						{
							int tens = 1;
							int tc = cx;
							while( tc != 3) { tc++; tens*=10; }
							pxls = font[(tv.tv_usec/100/tens)%10];
						}
						int px, py;
						for( py = 0; py < 8; py++ )
						for( px = 0; px < 8; px++ )
						{
							int color = basecolor;
							if( px < 3 ) color = ((pxls>>(14-(py*3-3+px)))&1)?(255-basecolor):basecolor;
							int pos = (py+cy*8)*16+px+cx*4;
							buffer[pos] = color;
						}
					}
				}
				H264FunAddMB( &funzie, mbx,  mby, buffer, H264FUN_PAYLOAD_LUMA_ONLY );
			}
			H264FunEmitFrame( &funzie );
			//H264FunEmitIFrame( &funzie );
		}
		OGUSleep( 10000 );
	}

	return 0;
}



