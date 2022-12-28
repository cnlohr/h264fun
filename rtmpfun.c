#if defined(WINDOWS) || defined(WIN32) || defined( _WIN32 ) || defined( WIN64 )
#include <winsock2.h>
#undef MSG_NOSIGNAL
#define MSG_NOSIGNAL      0x000
#define MSG_MORE MSG_PARTIAL 
#else
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "rtmpfun.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <errno.h>

#define NAL_START 21


static int rtmpp_finish_header( uint8_t * buffer, int * place, int maxlen, int post_header_place )
{
	int pl = *place;
	if( pl == maxlen ) return -1;
	int plen = pl - post_header_place;
	int hsize = ((buffer[post_header_place]&0xc0) == 0x40)?8:12;
	plen -= hsize;
	printf( "Updating buffer with: %d+... = %d\n", post_header_place, plen );
	buffer[post_header_place+4] = plen>>16;
	buffer[post_header_place+5] = plen>>8;
	buffer[post_header_place+6] = plen>>0;
	return 0;
}

static int rtmpp_add_raw_uint32( uint8_t * buffer, int * place, int maxlen, uint32_t dat )
{
	int pl = *place;
	if( pl + 4 > maxlen ) { *place = maxlen; return -1; }
	buffer[pl++] = dat>>24;
	buffer[pl++] = dat>>16;
	buffer[pl++] = dat>>8;
	buffer[pl++] = dat>>0;
	*place = pl;
	return 0;
}

static int rtmpp_add_boolean( uint8_t * buffer, int * place, int maxlen, int truefalse )
{
	int pl = *place;
	if( pl + 2 > maxlen ) { *place = maxlen; return -1; }
	buffer[pl++] = 1;
	buffer[pl++] = truefalse;
	*place = pl;
	return 0;
}

static int rtmpp_add_raw_null( uint8_t * buffer, int * place, int maxlen )
{
	int pl = *place;
	if( pl + 1 > maxlen ) { *place = maxlen; return -1; }
	buffer[pl++] = 0x05;
	*place = pl;
	return 0;
}


static int rtmpp_add_raw_array( uint8_t * buffer, int * place, int maxlen )
{
	int pl = *place;
	if( pl + 1 > maxlen ) { *place = maxlen; return -1; }
	buffer[pl++] = 0x08;
	*place = pl;
	return 0;
}

static int rtmpp_add_header( uint8_t * buffer, int * place, int maxlen, int pkthdr, int type_id, int stream_id )
{
	int pl = *place;
	if( pl + 12 > maxlen ) { *place = maxlen; return -1; }
	static const uint8_t bufferbase[] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
	memcpy( buffer+pl, bufferbase, sizeof( bufferbase ) );
	buffer[pl] = pkthdr;
	buffer[pl+7] = type_id;
	buffer[pl+8] = stream_id;
	*place = pl + (((pkthdr&0xc0)==0x40)?8:12);
	return 0;
}

static int rtmpp_add_string( uint8_t * buffer, int * place, int maxlen, const char * str )
{
	int stl = strlen( str );
	int pl = *place;
	if( stl + 3 + pl > maxlen ) { *place = maxlen; return -1; }
	buffer[pl++] = 2;
	buffer[pl++] = stl>>8;
	buffer[pl++] = stl&0xff;
	memcpy( buffer+pl, str, stl );
	*place = pl + stl;
	return 0;
}

static int rtmpp_add_number( uint8_t * buffer, int * place, int maxlen, double d )
{
	int pl = *place;
	if( pl + 9 > maxlen ) { *place = maxlen; return -1; }
	buffer[pl++] = 0;
	uint8_t * dbb = (uint8_t*)&d;
	buffer[pl++] = dbb[7];
	buffer[pl++] = dbb[6];
	buffer[pl++] = dbb[5];
	buffer[pl++] = dbb[4];
	buffer[pl++] = dbb[3];
	buffer[pl++] = dbb[2];
	buffer[pl++] = dbb[1];
	buffer[pl++] = dbb[0];
	*place = pl;
	return 0;
}

static int rtmpp_add_object( uint8_t * buffer, int * place, int maxlen )
{
	int pl = *place;
	if( pl + 1 > maxlen ) { *place = maxlen; return -1; }
	buffer[pl] = 3;
	*place = pl + 1;
	return 0;
}

static int rtmpp_add_property( uint8_t * buffer, int * place, int maxlen, const char * propname )
{
	int stl = strlen( propname );
	int pl = *place;
	if( stl + 2 + pl > maxlen ) { *place = maxlen; return -1; }
	buffer[pl++] = stl>>8;
	buffer[pl++] = stl&0xff;
	memcpy( buffer+pl, propname, stl );
	*place = pl + stl;
	return 0;
}

