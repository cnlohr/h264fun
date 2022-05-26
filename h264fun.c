#include "h264fun.h"
#include <string.h>
#include <stdlib.h>

static uint8_t H2mb_dark[384];


// Sometimes written u(#)
static void H2EmitU( H264Funzie * fun, uint32_t data, int bits )
{
	for( bits--; bits >= 0 ; bits-- )
	{
		uint8_t b = ( data >> bits ) & 1;
		fun->bytesofar |= b << fun->bitssofarm7;
		if( fun->bitssofarm7-- == 0 )
		{
			fun->datacb( fun->opaque, &fun->bytesofar, 1 );
			fun->bytesofar = 0;
			fun->bitssofarm7 = 7;
		}
	}
}

static int H2EmitFlush( H264Funzie * fun )
{
	if( fun->bitssofarm7 != 7 )
	{
		fun->datacb( fun->opaque, &fun->bytesofar, 1 );
		fun->bytesofar = 0;
		fun->bitssofarm7 = 7;
	}
}

// https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn
// https://stackoverflow.com/questions/21888140/de-bruijn-algorithm-binary-digit-count-64bits-c-sharp

static int H264FUNDeBruijnLog2( uint64_t v )
{
	// Note - if you are on a system with MSR or can compile to assembly, that is faster than this.
	// Otherwise, for normal C, this seems like a spicy way to roll.

	// Round v up!
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;

	static const int MultiplyDeBruijnBitPosition2[128] = 
	{
		0, // change to 1 if you want bitSize(0) = 1
		48, -1, -1, 31, -1, 15, 51, -1, 63, 5, -1, -1, -1, 19, -1, 
		23, 28, -1, -1, -1, 40, 36, 46, -1, 13, -1, -1, -1, 34, -1, 58,
		-1, 60, 2, 43, 55, -1, -1, -1, 50, 62, 4, -1, 18, 27, -1, 39, 
		45, -1, -1, 33, 57, -1, 1, 54, -1, 49, -1, 17, -1, -1, 32, -1,
		53, -1, 16, -1, -1, 52, -1, -1, -1, 64, 6, 7, 8, -1, 9, -1, 
		-1, -1, 20, 10, -1, -1, 24, -1, 29, -1, -1, 21, -1, 11, -1, -1,
		41, -1, 25, 37, -1, 47, -1, 30, 14, -1, -1, -1, -1, 22, -1, -1,
		35, 12, -1, -1, -1, 59, 42, -1, -1, 61, 3, 26, 38, 44, -1, 56
	};
	return MultiplyDeBruijnBitPosition2[(uint64_t)(v * 0x6c04f118e9966f6bUL) >> 57];
}

//For signed numbers.
static void H2EmitSE( H264Funzie * fun, int64_t data )
{
	if( data == 0 )
	{
		H2EmitU( fun, 1, 1 );
		return;
	}
	uint64_t enc;
	if( data < 0 )
		enc = ((-data) << 1 ) | 1;
	else
		enc = data << 1;

	int numbits = H264FUNDeBruijnLog2( enc );
	H2EmitU( fun, 0, numbits-1 );
	H2EmitU( fun, enc, numbits );
}

// Sometimes written ue(v)
static void H2EmitUE( H264Funzie * fun, int64_t data )
{
	if( data == 0 )
	{
		H2EmitU( fun, 1, 1 );
		return;
	}
	data++;
	int numbits = H264FUNDeBruijnLog2( data );
	H2EmitU( fun,0, numbits-1 );
	H2EmitU( fun, data, numbits );
}

static void H2EmitNAL( H264Funzie * fun )
{
	H2EmitFlush( fun );
	fun->datacb( fun->opaque, 0, -1 );
}

static void FreeFunzieUpdate( struct H264FunzieUpdate_t * thisupdate )
{
	switch( thisupdate->pl )
	{
	case H264FUN_PAYLOAD_LUMA_ONLY:
	case H264FUN_PAYLOAD_LUMA_AND_CHROMA:
		free( thisupdate->data );
		break;
	default:
		break;
	}
	thisupdate->pl = H264FUN_PAYLOAD_NONE;
}

