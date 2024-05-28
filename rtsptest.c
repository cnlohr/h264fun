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

char sprop[1024];

int CommonFunInit( H264Funzie * funzie, H264FunData datacb, void * opaque )
{
	const int FPS = 1;

//{ H2FUN_TIME_ENABLE, 0 }
//	const H264ConfigParam params_normal[] = { { H2FUN_TIME_NUMERATOR, 10000 }, { H2FUN_TIME_DENOMINATOR, FPS*10000 }, { H2FUN_TERMINATOR, 0 } };
//	const H264ConfigParam params_spsonly[] = { { H2FUN_TIME_NUMERATOR, 10000 }, { H2FUN_TIME_DENOMINATOR, FPS*10000 }, { H2FUN_SPSPPSONLY, 0 }, { H2FUN_TERMINATOR, 0 } };
	const H264ConfigParam params_normal[] = { { H2FUN_TIME_ENABLE, 0 }, { H2FUN_TERMINATOR, 0 } };
	const H264ConfigParam params_spsonly[] = { { H2FUN_TIME_ENABLE, 0 }, { H2FUN_SPSPPSONLY, 0 }, { H2FUN_TERMINATOR, 0 } };
	const H264ConfigParam * params = ( opaque == 0 ) ? params_spsonly : params_normal;
	return H264FunInit( funzie, 512, 512, 1, datacb, opaque, params );
}

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

		r = CommonFunInit( &demo->funzie, RTSPSend, conn );
		if( !r )
			demo->frameno = 0;
		return r;

	case RTSP_DEINIT:
		printf( "Stop H264 Stream\n" );
		H264FunClose( &demo->funzie );
		return 0;

	case RTSP_PLAY:
		demo->frameno = 0;
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
							pxls = font[(((long long int)(dNow*100))/((cx==0)?10:1))%10];
						else
							pxls = font[(((long long int)dNow)/((cx==2)?10:1))%10];
					}
					else if( cy == 1 )
					{
						int tens = 1;
						int tc = cx;
						while( tc != 3) { tc++; tens*=10; }
						pxls = font[(((long long int)(dNow*100000))/100/tens)%10];
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


//from http://stackoverflow.com/questions/342409/how-do-i-base64-encode-decode-in-c
static const char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                      'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                      'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                      'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                      'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                      'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                      'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                      '4', '5', '6', '7', '8', '9', '+', '/'};

static const int mod_table[] = {0, 2, 1};

void my_base64_encode(const unsigned char *data, unsigned int input_length, uint8_t * encoded_data )
{

	int i, j;
	int output_length = 4 * ((input_length + 2) / 3);

	if( !encoded_data ) return;
		if( !data ) { encoded_data[0] = '='; encoded_data[1] = 0; return; }

	for (i = 0, j = 0; i < input_length; ) {

		uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
		uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
		uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

		uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

		encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
	}

	for (i = 0; i < mod_table[input_length % 3]; i++)
        	encoded_data[output_length - 1 - i] = '=';

	encoded_data[j] = 0;
}


uint8_t spropbuffer[1024];
int spropbufferplace = 0;
char * sproptail;

void SPropRx( void * opaque, uint8_t * data, int bytes )
{
	if( bytes >= 0 )
	{
		if( bytes + spropbufferplace < sizeof( spropbuffer ) )
		{
			memcpy( spropbuffer + spropbufferplace, data, bytes );
			spropbufferplace += bytes;
		}
	}
	else if( bytes == -4 || bytes == -3 )
	{
		// A sync.
		char b64buff[spropbufferplace*2];
		my_base64_encode( spropbuffer, spropbufferplace, b64buff );
		int blen = strlen( b64buff );
		if( blen + sproptail - sprop < sizeof( sprop ) - 2 )
		{
			memcpy( sproptail, b64buff, blen );
			sproptail += blen;
			if( bytes == -4 )
				sproptail[0] = ',';
			else
				sproptail[0] = ';';
			sproptail++;
			sproptail[1] = 0;
		}
		spropbufferplace = 0;
	}
}


int main() {
	OGCreateThread( InputThread, 0 );

	sproptail = sprop;

	H264Funzie stream_template;
	CommonFunInit( &stream_template, SPropRx, 0 );
	H264FunClose( &stream_template );

	struct RTSPSystem system;
	if( StartRTSPFun( &system, RTSP_DEFAULT_PORT, RTSPControlCallback, DEFAULT_MAX_CONNS ) )
	{
		fprintf( stderr, "Error: StartRTSPFun failed.\n" );
	}
	return 0;
}