static int rtmpp_add_end_object_marker( uint8_t * buffer, int * place, int maxlen )
{
	int pl = *place;
	if( 3 + pl > maxlen ) { *place = maxlen; return -1; }
	buffer[pl++] = 0;
	buffer[pl++] = 0;
	buffer[pl++] = 9;
	*place = pl;
	return 0;
}


static void * RTMPListenThreadFunction( void * v )
{
	struct RTMPSession * rt = (struct RTMPSession *)v;
	int r;
	do
	{
		uint8_t buffer[8192];
		r = recv( rt->sock, buffer, 8192, MSG_NOSIGNAL );
		int i;
		for( i = 0; i < r; i++ )
		{
			printf( "%02x%c", buffer[i], (i&15)?' ':'\n' );
		}
		printf( "\n" );
	} while( r > 0 );
}

// Function returns upon successful connection or failure.
int InitRTMPConnection( struct RTMPSession * rt, void * opaque, const char * uri, const char * streamkey )
{
#if defined(WINDOWS) || defined(WIN32) || defined( _WIN32 ) || defined( WIN64 )
    WORD wVersionRequested = MAKEWORD(2, 2);
	WSADATA wsaData;
	WSAStartup(wVersionRequested, &wsaData);
#endif
	int urilen = strlen( uri );
	if( urilen < 8 || strncmp( uri, "rtmp://", 7 ) != 0 )
	{
		fprintf( stderr, "Error: Invalid URI. \"%s\"\n", uri );
		return -1;
	}
	char * slashpos = strchr( uri+7, '/' );
	char * app;
	char * server;
	if( slashpos )
	{
		int slen = slashpos - uri - 6;
		server = malloc( slen+1 );
		memcpy( server, uri + 7, slen-1 );
		server[slen-1] = 0;
		puts( server );
		app = strdup( slashpos );
	}
	else
	{
		server = strdup( uri + 7 );
		app = strdup( "/" );
	}

	{
		int portno = RTMP_DEFAULT_PORT;
		char * colon = strchr( server, ':' );
		if( colon )
		{
			portno = atoi( colon+1 );
			*colon = 0;
		}


		printf( "Connecting to host: \"%s\":%d app:\"%s\"\n", server, portno, app );

		struct hostent * he = gethostbyname( server );
		struct sockaddr_in rmt_addr;
		if( !he )
		{
			fprintf( stderr, "Error: gethostbyname returned error.\n" );
			return -2;
		}

		rt->sock = socket( AF_INET, SOCK_STREAM, 0 );
		if( rt->sock < 0 )
		{
			fprintf( stderr, "Error: could not create socket to connect to RTMP server.\n" );
			return -3;
		}

		rmt_addr.sin_family = AF_INET;
		rmt_addr.sin_port = htons( portno );
		rmt_addr.sin_addr = *((struct in_addr *)he->h_addr);
		memset(&(rmt_addr.sin_zero), 0, sizeof(rmt_addr.sin_zero));

		if (connect( rt->sock, (struct sockaddr *)&rmt_addr, sizeof( rmt_addr ) ) < 0 )
		{
			fprintf( stderr, "Error: could not connect to RTMP server \"%s\":%d\n", server, portno );
			goto closeconn;
		}
#if 0
		struct timeval timeoutrx;
		timeoutrx.tv_sec = 0;
		timeoutrx.tv_usec = 10000000;
		if( setsockopt( rt->sock, SOL_SOCKET, SO_RCVTIMEO, &timeoutrx, sizeof timeoutrx ) < 0 )
		{
			fprintf( stderr, "Error, couldn't set timeout on rx socket.\n" );
			goto closeconn;
		}
#endif

	}

	rt->opaque = opaque;

	// Now, do the weird handshake.
	{
		uint8_t my_c0c1[1537];
		my_c0c1[0] = 3;
const char * OrigCode = "0057b48600000000f691af67102d5101eaa3bd072f1b0b5d496faa04957bc94666b8c574cb3f0325b45fba7a7884f66d4b2c534b46293f7b307f901c55f36606aa73f07cba28aa6173fa0c2f5c2e3e0991962a3fa722e367d3552a7705b1b14ba8cce033cc15be698f3f68309c0fb067ea24bb4d86aac20715cc7637aa54e420362c62400c5e261fba81352220d01f483b79317c03f1df26b64be90ea131f770cf30e34b6aaba3091ab6ed5e1a5d3617b1d4e2044a357e7b70509d1d5b48d301045e285de34aaa4cb776110b96f4521c8a6d8d348acc3b029ba50468323a6e6856e2f96b2ae56c18ce491e504107b539b08f2f20e4159507eb5b995ae6bb9160f073bb26a6ddce7c068cb1282bedec22a9ceae23bcd79a37cc1ee41378ff916f27833e41e6d4d172935cc806d8572146300a506e03ad652433a0f4473568784be6f70f71ea160653cb5ccb6770659d2575e341556602d04fa29f0b0ecbc53b4190e73c6871e9295e0ccdf07a40776c0855ffbe65f8288a552633fe6845737a0c9e0659522cbfaf117060672f47d50776e9964a493c7f4b43c0d49965101a890a23541d365331626ce871aa50535e6d2456dec7101b129f1888c6e56f3cd6d7010529a56b5323b157ac3b75277a0ce740b92581274edb803546d22202490dbe0fbfc4aa13529f137d89842a1814c469794ac89d52afb728015937e405e8cef624dc76d812c9974b3530a4fe1ac50d235c06179778f0789800d527ac66296bb42e43aafa6cbd9956377cc921539988c27dd8abf54f05900743d55e9a7fddd49a3b58b3b81a819a0f2758e1817c12d93942cf75905c9eb3a47e5be6f7518f3a3b70f052b87be56a226aa3fea4693b1b564e94224b6bfd35896f23ea4c737099237ec6cdd424538e4b0e35a7465acce46b1d4307e40e0acff240f54f204c86b1de7bc76849787219421f1f3aa1799f143f4877a94962f4983b797de9d903cf5c027d75334b20d5ca5b00e1353c3f45a9db7c737e007f3d1c3411d4e3166d63d1b87a2287567b77e2bb569eec0e49b6a9a16674184546c2d65b3c2743c5643be6196b1565a74a5cea0b3f07cb8508596c8b5967b9fe7ffd1aa654df1d6a552e2248786f34e873ff570b4fce368740e6dd3156f3f046484b206144b53a34536924926820ebbc4497707012aecd6d659369bd43d48ca42382b18452f63a763ef613fb1ef993402995278507acbd9c056eac856f57fee043d300626aa9929f5a6c63880e30eb6d29b05d2563c5cf136897a46c29ad78cb37a5ed7d3dc5c6b4211cadb32ba445890c93fd3b62028be5019736d054de1d9d26b8c51955005b623dfe085a6b4f368a67ae28d0229172172f23c32e0b30da547588ad8d6d19d7292a2a6e951e1dd51275c594c62f981a1b0e74d3f3389995281a41adba68e0367c47c9809643f10ae04ba606902f6025036d9f83ab034bf40d6d25ecb70ebb305f2fef399779b9e9f370bebb44318670674e9707911776815e0687cbc90b9610eb02c5b7e86d35f4992e27830232e87a177966ceee23af30901f01524123903c8442cc05a314c6e6075328579f5040d9964d5f7c306d69045a392110131528fdc6305a0f3a05c716a3448822ca1df992e508120bb131ae0e822cb5c344380145482b67f8751d737f896987b5af79feff0635e900e86f0e8179059410f237aeb8d05d44751334bc93f4699633e856aa4302586bc484099785297a3a80861a38ca271e5d6c314d62d7256b78a3be6bbde8613acbdb7f2499b3d100e5e5286b25ebb92960ca74456e08f3081f7e9f3272d525771c177535d441e46a731a6e22830feb5247c16d54fbcf1d1c810ff20730c25544095197211620e43fde7a26224dc6aa55d2b3d82974ae0e79f709ad2d3d785d330b343873318a33487542855168a0694093615933eee5433d2589cb7a5e3dd9578799153e0b6ff46584289301e8638a037977e76ea3a632345a39b07a958e5c2477e8161fce531e1d189e4777bea98473c9233c3999ad397fee6bda37d274d35aafcd1d3fcce6005a203b7e308181f66840950f5317452b5ebff9531c4bc9474649cf5e26343cd96db369b106dc30b85922221d2bd9f27c013b6e9131aabb3269e4617167bf962433921fbd6c5dd95856623d5767";
		int i;

		unsigned int next = (unsigned int)OGGetAbsoluteTime();
		for( i = 0; i < sizeof(my_c0c1)-1; i++ )
		{
			//next = next * 1103515245 + 12345;
			//my_c0c1[i+1] = (next>>8)&0xff;
			unsigned char a = OrigCode[i*2+0];
			unsigned char b = OrigCode[i*2+1];
			if( a >= 'a' ) a = a - 'a' + 10; else a = a - '0';
			if( b >= 'a' ) b = b - 'a' + 10; else b = b - '0';
			my_c0c1[i+1] = (a<<4) | b;
		}
		if( send( rt->sock, my_c0c1, sizeof( my_c0c1 ), MSG_NOSIGNAL ) != sizeof( my_c0c1 ) )
		{
			fprintf( stderr, "Error, could not send c0c1 to server.\n" );
			goto closeconn;
		}

		{
			uint8_t their_c0c2[1537];
			int their_c0c2_place = 0;
			while( their_c0c2_place < sizeof( their_c0c2 ) )
			{
				int rx = recv( rt->sock, their_c0c2 + their_c0c2_place, sizeof(their_c0c2)- their_c0c2_place, MSG_NOSIGNAL );
				if( rx < 1 )
				{
					fprintf( stderr, "Error, no or invalid c0c2 reply from server.\n" );
					goto closeconn;
				}
				their_c0c2_place += rx;
			}

			if( their_c0c2[0] != 3 )
			{
				fprintf( stderr, "Error: Invalid remote c0c2\n" );
				goto closeconn;
			}

			if( send( rt->sock, their_c0c2+1, sizeof(their_c0c2)-1, MSG_NOSIGNAL ) != sizeof( their_c0c2 )- 1 )
			{
				fprintf( stderr, "Error sending their c0c2 back at them\n" );
				goto closeconn;
			}
		}

		{
			uint8_t check_c0c1[1536];
			int check_c0c1_place = 0;
			while( check_c0c1_place < sizeof( check_c0c1 ) )
			{
				int rx = recv( rt->sock, check_c0c1 + check_c0c1_place, sizeof(check_c0c1)- check_c0c1_place, MSG_NOSIGNAL );
				if( rx < 1 )
				{
					fprintf( stderr, "Error, no or invalid c0c2 reply from server.\n" );
					goto closeconn;
				}
				check_c0c1_place += rx;
			}
			if( memcmp( check_c0c1, my_c0c1+1, sizeof( check_c0c1 ) ) != 0 )
			{
				fprintf( stderr, "Error: c0c1 check bad. It seems some RTMP severs do things wrong.  Let's YOLO this.\n" );
				/*int i;
				for( i = 0; i < sizeof( check_c0c1 ); i++ )
				{
					printf( "%02x", ((uint8_t*)check_c0c1)[i] );
				}
				printf( "\n" );
				for( i = 0; i < sizeof( check_c0c1 ); i++ )
				{
					printf( "%02x", ((uint8_t*)(my_c0c1+1))[i] );
				}
				printf( "\n" );
				goto closeconn;*/
			}
		}
	}

	rt->thread_handle = OGCreateThread( RTMPListenThreadFunction, rt );

	// Established.
	rt->tmsgid = 1;

	uint8_t cmd_buffer[1536];
	int cmd_place = 0, header_place = 0;
	header_place = cmd_place; rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 2, 0x01, 0 ); //Set Chunk Size
	rtmpp_add_raw_uint32( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 65535 );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );

	if( cmd_place == sizeof( cmd_buffer ) )
	{
		fprintf( stderr, "Error: command buffer overflown on AMF0\n" );
		goto closeconn;
	}

	if( send( rt->sock, cmd_buffer, cmd_place, MSG_NOSIGNAL ) != cmd_place )
	{
		fprintf( stderr, "Error connect.\n" );
		goto closeconn;
	}

	OGUSleep(200000);

	cmd_place = 0;

	header_place = cmd_place; rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 3, 0x14, 0 ); //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "connect" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_object( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "app" );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), (app[0]=='/')?(app+1):app );
	rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "type" );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "nonprivate" );
	rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "flashVer" );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "FMLE/3.0 (compatible; FMSc/1.0)" );
	rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "swfUrl" );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), uri );
	rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "tcUrl" );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), uri );
	rtmpp_add_end_object_marker( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );

	if( cmd_place == sizeof( cmd_buffer ) )
	{
		fprintf( stderr, "Error: command buffer overflown on AMF0\n" );
		goto closeconn;
	}

	if( send( rt->sock, cmd_buffer, cmd_place, MSG_NOSIGNAL ) != cmd_place )
	{
		fprintf( stderr, "Error connect.\n" );
		goto closeconn;
	}

	OGUSleep(200000);

	// From here, we basically just ignore everything the server tells us.

	cmd_place = 0;
	header_place = cmd_place; rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x43, 0x14, 0 ); //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "releaseStream" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_raw_null( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), streamkey );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );

	if( cmd_place == sizeof( cmd_buffer ) )
	{
		fprintf( stderr, "Error: command buffer overflown on AMF0 1\n" );
		goto closeconn;
	}

	if( send( rt->sock, cmd_buffer, cmd_place, MSG_NOSIGNAL ) != cmd_place )
	{
		fprintf( stderr, "Error release stream.\n" );
		goto closeconn;
	}

	OGUSleep(200000);

	cmd_place = 0;
	header_place = cmd_place; rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x43, 0x14, 0 ); //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "FCPublish" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_raw_null( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), streamkey );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );

	if( cmd_place == sizeof( cmd_buffer ) )
	{
		fprintf( stderr, "Error: command buffer overflown on AMF0\n" );
		goto closeconn;
	}

	if( send( rt->sock, cmd_buffer, cmd_place, MSG_NOSIGNAL | MSG_MORE ) != cmd_place )
	{
		fprintf( stderr, "Error connect.\n" );
		goto closeconn;
	}

	cmd_place = 0;

	header_place = cmd_place; rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x43, 0x14, 0 ); //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "createStream" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_raw_null( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );

	if( cmd_place == sizeof( cmd_buffer ) )
	{
		fprintf( stderr, "Error: command buffer overflown on AMF0\n" );
		goto closeconn;
	}

	if( send( rt->sock, cmd_buffer, cmd_place, MSG_NOSIGNAL | MSG_MORE ) != cmd_place )
	{
		fprintf( stderr, "Error connect.\n" );
		goto closeconn;
	}


	cmd_place = 0;
	header_place = cmd_place; rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x03, 0x14, 0 ); //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "_checkbw" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_raw_null( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );


	if( cmd_place == sizeof( cmd_buffer ) )
	{
		fprintf( stderr, "Error: command buffer overflown on AMF0 2\n" );
		goto closeconn;
	}

	if( send( rt->sock, cmd_buffer, cmd_place, MSG_NOSIGNAL ) != cmd_place )
	{
		fprintf( stderr, "Error FCPublish.\n" );
		goto closeconn;
	}

	OGUSleep(200000);


	cmd_place = 0;
	header_place = cmd_place; rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x04, 0x14, 1 ); //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "publish" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_raw_null( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), streamkey );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "live" ); // This appears to always be "live" even when it's a different stream.  /// (app[0]=='/')?(app+1):app );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );


	if( cmd_place == sizeof( cmd_buffer ) )
	{
		fprintf( stderr, "Error: command buffer overflown on AMF0 3\n" );
		goto closeconn;
	}

	if( send( rt->sock, cmd_buffer, cmd_place, MSG_NOSIGNAL ) != cmd_place )
	{
		fprintf( stderr, "Error publish.\n" );
		goto closeconn;
	}
	
	//XXX TODO: Here is a place we kinda need to wait for them to give us the green light.
	OGUSleep(800000);


	cmd_place = 0;
	header_place = cmd_place; rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x04, 0x12, 1 ); //AMF0 Command for metadata.
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "@setDataFrame" );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "onMetaData" );
	rtmpp_add_raw_array( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_add_raw_uint32( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 8 ); //Array length 20 normally or 8 truncated
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "duration" );
		rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "fileSize" );
		rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "width" );
		rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 128 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "height" );
		rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 64 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "videocodecid" );
		rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "avc1" );//rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "avc1" );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "videodatarate" );
		rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 2000 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "framerate" );
		rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 30 );