#define BuildNALU( ref_idc, unit_type ) ( ( (ref_idc ) << 5 ) | ( unit_type ) )


static int H2GetParamValue( const H264ConfigParam * params, H264FunConfigType p )
{
	if( p >= H2FUN_MAX ) return 0;
	static const int defaults[] = { 0, 1, 1000, 30000, 0 }; 
	int i;
	if( params )
	{
		H264FunConfigType t;
		for( i = 0; (t = params[i].type) != H2FUN_TERMINATOR; i++ )
		{
			if( t == p )
			{
				return params[i].value;
			}
		}
	}
	return defaults[p];
}

void H264FUNPREFIX H264SendSPSPPS( H264Funzie * fun )
{
	// Generate stream.
	H2EmitNAL( fun );
	fun->datacb( fun->opaque, 0, -5 );

	fun->cnt_type = H2GetParamValue( fun->params, H2FUN_CNT_TYPE );

	//seq_parameter_set_rbsp()
	H2EmitU( fun, BuildNALU( 3, 7 ), 8 ); //NALU "7 = sequence parameter set" or SPS
	H2EmitU( fun, 66, 8 ); // Baseline Profile  (WAS ORIGINALLY 66)  // profile_idc 
	H2EmitU( fun, 0, 1 );  // We're not going to honor constraints. constraint_set0_flag? (We are 66 compliant) TODO: REVISIT
	H2EmitU( fun, 0, 1 );  // We're not going to honor constraints. constraint_set1_flag?  XXX TODO: Without this we can't have multiple groupled slices.
	H2EmitU( fun, 0, 1 );  // We're not going to honor constraints. constraint_set2_flag?
	H2EmitU( fun, 0, 1 );  // We're not going to honor constraints. / reserved
	H2EmitU( fun, 0, 4 );  // Reserved 
	H2EmitU( fun, 41, 8 ); // level_idc = 41 (4.1)       Level 1, sec A.3.1.   See "Table A-1 – Level limits"
		//Conformance to a particular level shall be specified by setting the syntax element level_idc equal to a value of ten times
		// the level number specified in Table A-1.
	H2EmitUE( fun, 0 );    // seq_parameter_set_id = 0

	H2EmitUE( fun, 12 );   // log2_max_frame_num_minus4  (16-bit frame numbers)
	H2EmitUE( fun, fun->cnt_type );    // pic_order_cnt_type
	if( fun->cnt_type == 0 )
		H2EmitUE( fun, 0 );    // log2_max_pic_order_cnt_lsb_minus4
	H2EmitUE( fun, 0 );    // num_ref_frames (we only send I slices) (I think this is right)
	H2EmitU( fun, 0, 1 );	// gaps_in_frame_num_value_allowed_flag  ( =0 says we can't skip frames.)
	H2EmitUE( fun, fun->mbw-1 ); // pic_width_in_mbs_minus_1.  (x16 px)
	H2EmitUE( fun, fun->mbh-1 ); // pic_height_in_map_units_minus_1.   (x16 px)
	H2EmitU( fun, 1, 1 ); // frame_mbs_only_flag = 1 //We will not to field/frame encoding.
	H2EmitU( fun, 0, 1 ); // direct_8x8_inference_flag = 0 // Used for B slices. We will not send B slices.
	H2EmitU( fun, 0, 1 ); // frame_cropping_flag = 0
	H2EmitU( fun, 1, 1 ); // vui_parameters_present_flag = 1
		//vui_parameters()
		H2EmitU( fun, 1, 1 ); // aspect_ratio_info_present_flag = 1
			H2EmitU( fun, 1, 8 ); // 1:1 Square
		H2EmitU( fun, 0, 1 ); // overscan_info_present_flag = 0
		H2EmitU( fun, 1, 1 ); // video_signal_type_present_flag = 1
			H2EmitU( fun, 0, 3 ); //video_format
			H2EmitU( fun, 1, 1 ); //video_full_range_flag
			H2EmitU( fun, 0, 1 ); //colour_description_present_flag = 0
		H2EmitU( fun, 0, 1 ); // chroma_loc_info_present_flag = 0

		int e = H2GetParamValue( fun->params, H2FUN_TIME_ENABLE );
		H2EmitU( fun, e, 1 ); //timing_info_present_flag = 1
		if( e )
		{
			H2EmitU( fun, H2GetParamValue( fun->params, H2FUN_TIME_NUMERATOR ), 32 ); // num_units_in_tick = 1
			H2EmitU( fun, H2GetParamValue( fun->params, H2FUN_TIME_DENOMINATOR ), 32 ); // time_scale = 50
			H2EmitU( fun, 0, 1 ); // fixed_frame_rate_flag = 0
		}
		H2EmitU( fun, 0, 1 ); // nal_hrd_parameters_present_flag = 0
		H2EmitU( fun, 0, 1 ); // vcl_hrd_parameters_present_flag = 0
		H2EmitU( fun, 0, 1 ); // pic_struct_present_flag = 0
		H2EmitU( fun, 1, 1 ); // bitstream_restriction_flag = 1
			H2EmitU( fun, 0, 1 ); // No motion vectors over pic boundaries
			H2EmitUE( fun, 1 ); //max_bytes_per_pic_denom
			H2EmitUE( fun, 1 ); //max_bits_per_mb_denom // 1 should be sufficient for all PCM's but Android still no like it.
			H2EmitUE( fun, 9 ); //log2_max_mv_length_horizontal
			H2EmitUE( fun, 10 ); //log2_max_mv_length_vertical
			H2EmitUE( fun, 0 ); //num_reorder_frames
			H2EmitUE( fun, 0 ); //max_dec_frame_buffering (?)

	H2EmitU( fun, 1, 1 ); // Stop bit from rbsp_trailing_bits()
	H2EmitFlush( fun );
	fun->datacb( fun->opaque, 0, -4 );

	//pps (need to be ID 0)
	// 00 00 00 01 68 // EB E3 CB 22 C0 (OLD)

	// "PPS"
	H2EmitNAL( fun );
	H2EmitU( fun, BuildNALU( 3, 8 ), 8 ); // nal_unit_type = 8 = pic_parameter_set_rbsp( )
	H2EmitUE( fun, 0 ); // pic_parameter_set_id  (Do we need more, so we can define other PPS types to encode with?)
	H2EmitUE( fun, 0 ); // seq_parameter_set_id
	H2EmitU( fun, 0, 1 ); //entropy_coding_mode_flag (OFF, LEFT COLUMN)
	H2EmitU( fun, 0, 1 ); //pic_order_present_flag
	H2EmitUE( fun, 0 ); //num_slice_groups_minus1
	H2EmitUE( fun, 0 ); //num_ref_idx_l0_active_minus1 >>> THIS IS SUSPICIOUS (WAS 2)
	H2EmitUE( fun, 0 ); //num_ref_idx_l1_active_minus1
	H2EmitU( fun, 0, 1 ); // weighted_pred_flag (was 1)
	H2EmitU( fun, 0, 2 ); // weighted_bipred_idc (Was 2)
	H2EmitSE( fun, 0 ); //pic_init_qp_minus26 (was -3)
	H2EmitSE( fun, 0 ); //pic_init_qs_minus26
	H2EmitSE( fun, 0 ); //chroma_qp_index_offset (was -2 )
	H2EmitU( fun, 0, 1 ); //deblocking_filter_control_present_flag
	H2EmitU( fun, 1, 1 ); //constrained_intra_pred_flag=1 don't use predictive coding everywhere.
	H2EmitU( fun, 0, 1 ); //redundant_pic_cnt_present_flag = 0
	H2EmitU( fun, 1, 1 ); // Stop bit from rbsp_trailing_bits()
	H2EmitFlush( fun );
	fun->datacb( fun->opaque, 0, -3 );
}


