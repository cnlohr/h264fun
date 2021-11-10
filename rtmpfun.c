#include "rtmpfun.h"
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


static int rtmpp_finish_header( uint8_t * buffer, int * place, int maxlen, int post_header_place )
{
	int pl = *place;
	if( pl == maxlen ) return -1;
	int plen = pl - post_header_place;
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

static int rtmpp_add_raw_null( uint8_t * buffer, int * place, int maxlen )
{
	int pl = *place;
	if( pl + 1 > maxlen ) { *place = maxlen; return -1; }
	buffer[pl++] = 5;
	*place = pl;
	return 0;
}

static int rtmpp_add_header( uint8_t * buffer, int * place, int maxlen, int type_id )
{
	int pl = *place;
	if( pl + 12 > maxlen ) { *place = maxlen; return -1; }
	static const uint8_t bufferbase[] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
	memcpy( buffer+pl, bufferbase, sizeof( bufferbase ) );
	buffer[pl+7] = type_id;
	*place = pl + sizeof(bufferbase);
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
	memcpy( buffer+pl, &d, 8 );
	*place = pl + 8;
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


// Function returns upon successful connection or failure.
int InitRTMPConnection( struct RTMPSession * rt, void * opaque, const char * uri, const char * streamkey )
{
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
		server[slen] = 0;
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
		bzero(&(rmt_addr.sin_zero), 8);

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
		int i;

		unsigned int next = (unsigned int)OGGetAbsoluteTime();
		for( i = 0; i < 1536; i++ )
		{
			next = next * 1103515245 + 12345;
			my_c0c1[i+1] = (next>>8)&0xff;
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
				fprintf( stderr, "Error: c0c1 check bad.\n" );
				goto closeconn;
			}
		}
	}

	// Established.
	rt->tmsgid = 1;

	uint8_t cmd_buffer[1536];
	int cmd_place = 0, header_place = 0;
	rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x01 ); header_place = cmd_place; //Set Chunk Size
	rtmpp_add_raw_uint32( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 4096 );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );

	rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x14 ); header_place = cmd_place; //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "connect" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_object( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "app" );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), app );
	rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "type" );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "nonprivate" );
	rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "flashVer" );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "FMLE/3.0 (compatible; FMSc/1.0)" );
	rtmpp_add_property( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "swfUrl" );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), uri );
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
		fprintf( stderr, "Error setting chunk size.\n" );
		goto closeconn;
	}

	// From here, we basically just ignore everything the server tells us.

	cmd_place = 0;
	rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x14 ); header_place = cmd_place; //AMF0 Command
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
		fprintf( stderr, "Error setting chunk size.\n" );
		goto closeconn;
	}

	rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x14 ); header_place = cmd_place; //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "FCPublish" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_raw_null( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), streamkey );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );

	rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x14 ); header_place = cmd_place; //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "createStream" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_raw_null( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), streamkey );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );

	rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x14 ); header_place = cmd_place; //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "_checkbw" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_raw_null( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), streamkey );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );


	if( cmd_place == sizeof( cmd_buffer ) )
	{
		fprintf( stderr, "Error: command buffer overflown on AMF0 2\n" );
		goto closeconn;
	}

	if( send( rt->sock, cmd_buffer, cmd_place, MSG_NOSIGNAL ) != cmd_place )
	{
		fprintf( stderr, "Error setting chunk size.\n" );
		goto closeconn;
	}

	rtmpp_add_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), 0x14 ); header_place = cmd_place; //AMF0 Command
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), "publish" );
	rtmpp_add_number( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), rt->tmsgid++ );
	rtmpp_add_raw_null( cmd_buffer, &cmd_place, sizeof( cmd_buffer ) );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), streamkey );
	rtmpp_add_string( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), app );
	rtmpp_finish_header( cmd_buffer, &cmd_place, sizeof( cmd_buffer ), header_place );


	if( cmd_place == sizeof( cmd_buffer ) )
	{
		fprintf( stderr, "Error: command buffer overflown on AMF0 3\n" );
		goto closeconn;
	}

	if( send( rt->sock, cmd_buffer, cmd_place, MSG_NOSIGNAL ) != cmd_place )
	{
		fprintf( stderr, "Error setting chunk size.\n" );
		goto closeconn;
	}
	
	rt->nalbuffer = malloc( RTMP_SEND_BUFFER + 13);
	rt->nalbuffer[0] = 0x04;
	rt->nalbuffer[1] = 0x00;
	rt->nalbuffer[2] = 0x00;
	rt->nalbuffer[3] = 0x00;
	rt->nallen = 0;

	return 0;
closeconn:
	close( rt->sock );
	rt->sock = 0;	
	return -6;
}

void RTMPClose( struct RTMPSession * rt )
{
	if( rt->sock )
	{
		close( rt->sock );
	}
}

int RTMPSend( struct RTMPSession * rt, uint8_t * buffer, int len )
{
	if( len == -1 )
	{
		uint8_t * nb = rt->nalbuffer;
		int nallen = rt->nallen;
		if( nallen+4 >= RTMP_SEND_BUFFER+13 )
		{
			fprintf( stderr, "Error: overflow on rtmp sending\n" );
			return -1;
		}
		nb[nallen++] = 0;
		nb[nallen++] = 0;
		nb[nallen++] = 0;
		nb[nallen++] = 1;
		rt->nallen = nallen;
	}
	else if( len == -2 )
	{
		int nallen = rt->nallen;
		uint8_t * nb = rt->nalbuffer;
		nb[4] = nallen>>16;
		nb[5] = nallen>>8;
		nb[6] = nallen>>0;
		nb[7] = 0x09; // Video data
		nb[8] = 1; //Stream ID
		nb[9] = 0;
		nb[10] = 0;
		nb[11] = 0;
		nb[12] = 0x17; //h.264...stuff?

		int ret = send( rt->sock, nb, nallen+13, MSG_NOSIGNAL );
		if( ret != nallen+13 )
		{
			rt->nallen = 0;
			fprintf( stderr, "Fault sending [%d != %d]\n", ret, nallen+13 );
			return -1;
		}
		rt->nallen = 0;
	}
	else
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