#if 0
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "audiocodecid" );
		rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "mp4a" );//rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "mp4a" );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "audiodatarate" );
		rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 160 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "audiosamplerate" );
		rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 44100 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "audiosamplesize" );
		rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 16 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "audiochannels" );
		rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 2 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "stereo" );
		rtmpp_add_boolean( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 1 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "2.1" );
		rtmpp_add_boolean( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "3.1" );
		rtmpp_add_boolean( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "4.0" );
		rtmpp_add_boolean( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "4.1" );
		rtmpp_add_boolean( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "5.1" );
		rtmpp_add_boolean( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0 );
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "7.1" );
		rtmpp_add_boolean( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0 );
#endif
		rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "encoder" );
		rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "obs-output module (libobs version 25.0.3+dfsg1-2)" );
	rtmpp_add_end_object_marker( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );


	if( cmd_place == sizeof( cmd_buffer ) )
	{
		fprintf( stderr, "Error: command buffer overflown on AMF0 3\n" );
		goto closeconn;
	}

	if( send( rt->sock, cmd_buffer, cmd_place, MSG_NOSIGNAL ) != cmd_place )
	{
		fprintf( stderr, "Error @setDataFrame onMetadata.\n" );
		goto closeconn;
	}

	rt->nalbuffer = malloc( RTMP_SEND_BUFFER + 34);
	rt->nalbuffer[0] = 0x04; // Chunck stream ID
	rt->nalbuffer[1] = 0x00;
	rt->nalbuffer[2] = 0x00;
	rt->nalbuffer[3] = 0x00;
	rt->nallen = NAL_START;

	OGUSleep(100000);
	printf( "OK\n" );