int H264FUNPREFIX H264FunInit( H264Funzie * fun, int w, int h, int slices, H264FunData datacb, void * opaque, const H264ConfigParam * params )
{
	if( slices > MAX_H264FUNZIE_SLICES ) return -3;

	// A completely dark frame.
	memset( H2mb_dark, 128, 256 );
	memset( H2mb_dark+256, 128, 128 );
	
	if( ( w & 0xf ) || ( h & 0xf ) ) return -1;
	fun->bytesofar = 0;
	fun->bitssofarm7 = 7;
	fun->w = w;
	fun->h = h;
	fun->slices = slices;
	fun->datacb = datacb;
	fun->frameno = 0;
	fun->opaque = opaque;
	int mbw = fun->mbw = w>>4;
	int mbh = fun->mbh = h>>4;
	fun->frameupdates = calloc( sizeof( H264FunzieUpdate ), mbw * mbh );

	H264FunConfigType t;

	int cfgsize = 0;
	for( ; params[cfgsize].type != H2FUN_TERMINATOR; cfgsize++ ); cfgsize++;

	fun->params = malloc( cfgsize * sizeof( const H264ConfigParam ) );
	memcpy( fun->params, params, cfgsize * sizeof( const H264ConfigParam ) );
	
	H264SendSPSPPS( fun );
	
	int slice = 0;
	for( slice = 0; slice < slices; slice++ )
	{
		//slice_layer_without_partitioning_rbsp()
		H2EmitNAL( fun );
		int slicestride = mbw*mbh/slices;

		//NALU "5 = coded slice of an IDR picture"   nal_ref_idc = 3, nal_unit_type = 5 
		// IDR = A coded picture containing only slices with I or SI slice types
		H2EmitU( fun, BuildNALU( 3, 5 ), 8 ); 

		// slice_header();
		H2EmitUE( fun, slice*slicestride );    //first_mb_in_slice 0 = new frame.
		H2EmitUE( fun, 7 );    //I-slice only. (slice_type == 7 (I slice))
		H2EmitUE( fun, 0 );    //pic_parameter_set_id = 0 (referencing pps 0)
		H2EmitU( fun, fun->frameno, 16 );	//frame_num
		H2EmitUE( fun, 0 ); // idr_pic_id
		if( fun->cnt_type == 0 )
		{
			//pic_order_cnt_type => 0
			H2EmitU( fun, slice, 4 ); //pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4+4)  (TODO: REVISIT)?
		}

		//ref_pic_list_reordering() -> Nothing
		//dec_ref_pic_marking(()
			H2EmitU( fun, 0, 1 ); // no_output_of_prior_pics_flag = 0
			H2EmitU( fun, 0, 1 ); // long_term_reference_flag = 0
		H2EmitSE( fun, 0 ); // slice_qp_delta 

		int k;
		for( k = 0; k < 1; k++ )
		{
			//TODO: SEE: ff_h264_decode_mb_cavlc

			//XXX XXX BIG WARNING: 
			//  We are actually violating H264 here, since we 
			//  don't fill out any more than the first MB of a given slice.

			// this is a "macroblock_layer"
			//Send an I_PCM macroblock, lossless.
			H2EmitUE( fun, 25 ); //I_PCM=25 (mb_type)
			H2EmitFlush( fun );
			
			fun->datacb( fun->opaque, H2mb_dark, sizeof( H2mb_dark ) );
		}
		H2EmitU( fun, 1, 1 ); // Stop bit from rbsp_trailing_bits()
		H2EmitFlush( fun );
	}
	
	fun->datacb( fun->opaque, 0, -2 );
	
	return 0;
}

