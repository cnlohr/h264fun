#include "h264fun.h"

// Sometimes written u(#)
static void H2EmitU( H264Funzie * fun, uint32_t data, int bits )
{
	for( bits--; bits >= 0 ; bits-- )
	{
		uint8_t b = ( data >> bits ) & 1;
		bytesofar |= b << bitssofarm7;
		if( bitssofarm7-- == 0 )
		{
			fun->datacb( fun->opaque, &bytesofar, 1 );
			bytesofar = 0;
			bitssofarm7 = 7;
		}
	}
}

static int H2EmitFlush( H264Funzie * fun )
{
	if( bitssofarm7 != 7 )
	{
		fun->datacb( fun->opaque, &bytesofar, 1 );
		bytesofar = 0;
		bitssofarm7 = 7;
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
		EmitU( fun, 1, 1 );
		return;
	}
	uint64_t enc;
	if( data < 0 )
		enc = ((-data) << 1 ) | 1;
	else
		enc = data << 1;

	int numbits = H264FUNDeBruijnLog2( enc );
	EmitU( fun, 0, numbits-1 );
	EmitU( fun, enc, numbits );
}

// Sometimes written ue(v)
static void H2EmitUE( H264Funzie * fun, int64_t data )
{
	if( data == 0 )
	{
		EmitU( 1, 1 );
		return;
	}
	data++;
	int numbits = H264FUNDeBruijnLog2( data );
	EmitU( 0, numbits-1 );
	EmitU( data, numbits );
}

void H2EmitNAL( H264Funzie * fun )
{
	H2EmitFlush( fun );
	fun->datacb( fun->opaque, 0, -1 );
}

#define BuildNALU( ref_idc, unit_type ) ( ( (ref_idc ) << 5 ) | ( unit_type ) )

int H264FUNPREFIX H264FunInit( H264Funzie * fun, int w, int h, int slices, H264FunData * datacb )
{
	// A completely dark frame.
	uint8_t mb_dark[384];
	memset( mb_dark, 0, 256 );
	memset( mb_dark+256, 128, 128 );
	
	if( ( w & 0xf ) || ( h & 0xf ) ) return -1;
	fun->bytesofar = 0;
	fun->bitssofarm7 = 7;
	fun->w = w;
	fun->h = h;
	fun->slices = slices;
	fun->datacb = datacb;
	int mbw = fun->mbw = w>>4;
	int mbh = fun->mbh = h>>4;
	fun->frameupdates = calloc( sizeof( H264FunzieUpdate ), mbw * mbh );
	
	// Generate stream.
	H2EmitNAL( fun );
	
	//seq_parameter_set_rbsp()
	H2EmitU( fun, BuildNALU( 3, 7 ), 8 ); //NALU "7 = sequence parameter set"
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
	H2EmitUE( fun, 0 );    // pic_order_cnt_type
		H2EmitUE( fun, 0 );    // log2_max_pic_order_cnt_lsb_minus4
	H2EmitUE( fun, 0 );    // num_ref_frames (we only send I slices) (I think this is right)
	H2EmitU( fun, 0, 1 );	// gaps_in_frame_num_value_allowed_flag  ( =0 says we can't skip frames.)
	H2EmitUE( fun, blk_x-1 ); // pic_width_in_mbs_minus_1.  (x16 px)
	H2EmitUE( fun, blk_y-1 ); // pic_height_in_map_units_minus_1.   (x16 px)
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
		H2EmitU( fun, 1, 1 ); //timing_info_present_flag = 1
			H2EmitU( fun, 1000, 32 ); // num_units_in_tick = 1
			H2EmitU( fun, 60000, 32 ); // time_scale = 50
			H2EmitU( fun, 0, 1 ); // fixed_frame_rate_flag = 0
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
	
	int slice = 0;
	for( slice = 0; slice < slices; slice++ )
	{
		//slice_layer_without_partitioning_rbsp()
		H2EmitNAL( fun );
		int slicestride = blk_x*blk_y/slices;

		//NALU "5 = coded slice of an IDR picture"   nal_ref_idc = 3, nal_unit_type = 5 
		// IDR = A coded picture containing only slices with I or SI slice types
		H2EmitU( fun, BuildNALU( 3, 5 ), 8 ); 

		// slice_header();
		H2EmitUE( fun, slice*slicestride );    //first_mb_in_slice 0 = new frame.
		H2EmitUE( fun, 7 );    //I-slice only. (slice_type == 7 (I slice))
		H2EmitUE( fun, 0 );    //pic_parameter_set_id = 0 (referencing pps 0)
		H2EmitU( fun, i, 16 );	//frame_num
		H2EmitUE( fun, 0 ); // idr_pic_id
			//pic_order_cnt_type => 0
			H2EmitU( fun, i, 4 ); //pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4+4)  (TODO: REVISIT)?

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
			
			fun->datacb( fun->opaque, mb_dark, sizeof( mb_dark ) );
		}
		H2EmitU( fun, 1, 1 ); // Stop bit from rbsp_trailing_bits()
		H2EmitFlush( fun );
	}
	
	fun->datacb( fun->opaque, 0, -2 );
	
	return 0;
}