/*
	cmd_place = 0;

	//rtmp-stream.c `send_video_header(...)`
	//static void load_headers(struct obs_x264 *obsx264)
	// NOTE: This outputs NAL_SPS, NAL_PPS and NAL_SEI
	

*/


#if 0
	OGUSleep(500000);

	const char * audiopacket = "040000000000070801000000af00121056e500";
	plen = strlen(audiopacket)/2;
	for( i = 0; i < plen; i++ )
	{
		unsigned char a = audiopacket[i*2+0];
		unsigned char b = audiopacket[i*2+1];
		if( a >= 'a' ) a = a - 'a' + 10; else a = a - '0';
		if( b >= 'a' ) b = b - 'a' + 10; else b = b - '0';
		buffer[i] = (a<<4) | b;
	}
	if( send( rt->sock, buffer, plen, MSG_NOSIGNAL ) != plen )
	{
		fprintf( stderr, "Error, could not send dummy buffer.\n" );
		goto closeconn;
	}
#endif

#if 0
	OGUSleep(500000);
	char buffer[2048];
	int plen, i;

/*
av.PRE gortsplib.NewTrackH264
 SPS -> [103 100 0 11 172 217 67 15 251 132 0 0 3 0 4 0 0 3 0 242 60 80 166 88]
 PPS -> [104 239 188 176]
gortsplib.NewTrackH264 ->  <nil>
*/

								// 010000530000002c // 419a[2418] // 873f175481148f6ba2cec3fe346f46c39582463af138243fd3c421970e6dd400000b9745044ea8f0
								// 000000000164000b // ffe1[0018] // 6764000bacd9430ffb84000003000400000300f23c50a658 // 01[0004] // 68efbcb0
	const char * videopacket = "0400000000002c090100000017000000000164000bffe100186764000bacd9430ffb84000003000400000300f23c50a65801000468efbcb0";
							  //0400000000002c090100000017000000000164000bffe100186764000bacd9430ffb84000003000400000300f23c50a65801000468efbcb0" //192?x108?
                              //0400000000002d090100000017000000000164001effe100196764001eacd94080107ba840000003004000000f23c58b658001000468efbcb0 //511x511
                              //0400000000002d090100000017000000000164001effe100196764001eacd94080107ba840000003004000000f23c58b658001000468efbcb0 //511x511 again
                              //0400000000002d090100000017000000000164001fffe100196764001facd94080107ba840000003004000001e23c60c658001000468efbcb0 //511x511 @ 60
                              //0400000000002c090100000017000000000164001effe100186764001eacd94080106840000003004000000f23c58b658001000468efbcb0" //512X512
                              //0400000000002e0901000000170000000001640028ffe1001a67640028acd940780227e584000003000400000300f23c60c65801000468efbcb0 //1920x1080
	plen = strlen(videopacket)/2;
	for( i = 0; i < plen; i++ )
	{
		unsigned char a = videopacket[i*2+0];
		unsigned char b = videopacket[i*2+1];
		if( a >= 'a' ) a = a - 'a' + 10; else a = a - '0';
		if( b >= 'a' ) b = b - 'a' + 10; else b = b - '0';
		buffer[i] = (a<<4) | b;
	}
	if( send( rt->sock, buffer, plen, MSG_NOSIGNAL ) != plen )
	{
		fprintf( stderr, "Error, could not send dummy buffer.\n" );
		goto closeconn;
	}