void H264FUNPREFIX H264FakeIFrame( H264Funzie * fun )
{
	int slice = 0;
	fun->frameno++;
	for( slice = 0; slice < fun->slices; slice++ )
	{
		//slice_layer_without_partitioning_rbsp()
		H2EmitNAL( fun );
		int slicestride = fun->mbw*fun->mbh/fun->slices;

		//NALU "5 = coded slice of an IDR picture"   nal_ref_idc = 3, nal_unit_type = 5 
		// IDR = A coded picture containing only slices with I or SI slice types
		H2EmitU( fun, BuildNALU( 3, 5 ), 8 ); 

		// slice_header();
		H2EmitUE( fun, slice*slicestride );    //first_mb_in_slice 0 = new frame.
		H2EmitUE( fun, 7 );    //I-slice only. (slice_type == 7 (I slice))
		H2EmitUE( fun, 0 );    //pic_parameter_set_id = 0 (referencing pps 0)
		H2EmitU( fun, fun->frameno, 16 );	//frame_num
		H2EmitUE( fun, 0 ); // idr_pic_id
		if( fun->cnt_type == 0 )
		{
			//pic_order_cnt_type => 0
			H2EmitU( fun, slice, 4 ); //pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4+4)  (TODO: REVISIT)?
		}

		//ref_pic_list_reordering() -> Nothing
		//dec_ref_pic_marking(()
			H2EmitU( fun, 0, 1 ); // no_output_of_prior_pics_flag = 0
			H2EmitU( fun, 0, 1 ); // long_term_reference_flag = 0
		H2EmitSE( fun, 0 ); // slice_qp_delta 

		int k;
		for( k = 0; k < 1; k++ )
		{
			//TODO: SEE: ff_h264_decode_mb_cavlc

			//XXX XXX BIG WARNING: 
			//  We are actually violating H264 here, since we 
			//  don't fill out any more than the first MB of a given slice.

			// this is a "macroblock_layer"
			//Send an I_PCM macroblock, lossless.
			H2EmitUE( fun, 25 ); //I_PCM=25 (mb_type)
			H2EmitFlush( fun );
			
			fun->datacb( fun->opaque, H2mb_dark, sizeof( H2mb_dark ) );
		}
		H2EmitU( fun, 1, 1 ); // Stop bit from rbsp_trailing_bits()
		H2EmitFlush( fun );
	}
	fun->datacb( fun->opaque, 0, -2 );
}


