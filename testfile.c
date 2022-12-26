#include <stdio.h>
#include "os_generic.h"

#define _H264FUN_H_IMPL
#include "h264fun.h"


void DataCallback( void * opaque, uint8_t * data, int bytes )
{	
	// If bytes == -1, then, it indicates a new NAL -> YOU as the USER must emit the 00 00 00 01 (or other header)
	// If bytes == -2, then that indicates a flush.  If bytes == -3 it's a flsh and we're a header.  If bytes == -4, it's a header with a cork.

	if( bytes >= 0 )
	{
		fwrite( data, bytes, 1, opaque );
	}
	else
	{
		if( bytes == -1 )
		{
			fwrite( "\x00\x00\x00\x01", 4, 1, opaque );			
		}
		else if( bytes < -1 )
		{
			fflush( opaque );
		}
	}

	return;
}

int main()
{
#define MINITEST

	int r;
	H264Funzie funzie;
	FILE * f = fopen( "testfile.h264", "wb" );
//	fwrite( h264fun_mp4header, sizeof( h264fun_mp4header ), 1, f );

	{
		//const H264ConfigParam params[] = { { H2FUN_TIME_NUMERATOR, 1000 }, { H2FUN_TIME_DENOMINATOR, 10000 }, { H2FUN_TERMINATOR, 0 } };
		//r = H264FunInit( &funzie, 256, 256, 1, DataCallback, f, params );
		const H264ConfigParam params[] = { { H2FUN_TIME_ENABLE, 1 }, { H2FUN_TIME_NUMERATOR, 1000 }, { H2FUN_TIME_DENOMINATOR, 30000 }, { H2FUN_TERMINATOR, 0 }  };

#ifdef MINITEST
		r = H264FunInit( &funzie, 32, 16, 1, DataCallback, f, params );
#else
		r = H264FunInit( &funzie, 512, 512, 1, DataCallback, f, params );
#endif

		if( r )
		{
			fprintf( stderr, "Error: H264FunInit returned %d\n", r );
			return -1;
		}
	}

	int frame;

	int mx = 0;
	int my = 0;
	for( frame = 0; frame < 400; frame++ )
	{
#ifdef MINITEST
		uint8_t * buffer = malloc( 256 );
		memset( buffer, (frame%4)*60+3, 256 );
		H264FunAddMB( &funzie, 0, 0, buffer, H264FUN_PAYLOAD_LUMA_ONLY );

		buffer = malloc( 256 );
		memset( buffer, (frame%4)*60+3, 256 );
		H264FunAddMB( &funzie, 1, 0, buffer, H264FUN_PAYLOAD_LUMA_ONLY );

		H264FunEmitIFrame( &funzie );
#else
		int nmx, nmy;
		switch( frame % 4 )
		{
		case 0: nmx = 2; nmy = 2; break;
		case 1: nmx = 3; nmy = 2; break;
		case 2: nmx = 3; nmy = 3; break;
		case 3: nmx = 2; nmy = 3; break;
		}
		int lx, ly;
		for( lx = 0; lx < 4; lx++ )
		for( ly = 0; ly < 4; ly++ )
		{
			uint8_t * buffer = malloc( 256 );
			memset( buffer, 250, 256 );
			H264FunAddMB( &funzie, nmx*4+lx, nmy*4+ly, buffer, H264FUN_PAYLOAD_LUMA_ONLY );
		}

		for( lx = 0; lx < 4; lx++ )
		for( ly = 0; ly < 4; ly++ )
		{
			uint8_t * buffer = malloc( 256 );
			memset( buffer, 5, 256 );
			H264FunAddMB( &funzie, mx*4+lx, my*4+ly, buffer, H264FUN_PAYLOAD_LUMA_ONLY );
		}
		mx = nmx; my = nmy;
		H264FunEmitFrame( &funzie );
#endif
	}


	fclose( funzie.opaque );
	H264FunClose( &funzie );
	return 0;
}



