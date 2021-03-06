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
	int r;
	H264Funzie funzie;
	FILE * f = fopen( "testfile.h264", "wb" );
//	fwrite( h264fun_mp4header, sizeof( h264fun_mp4header ), 1, f );

	{
		//const H264ConfigParam params[] = { { H2FUN_TIME_NUMERATOR, 1000 }, { H2FUN_TIME_DENOMINATOR, 10000 }, { H2FUN_TERMINATOR, 0 } };
		//r = H264FunInit( &funzie, 256, 256, 1, DataCallback, f, params );
		const H264ConfigParam params[] = { { H2FUN_TIME_ENABLE, 0 }, { H2FUN_TERMINATOR, 0 } };
		r = H264FunInit( &funzie, 256, 256, 1, DataCallback, f, params );

		if( r )
		{
			fprintf( stderr, "Error: H264FunInit returned %d\n", r );
			return -1;
		}
	}

	int frame;
	for( frame = 0; frame < 400; frame++ )
	{
		int bk;
		for( bk = 0; bk < 10; bk++ )
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


	fclose( funzie.opaque );
	H264FunClose( &funzie );
	return 0;
}