void H264FUNPREFIX H264FunAddMB( H264Funzie * fun, int x, int y, uint8_t * data, H264FunPayload pl )
{
	int mbid = (x+y*fun->mbw);
	H264FunzieUpdate * update = &fun->frameupdates[mbid];
	if( update )
	{
		FreeFunzieUpdate( update );
	}

	if( pl == H264FUN_PAYLOAD_LUMA_ONLY_COPY_ON_SUBMIT )
	{
		update->data = malloc( 256 );
		memcpy( update->data, data, 256 );
		update->pl = H264FUN_PAYLOAD_LUMA_ONLY;
	}
	else if( pl == H264FUN_PAYLOAD_LUMA_AND_CHROMA_COPY_ON_SUBMIT )
	{
		update->data = malloc( 384 );
		memcpy( update->data, data, 384 );
		pl = H264FUN_PAYLOAD_LUMA_AND_CHROMA;
	}
	else
	{
		update->data = data;
		update->pl = pl;
	}
}

int H264FUNPREFIX H264FunEmitFrame( H264Funzie * fun )
{
	// Iterate over all funzie->frameupdates and emit!
	//H2EmitNAL( fun );
	
	fun->frameno++;

	int mbs = fun->mbh * fun->mbw;
	int stride = mbs/fun->slices;
	int slices = fun->slices;
	int mb = 0;
	int slice;
	H264FunzieUpdate * updates = fun->frameupdates;
	for( slice = 0; slice < slices; slice++ )
	{
		H2EmitNAL( fun );
		
		//NALU "1 = coded slice of a non-IDR picture"   nal_ref_idc = 3, nal_unit_type = 1 
		// IDR = A coded picture containing only slices with I or SI slice types
		H2EmitU( fun, BuildNALU( 3, 1 ), 8 ); 

		// slice_layer_without_partitioning_rbsp()

		int linestride = mbs/slices;

		// slice_header();
		H2EmitUE( fun, slice*linestride );    //first_mb_in_slice 0 = new frame.
		H2EmitUE( fun, 5 );    //P-slice only. (slice_type == 5 (P slice))  (P and I allowed macroblocks)
		H2EmitUE( fun, 0 );    //pic_parameter_set_id = 0 (referencing pps 0)
		H2EmitU( fun, fun->frameno, 16 );	//frame_num

		if( fun->cnt_type == 0 )
		{
			H2EmitU( fun, 0, 4 ); //pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4+4)
		}

		H2EmitU( fun, 0, 1 ); // num_ref_idx_active_override_flag = 0

		//ref_pic_list_reordering()
			H2EmitU( fun, 0, 1 ); //ref_pic_list_reordering_flag_l0

		//ref_pic_list_reordering() -> Nothing
		//dec_ref_pic_marking(()
			H2EmitU( fun, 0, 1 ); // adaptive_ref_pic_marking_mode_flag = 0

		H2EmitSE( fun, 0 ); // slice_qp_delta 
		
		int mbend = mb + stride;

		do
		{
			int mbst = mb;
			H264FunzieUpdate * thisupdate = updates + mb;
			while( ( mb != mbend ) && ( thisupdate->pl == H264FUN_PAYLOAD_NONE ) )
			{
				mb++;
				thisupdate = updates + mb;
			}
			int skip = mb - mbst;
			
			H2EmitUE( fun, skip );

			if( mb == mbend )
			{
				break;
			}

			//Send an I_PCM macroblock, lossless.
			H2EmitUE( fun, 25+5 ); //I_PCM=25 (mb_type)  (see 
				// "The macroblock types for P and SP slices are specified in Table 7-10 and Table 7-8. mb_type values 0 to 4 are specified
				// in Table 7-10 and mb_type values 5 to 30 are specified in Table 7-8, indexed by subtracting 5 from the value of
				// mb_type."
			H2EmitFlush( fun );
			// "Sample construction process for I_PCM macroblocks "

			switch( thisupdate->pl )
			{
			case H264FUN_PAYLOAD_EMPTY:
				fun->datacb( fun->opaque, H2mb_dark, sizeof( H2mb_dark ) );
				break;
			case H264FUN_PAYLOAD_LUMA_ONLY:
				fun->datacb( fun->opaque, thisupdate->data, 256 );
				fun->datacb( fun->opaque, H2mb_dark+256, 128 );
				break;
			case H264FUN_PAYLOAD_LUMA_ONLY_DO_NOT_FREE:
				fun->datacb( fun->opaque, thisupdate->data, 256 );
				fun->datacb( fun->opaque, H2mb_dark+256, 128 );
				break;
			case H264FUN_PAYLOAD_LUMA_AND_CHROMA:
				fun->datacb( fun->opaque, thisupdate->data, 384 );
				break;
			case H264FUN_PAYLOAD_LUMA_AND_CHROMA_DO_NOT_FREE:
				fun->datacb( fun->opaque, thisupdate->data, 384 );
				break;
			}

			FreeFunzieUpdate( thisupdate );

			mb++;
		} while( mb != mbend );
		H2EmitU( fun, 1, 1 ); // Stop bit from rbsp_trailing_bits()
		H2EmitFlush( fun );
	}

	fun->datacb( fun->opaque, 0, -6 );
}