#endif


	return 0;
closeconn:
	close( rt->sock );
	OGJoinThread( rt->thread_handle );
	rt->sock = 0;	
	return -6;
}

void RTMPClose( struct RTMPSession * rt )
{
	if( rt->sock )
	{
		close( rt->sock );
	}
	if( rt->thread_handle )
	{
		OGJoinThread( rt->thread_handle );
	}
}

int RTMPSend( struct RTMPSession * rt, uint8_t * buffer, int len )
{
	double dNow = OGGetAbsoluteTime();
/*
	static FILE * flog;
	if( !flog ) flog = fopen( "testout.h264", "wb" );
	if( len == -1 )
	{
		fwrite( "\x00\x00\x00\x01", 4, 1, flog );
	}
	else if( len > 0 )
	{
		fwrite( buffer, len, 1, flog );
	}
*/
/*
	if( len == 1 )
	{
		printf( "EMIT: %02x\n", buffer[0] );
	}
	else
	{
		printf( "RTMP SEND %p %d\n", buffer, len );
	}*/


	if( len == -1 )
	{
		if( rt->nallen+5 >= RTMP_SEND_BUFFER+13 )
		{
			fprintf( stderr, "Error: overflow on rtmp sending\n" );
			return -1;
		}
/*
		rt->nalbuffer[rt->nallen++] = 0x00;
		rt->nalbuffer[rt->nallen++] = 0x00;
		rt->nalbuffer[rt->nallen++] = 0x00;
		rt->nalbuffer[rt->nallen++] = 0x01;
*/
		//len = -2;
	}

//temp notes
// Our output / theirs
//000000000000001effe10000674200298de187980a2400000fa00001d4c024850be068ce3a80
//000000000164001fffe100196764001facd94080107ba840000003004000001e23c60c658001000468efbcb0

	// For this section, it goes 
	// -5 = /* SPS */
	// -4 = /* PPS */
	// -3 => Transmit
	if( len == -5 )
	{
		//OBS:
		//04000000[000036]09/01000000  @12
		//17000000000164000dffe1 @22
		//   00226764000dac2ca504021b016a04040a800001f40000753070000186a000c353779705
		//    01
		//   000468eb8f2c

		//
		//04000000[00002b]09/01000000
		//17000000000164000d01
		// 01 0017 674200298de0804260289000003e800007530092142f80
		// 01 0004 68ce3a80

		// With exp
		//0400000000002a0901000000
		//17000000000164 (prfile idc)/00profile-compat 0d level-idc
		//010017674200298de0804260289000003e800007530092142f80
		//01000468ce3a80

		//ME (new):
		//04000000[00002b]0901/000000
		//170000000001640022ffe1
		//  0017674200298de0804260289000003e800007530092142f80
		//  01000468ce3a80

		// 17 00   data[1...]   cfgVer, profileIdc, profileCompat, levelIdc
		// 17 00    000000         01        64          00           0d       ffe1

		//ME (old):
		//04000000[00002b]09/01000000  @12
		//170000000000000022ffe1 @
		//   0017674200298de0804260289000003e800007530092142f80
		//    01
		//   000468ce3a80

		// NEW NEW 
		//04000000[00002b]09/01000000
		//17000000000164000d0101
		//   0017674200298de0804260289000003e800007530092142f80
		// 01000468ce3a80

		//04000000[00002b]09/01000000
		//17000000000164000d0101
		//	0017674200298de0804260289000003e800007530092142f8001000468ce3a80


		//OBS (Copy)
		//04000000[000036]09/01000000  @12
		//17000000000164000dffe1 @22
		//   00226764000dac2ca504021b016a04040a800001f40000753070000186a000c353779705
		//    01
		//   000468eb8f2c


		//https://blog.kundansingh.com/2012/01/translating-h264-between-flash-player.html

		printf( "NALLLEN -5: %d\n", rt->nallen );

//		int i;
//		for( i = 0; i < rt->nallen; i++ )
//			printf( "%02x ", rt->nalbuffer[i] );
		rt->nalbuffer[rt->nallen++] = 0xff;  // length-flag  (was 0xff) should be 0x01
			//WARNING Setting the above to 0x01 from 0xff causes NAL errors all over the place
		rt->nalbuffer[rt->nallen++] = 0xe1;  // sps-count    (was 0xe1) should be 0x01
		rt->bookmarksize = rt->nallen;
		rt->nalbuffer[rt->nallen++] = 0x00; // will become size of immediate.
		rt->nalbuffer[rt->nallen++] = 0x00; // will become size of immediate.
								// 010000530000002c // 419a[2418] // 873f175481148f6ba2cec3fe346f46c39582463af138243fd3c421970e6dd400000b9745044ea8f0
								// 000000000164000b // ffe1[0018] // 6764000bacd9430ffb84000003000400000300f23c50a658 // 01[0004] // 68efbcb0
	}
	if( len == -4 )
	{
		printf( "NALLLEN -4: %d\n", rt->nallen );

		int i;
		for( i = 0; i < rt->nallen; i++ )
			printf( "%02x ", rt->nalbuffer[i] );

		rt->nalbuffer[rt->bookmarksize+0] = (rt->nallen - rt->bookmarksize - 2)>>8;
		rt->nalbuffer[rt->bookmarksize+1] = (rt->nallen - rt->bookmarksize - 2)>>0;
		rt->nalbuffer[rt->nallen++] = 0x01;
		rt->bookmarksize = rt->nallen;
		rt->nalbuffer[rt->nallen++] = 0x00; // will become size of immediate.
		rt->nalbuffer[rt->nallen++] = 0x00; // will become size of immediate.
	}

	if( len == -2 || len == -3 || len == -6 )
	{
		if( rt->nallen > NAL_START )
		{
			int nallen = rt->nallen;
			uint8_t * nb = rt->nalbuffer;
			//memset( nb + nallen, 0, 4 );
			//nallen+=4;

			//Round up
			//nallen = (nallen+3) & 0xffffffc;
			int nalrep = nallen-12; //Was 9 //+1 = for the data type code.


			static uint32_t whichtsa = 0;
			static int nrframes = 0;
			whichtsa=dNow*999;
			nrframes++;
			
#if 1
			int tsa = 0;
			// This is TOTALLY BOGUS, but tricks things into delivering things ASAP.
			if( whichtsa == 1) tsa = 0x00;
			if( whichtsa == 2) tsa = 0x10;
			if( whichtsa == 3) tsa = 0x53;
			if( whichtsa == 4) { tsa = 0x21; whichtsa = 0; }
#endif

			// See flv-mux.c `flv_video(...)`
			nb[0] = 0x04;
			nb[1] = 0>>16;
			nb[2] = 0>>8;
			nb[3] = whichtsa; // timestamp (fudged to keep things urgent!)
			//printf( "%d\n", whichtsa&0xff );
			nb[4] = nalrep>>16;
			nb[5] = nalrep>>8;
			nb[6] = nalrep>>0;
			nb[7] = 0x09; // Video data
			nb[8] = 1; //Stream ID
			nb[9] = 0;
			nb[10] = 0;
			nb[11] = 0;

				//0x17 = I-frame
				//0x27 = B- or P-frame
			nb[12] = (len == -6)?0x27:0x17; //Data type code.

			int encaplen = nallen-21;
			//0: is_header  (configuration data)
			//1: !is_header (video data)
			nb[13] = (len == -2 || len == -6)?1:0; 

			nb[14] = 0>>16; //Timestamp 
			nb[15] = 0>>8; //Timestamp 
			
			//TRICKY: We "trick" the VRCDN servers to think our stream is online, then
			// Once they know no better, we just set all timestamps to 0 so it just 
			// sends the packets out without buffering.
			if( nrframes < 100 )
				nb[16] = whichtsa>>2; //Timestamp 
			else
				nb[16] = 0;
			nb[17] = 0;
			nb[18] = encaplen>>16;
			nb[19] = encaplen>>8;
			nb[20] = encaplen>>0;

			if( len == -3 )
			{
				nb[17] = 1;  //Version
				nb[18] = 0x64; //Profile (IDC) (was 64), now 42 (baseline)
				nb[19] = 0x00; //Profile (compat) (was 0x00)
				nb[20] = 0x0d; //Profile (level-icd) (was 0x0d)
				rt->nalbuffer[rt->bookmarksize+0] = (rt->nallen - rt->bookmarksize - 2)>>8;
				rt->nalbuffer[rt->bookmarksize+1] = (rt->nallen - rt->bookmarksize - 2)>>0;
				//printf( "NALLEN -3: %d - %d - 2 = %d @ %d\n", rt->nallen, rt->bookmarksize, rt->nallen - rt->bookmarksize - 2, rt->bookmarksize );

				// Ram-jam in a copy from OBS.
			//	uint8_t * nbo = nb;
			//	const char nbB [] = { 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x09, 0x01, 0x00, 0x00, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x0d, 0xff, 0xe1, 0x00, 0x22, 0x67, 0x64, 0x00, 0x0d, 0xac, 0x2c, 0xa5, 0x04, 0x02, 0x1b, 0x01, 0x6a, 0x04, 0x04, 0x0a, 0x80, 0x00, 0x01, 0xf4, 0x00, 0x00, 0x75, 0x30, 0x70, 0x00, 0x01, 0x86, 0xa0, 0x00, 0xc3, 0x53, 0x77, 0x97, 0x05, 0x01, 0x00, 0x04, 0x68, 0xeb, 0x8f, 0x2c };
			//	memcpy( nbo, nbB, sizeof( nbB ) );
			//	nallen = sizeof( nbB );
			}



			int ts = 0;
			int tosend = nallen + ts;
/*
			int i;
			for( i = 0; i < tosend; i++ )
				printf( "%02x", nb[i] );
	*/		
			int ret = send( rt->sock, nb, tosend, MSG_NOSIGNAL );
			printf( "Sending total len: %d / nallen %d => %d\n", nallen+ts, nallen, ret );
			if( ret != tosend )
			{
				rt->nallen = 0;
				fprintf( stderr, "Fault sending [%d != %d]\n", ret, tosend );
				return -1;
			}

			rt->nallen = NAL_START;
		}
	}

	if( len > 0 )
	{
		int nallen = rt->nallen;
		uint8_t * nb = rt->nalbuffer;
		if( nallen + len >= RTMP_SEND_BUFFER+13 )
		{
			fprintf( stderr, "Error: overflow on rtmp sending (data)\n" );
			return -1;
		}
		memcpy( nb + nallen, buffer, len );
		rt->nallen = nallen + len;
	}
	return 0;
}


