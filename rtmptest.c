#if defined(WINDOWS) || defined(WIN32) || defined( _WIN32 ) || defined( WIN64 )
#include <winsock2.h>
#define MSG_NOSIGNAL      0x200
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <string.h>
#include <errno.h>
#include <stdio.h>

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
		int w = 128;
		int h = 128;
		g_mbw = w/16;
		g_mbh = h/16;
		const H264ConfigParam params[] = { /*{H2FUN_CNT_TYPE, 2}, */{ H2FUN_TIME_ENABLE, 1 },  { H2FUN_TIME_NUMERATOR, 1000 }, { H2FUN_TIME_DENOMINATOR, 30000 }, { H2FUN_TERMINATOR, 0 } };
		r = H264FunInit( &funzie, w, h , 1, (H264FunData)RTMPSend, &rtmp, params );
		if( r )
		{
			fprintf( stderr, "Closing due to H.264 fun error.\n" );
			return r;
		}
	}

	usleep(500000);
	int frameno = 0;
	int cursor = 0;
	while( 1 )
	{
		int bk;
		frameno++;
/*
		if( ( frameno % 200 ) == 1 )
		{
			H264FakeIFrame( &funzie );
			//H264FunEmitIFrame( &funzie );
		}
		else */
		{
			double dNow = OGGetAbsoluteTime();
			
			for( bk = 0; bk < g_mbw*g_mbh; bk++ )
			{
				int mbx = 0;
				int mby = 0;

				int basecolor = akey?254:1;
				uint8_t * buffer = malloc( 256 );
				memset( buffer, 0xff, 256 );
				#if 0
				if( bk == 0 )
				{
					mbx = mby = 0;
				}
				else
				#endif
				{
					mbx = cursor%g_mbw;
					mby = (cursor/g_mbw)%g_mbh;
					cursor++;
				}
				//mbx = bk;
				//mby = 0;

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
						
						int num = dNow * 100;
						int p10 = 1;
						int j;
						for( j = 3; j > cx; j-- )
							p10*=10;

						if( cy == 0 )
						{
							pxls = font[(num/p10)%10];
						}
						else if( cy == 1 )
						{
							pxls = font[(num/10000/p10)%10];
						}
						int px, py;
						for( py = 0; py < 8; py++ )
						for( px = 0; px < 4; px++ )
						{
							int color = ((pxls>>(14-(py*3-3+px)))&1)?(255-basecolor):basecolor;
							if( px == 3 ) color = basecolor;
							
							int pos = (py+cy*8)*16+px+cx*4;
						
							int rpx = px+cx*4 + mbx*16;
							int rpy = py+cy*8 + mby*16;
							
							if( mbx == 0 && mby == 0)
							{
								color = rpx+rpy*16;
							}
							if( mbx == 1 && mby == 0)
							{
								int chat = rpx/8+rpy/8;
								color = (chat&1)?0xff:0x00;
							}
							
							buffer[pos] = color;
						}
					}
				}
				H264FunAddMB( &funzie, mbx,  mby, buffer, H264FUN_PAYLOAD_LUMA_ONLY );
			}
			//H264FunEmitFrame( &funzie );
			H264FunEmitIFrame( &funzie );
		}
		//OGUSleep( 16000 );
		static double dly;
		double now = OGGetAbsoluteTime();
		if( dly == 0 ) dly = now;
		while( dly > now )
		{
			now = OGGetAbsoluteTime();
		}
		dly += 0.05;
	}

	return 0;
}