// Emit the frame, but as an IFrame
int H264FUNPREFIX H264FunEmitIFrame( H264Funzie * fun )
{
	int slice = 0;
	fun->frameno++;
	H264FunzieUpdate * updates = fun->frameupdates;
	int mb = 0;
	int mbs = fun->mbh * fun->mbw;
	int stride = mbs/fun->slices;
	int ict = 0;
	for( slice = 0; slice < fun->slices; slice++ )
	{
		//slice_layer_without_partitioning_rbsp()
		H2EmitNAL( fun );
		int slicestride = fun->mbw*fun->mbh/fun->slices;

		//NALU "5 = coded slice of an IDR picture"   nal_ref_idc = 3, nal_unit_type = 5 
		// IDR = A coded picture containing only slices with I or SI slice types
		H2EmitU( fun, BuildNALU( 3, 5 ), 8 ); 

		// slice_header();
		H2EmitUE( fun, slice*slicestride );    //first_mb_in_slice 0 = new frame.
		H2EmitUE( fun, 7 );    //I-slice only. (slice_type == 7 (I slice))
		H2EmitUE( fun, 0 );    //pic_parameter_set_id = 0 (referencing pps 0)
		H2EmitU( fun, fun->frameno, 16 );	//frame_num
		H2EmitUE( fun, 0 ); // idr_pic_id
		if( fun->cnt_type == 0 )
		{
			//pic_order_cnt_type => 0
			H2EmitU( fun, slice, 4 ); //pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4+4)  (TODO: REVISIT)?
		}

		//ref_pic_list_reordering() -> Nothing
		//dec_ref_pic_marking(()
			H2EmitU( fun, 0, 1 ); // no_output_of_prior_pics_flag = 0
			H2EmitU( fun, 0, 1 ); // long_term_reference_flag = 0
		H2EmitSE( fun, 0 ); // slice_qp_delta 

		int mbend = mb + stride;

		do
		{
			H264FunzieUpdate * thisupdate = updates + mb;
/*
			while( ( mb != mbend ) && ( thisupdate->pl == H264FUN_PAYLOAD_NONE ) )
			{
				mb++;
				thisupdate = updates + mb;
			}

			
			int skip = mb - mbst;
			H2EmitUE( fun, skip );

			if( mb == mbend )
			{
				break;
			}
*/
			//Send an I_PCM macroblock, lossless.
			H2EmitUE( fun, 25+5 ); //I_PCM=25 (mb_type)  (see 
				// "The macroblock types for P and SP slices are specified in Table 7-10 and Table 7-8. mb_type values 0 to 4 are specified
				// in Table 7-10 and mb_type values 5 to 30 are specified in Table 7-8, indexed by subtracting 5 from the value of
				// mb_type."
			H2EmitFlush( fun );
			// "Sample construction process for I_PCM macroblocks "

			ict++;
			switch( thisupdate->pl )
			{
			case H264FUN_PAYLOAD_NONE:
			case H264FUN_PAYLOAD_EMPTY:
				fun->datacb( fun->opaque, H2mb_dark, sizeof( H2mb_dark ) );
				break;
			case H264FUN_PAYLOAD_LUMA_ONLY:
				fun->datacb( fun->opaque, thisupdate->data, 256 );
				fun->datacb( fun->opaque, H2mb_dark+256, 128 );
				break;
			case H264FUN_PAYLOAD_LUMA_ONLY_DO_NOT_FREE:
				fun->datacb( fun->opaque, thisupdate->data, 256 );
				fun->datacb( fun->opaque, H2mb_dark+256, 128 );
				break;
			case H264FUN_PAYLOAD_LUMA_AND_CHROMA:
				fun->datacb( fun->opaque, thisupdate->data, 384 );
				break;
			case H264FUN_PAYLOAD_LUMA_AND_CHROMA_DO_NOT_FREE:
				fun->datacb( fun->opaque, thisupdate->data, 384 );
				break;
			}

			FreeFunzieUpdate( thisupdate );

			mb++;
		} while( mb != mbend );
		H2EmitU( fun, 1, 1 ); // Stop bit from rbsp_trailing_bits()

		H2EmitFlush( fun );
	}

	fun->datacb( fun->opaque, 0, -2 );
	printf( "ICT: %d\n", ict );
}


void H264FUNPREFIX H264FunClose( H264Funzie * funzie )
{
	int i;
	int ct = funzie->mbh * funzie->mbw;
	for( i = 0; i < ct; i++ )
	{
		FreeFunzieUpdate( &funzie->frameupdates[i] );
	}
	free( funzie->frameupdates );
	free( funzie->params );
}

const uint8_t h264fun_mp4header[48] = {
	0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x69, 0x73, 0x6F, 0x6D, 0x00, 0x00, 0x02, 0x00,
	0x69, 0x73, 0x6F, 0x6D, 0x69, 0x73, 0x6F, 0x32, 0x61, 0x76, 0x63, 0x31, 0x6D, 0x70, 0x34, 0x31,
	0x00, 0x00, 0x00, 0x08, 0x66, 0x72, 0x65, 0x65, 0x00, 0x17, 0x58, 0x89, 0x6D, 0x64, 0x61, 0x74 };


