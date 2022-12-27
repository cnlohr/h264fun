/*
    H264Fun - create H264 data streams that are lossless, and selectively
		updatable.  
		
		Note: Only luma is lossless.  There is 420 colorspace compression.
		Note: The deblocking filter is turned off.
*/

#ifndef _H264FUN_H
#define _H264FUN_H

#include <stdint.h>

#ifndef H264FUNPREFIX
#define H264FUNPREFIX
#endif

// Tested with 16.  May operate much higher.
#define MAX_H264FUNZIE_SLICES 16


enum H264FunPayload_t
{
	H264FUN_PAYLOAD_NONE = 0,
	H264FUN_PAYLOAD_EMPTY, // Not implemented in a sane way.  Recommended not to use.  TODO: Someone, some day should fix this, and make it able to CABAC or CALVC encode a solid color or something.
	H264FUN_PAYLOAD_LUMA_ONLY,
	H264FUN_PAYLOAD_LUMA_ONLY_COPY_ON_SUBMIT,
	H264FUN_PAYLOAD_LUMA_ONLY_DO_NOT_FREE, // Same as above, but free() is not called on macroblock data.
	H264FUN_PAYLOAD_LUMA_AND_CHROMA,
	H264FUN_PAYLOAD_LUMA_AND_CHROMA_COPY_ON_SUBMIT,
	H264FUN_PAYLOAD_LUMA_AND_CHROMA_DO_NOT_FREE, // Same as above, but free() is not called on macroblock data.
};

typedef enum H264FunPayload_t H264FunPayload;

struct H264Funzie_t;

// If bytes == -1, then, it indicates a new NAL -> YOU as the USER must:
//     emit the 00 00 00 01 (or other header) (NAL/H264)
//       or
//     emit the number of bytes to follow (MP4)
// If bytes == -2, then that indicates a flush.
typedef void (*H264FunData)( void * opaque, uint8_t * data, int bytes );

struct H264FunzieUpdate_t
{
	H264FunPayload pl;
	uint8_t * data;
};

typedef struct H264FunzieUpdate_t H264FunzieUpdate;
struct H264ConfigParam_t;

struct H264Funzie_t
{
	uint8_t bytesofar, bitssofarm7; // defaults to 7.
	uint16_t slices;
	uint16_t w, h;
	uint8_t cnt_type;
	uint16_t mbw, mbh; //macroblock w, macroblock h.
	int frameno;

	H264FunData datacb;
	void * opaque;
	
	struct H264ConfigParam_t * params;

	// Area for collecting macroblocks that need updating this frame.
	H264FunzieUpdate * frameupdates;
};

typedef enum H264FunConfigType_t
{
	H2FUN_TERMINATOR = 0,
	H2FUN_TIME_ENABLE,
	H2FUN_TIME_NUMERATOR,   // 1000 default
	H2FUN_TIME_DENOMINATOR, // 30000 default
	H2FUN_CNT_TYPE,         // 0 default (only 0 and 2 supported)
	H2FUN_MAX
} H264FunConfigType;

typedef struct H264ConfigParam_t
{
	H264FunConfigType type;
	int value;
} H264ConfigParam;

typedef struct H264Funzie_t H264Funzie;

// Initialize a H264Funzie.
//  w and h must be divisble by 16.
//  slices probably shouldn't be greater than MAX_H264FUNZIE_SLICES.
int H264FUNPREFIX H264FunInit( H264Funzie * funzie, int w, int h, int slices, H264FunData datacb, void * opaque, const H264ConfigParam * params );

void H264FUNPREFIX H264FunAddMB( H264Funzie * funzie, int x, int y, uint8_t * data, H264FunPayload pl );

void H264FUNPREFIX H264SendSPSPPS( H264Funzie * funzie, int emissionmode );

void H264FUNPREFIX H264FakeIFrame( H264Funzie * funzie );

// Emit the frame.  Note: the return value will be 0 if successful, otherwise will fail with error code returned by H264FunData.
int H264FUNPREFIX H264FunEmitFrame( H264Funzie * funzie );

// Emit the frame, but as an IFrame
int H264FUNPREFIX H264FunEmitIFrame( H264Funzie * funzie );

void H264FUNPREFIX H264FunClose( H264Funzie * funzie );

// Actually not useful, will need another way.  TODO: read phos's message about stream setting mp4's.
H264FUNPREFIX extern const uint8_t h264fun_mp4header[48];


#ifdef _H264FUN_H_IMPL
#include "h264fun.c"
#endif

#endif