void H264FUNPREFIX H264FunAddMB( H264Funzie * fun, int x, int y, uint8_t * data, enum H264FunPayload pl )
{
	int mbid = (x+y*funzie->mbw);
	H264FunzieUpdate * update = &funzie->frameupdates[mbid];
	update->mbid = mbid;
	update->data = data;
	update->pl = pl;
}

int H264FUNPREFIX H264FunEmitFrame( H264Funzie * fun )
{
	// Iterate over all funzie->frameupdates and emit!
	//H2EmitNAL( fun );
	
	int mbs = fun->mbh * fun->mbw;
	int stride = mbs/fun->slices;
	int mb = 0;
	int slice;
	for( slice = 0; slice < slices; slice++ )
	{
		H2EmitNAL( fun );
		
		//NALU "1 = coded slice of a non-IDR picture"   nal_ref_idc = 3, nal_unit_type = 1 
		// IDR = A coded picture containing only slices with I or SI slice types
		H2EmitU( fun, BuildNALU( 3, 1 ), 8 ); 

		// slice_layer_without_partitioning_rbsp()

		int linestride = blk_x*blk_y/slices;

		// slice_header();
		H2EmitUE( fun, slice*linestride );    //first_mb_in_slice 0 = new frame.
		H2EmitUE( fun, 5 );    //P-slice only. (slice_type == 5 (P slice))  (P and I allowed macroblocks)
		H2EmitUE( fun, 0 );    //pic_parameter_set_id = 0 (referencing pps 0)
		H2EmitU( fun, i, 16 );	//frame_num

		H2EmitU( fun, 0, 4 ); //pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4+4)

		H2EmitU( fun, 0, 1 ); // num_ref_idx_active_override_flag = 0

		//ref_pic_list_reordering()
			H2EmitU( fun, 0, 1 ); //ref_pic_list_reordering_flag_l0

		//ref_pic_list_reordering() -> Nothing
		//dec_ref_pic_marking(()
			H2EmitU( fun, 0, 1 ); // adaptive_ref_pic_marking_mode_flag = 0

		H2EmitSE( fun, 0 ); // slice_qp_delta 
		
		int k;
		for( k = 0; k < 1; k++ )
		{
			int kx = k % blk_x;
			int ky = slice;//k / blk_x;

			//slice_data(()

			int toskip = rand()%(linestride);
			H2EmitUE( fun, toskip );  //mb_skip_run

			int col = (rand()%4);
			// this is a "macroblock_layer"

			{
				//Send an I_PCM macroblock, lossless.
				H2EmitUE( fun, 25+5 ); //I_PCM=25 (mb_type)  (see 
					// "The macroblock types for P and SP slices are specified in Table 7-10 and Table 7-8. mb_type values 0 to 4 are specified
					// in Table 7-10 and mb_type values 5 to 30 are specified in Table 7-8, indexed by subtracting 5 from the value of
					// mb_type."
				H2EmitFlush( fun );
				// "Sample construction process for I_PCM macroblocks "

				fun->datacb( fun->opaque, DATA SIZE, 

			}

			//Skip rest of line.
			if( toskip != (linestride-1) )
				H2EmitUE( fun, (linestride-1)-toskip );  //mb_skip_run
		}
		H2EmitU( fun, 1, 1 ); // Stop bit from rbsp_trailing_bits()
		H2EmitFlush( fun );
	}

	fun->datacb( fun->opaque, 0, -2 );
}
