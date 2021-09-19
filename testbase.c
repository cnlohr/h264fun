#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

// Based on
// https://yumichan.net/video-processing/video-compression/introduction-to-h264-nal-unit/
// https://www.cardinalpeak.com/blog/the-h-264-sequence-parameter-set


static uint8_t bytesofar;
static uint8_t bitssofarm7 = 7;

// Sometimes written u(#)
void EmitU( uint32_t data, int bits )
{
	for( bits--; bits >= 0 ; bits-- )
	{
		uint8_t b = ( data >> bits ) & 1;
		bytesofar |= b << bitssofarm7;
		if( bitssofarm7-- == 0 )
		{
			putchar( bytesofar );
			bytesofar = 0;
			bitssofarm7 = 7;
		}
	}
}

void EmitFlush()
{
	if( bitssofarm7 != 7 )
	{
		putchar( bytesofar ); 
		bytesofar = 0;
		bitssofarm7 = 7;
	}
}

// https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn
// https://stackoverflow.com/questions/21888140/de-bruijn-algorithm-binary-digit-count-64bits-c-sharp

int DeBruijnLog2( uint64_t v )
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
void EmitSE( int64_t data )
{
	if( data == 0 )
	{
		EmitU( 1, 1 );
		return;
	}
	uint64_t enc;
	if( data < 0 )
		enc = ((-data) << 1 ) | 1;
	else
		enc = data << 1;

	int numbits = DeBruijnLog2( enc );
	EmitU( 0, numbits-1 );
	EmitU( enc, numbits );
}

// Sometimes written ue(v)
void EmitUE( int64_t data )
{
	if( data == 0 )
	{
		EmitU( 1, 1 );
		return;
	}
	data++;
	int numbits = DeBruijnLog2( data );
	EmitU( 0, numbits-1 );
	EmitU( data, numbits );
}

#define BuildNALU( ref_idc, unit_type ) ( ( (ref_idc ) << 5 ) | ( unit_type ) )

int blk_x = 16;
int blk_y = 16;

int main()
{
	EmitU( 0x00000001, 32 );

	//seq_parameter_set_rbsp()
	EmitU( BuildNALU( 3, 7 ), 8 ); //NALU "7 = sequence parameter set"
	EmitU( 66, 8 ); // Baseline Profile  (WAS ORIGINALLY 66)
	EmitU( 0, 1 );  // We're not going to honor constraints. constraint_set0_flag?
	EmitU( 0, 1 );  // We're not going to honor constraints. constraint_set1_flag?
	EmitU( 0, 1 );  // We're not going to honor constraints. constraint_set2_flag?
	EmitU( 0, 1 );  // We're not going to honor constraints.
	EmitU( 0, 4 );  // Reserved 
	EmitU( 10, 8 ); // level_idc = 11     (ORIGINALLY 10!!!)         Level 1, sec A.3.1.
	EmitUE( 0 );    // seq_parameter_set_id = 0
	EmitUE( 12 );   // log2_max_frame_num_minus4  (16-bit frame numbers)
	EmitUE( 0 );    // pic_order_cnt_type
		EmitUE( 0 );    // log2_max_pic_order_cnt_lsb_minus4
	EmitUE( 0 );    // num_ref_frames (we only send I slices) (I think this is right)
	EmitU( 0, 1 );	// gaps_in_frame_num_value_allowed_flag  ( =0 says we can't skip frames.)
	EmitUE( blk_x-1 ); // pic_width_in_mbs_minus_1.  (128 px)
	EmitUE( blk_y-1 ); // pic_height_in_map_units_minus_1.   (128 px)
	EmitU( 1, 1 ); // frame_mbs_only_flag = 1 //We will not to field/frame encoding.
	EmitU( 0, 1 ); // direct_8x8_inference_flag = 0 // Used for B slices. We will not send B slices.
	EmitU( 0, 1 ); // frame_cropping_flag = 0
	EmitU( 1, 1 ); // vui_parameters_present_flag = 1
		//vui_parameters()
		EmitU( 1, 1 ); // aspect_ratio_info_present_flag = 1
			EmitU( 1, 8 ); // 1:1 Square
		EmitU( 0, 1 ); // overscan_info_present_flag = 0
		EmitU( 1, 1 ); // video_signal_type_present_flag = 1
			EmitU( 0, 3 ); //video_format
			EmitU( 1, 1 ); //video_full_range_flag
			EmitU( 0, 1 ); //colour_description_present_flag = 0
		EmitU( 0, 1 ); // chroma_loc_info_present_flag = 0
		EmitU( 1, 1 ); //timing_info_present_flag = 1
			EmitU( 1000, 32 ); // num_units_in_tick = 1
			EmitU( 30000, 32 ); // time_scale = 50
			EmitU( 0, 1 ); // fixed_frame_rate_flag = 0
		EmitU( 0, 1 ); // nal_hrd_parameters_present_flag = 0
		EmitU( 0, 1 ); // vcl_hrd_parameters_present_flag = 0
		EmitU( 0, 1 ); // pic_struct_present_flag = 0
		EmitU( 1, 1 ); // bitstream_restriction_flag = 1
			EmitU( 0, 1 ); // No motion vectors over pic boundaries
			EmitUE( 0 ); //max_bytes_per_pic_denom
			EmitUE( 0 ); //max_bits_per_mb_denom
			EmitUE( 9 ); //log2_max_mv_length_horizontal
			EmitUE( 9 ); //log2_max_mv_length_vertical
			EmitUE( 2 ); //num_reorder_frames
			EmitUE( 4 ); //max_dec_frame_buffering

	EmitU( 1, 1 ); // Stop bit from rbsp_trailing_bits()
	EmitFlush();

	//pps (need to be ID 0)
	// 00 00 00 01 68 // EB E3 CB 22 C0 (OLD)
	EmitU( 0x00000001, 32 );
	EmitU( BuildNALU( 3, 8 ), 8 ); //NALU "5 = coded slice of an IDR picture"
	EmitUE( 0 ); // pic_parameter_set_id
	EmitUE( 0 ); // seq_parameter_set_id
	EmitU( 0, 1 ); //entropy_coding_mode_flag (OFF, LEFT COLUMN)
	EmitU( 0, 1 ); //pic_order_present_flag
	EmitUE( 0 ); //num_slice_groups_minus1
	EmitUE( 0 ); //num_ref_idx_l0_active_minus1 >>> THIS IS SUSPICIOUS (WAS 2)
	EmitUE( 0 ); //num_ref_idx_l1_active_minus1
	EmitU( 0, 1 ); // weighted_pred_flag (was 1)
	EmitU( 0, 2 ); // weighted_bipred_idc (Was 2)
	EmitSE( 0 ); //pic_init_qp_minus26 (was -3)
	EmitSE( 0 ); //pic_init_qs_minus26
	EmitSE( 0 ); //chroma_qp_index_offset (was -2 )
	EmitU( 0, 1 ); //deblocking_filter_control_present_flag
	EmitU( 0, 1 ); //constrained_intra_pred_flag
	EmitU( 0, 1 ); //redundant_pic_cnt_present_flag = 0
	EmitU( 1, 1 ); // Stop bit from rbsp_trailing_bits()
	EmitFlush();

	int i;
	for( i = 0; i < 100; i++ )
	{
	//slice_layer_without_partitioning_rbsp()
		EmitU( 0x00000001, 32 );
		// slice_header();
		EmitU( BuildNALU( 3, 5 ), 8 ); //NALU "5 = coded slice of an IDR picture"   nal_ref_idc = 3, nal_unit_type = 5 
		EmitUE( 0 );    //first_mb_in_slice 0 = new frame.
		EmitUE( 7 );    //I-slice only. (slice_type == 7 (I slice))
		EmitUE( 0 );    //pic_parameter_set_id = 0 (referencing pps 0)
		EmitU( i, 16 );	//frame_num
		EmitUE( 0 ); // idr_pic_id
			//pic_order_cnt_type => 0
			EmitU( 0, 4 ); //pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4+4)

		//ref_pic_list_reordering() -> Nothing
		//dec_ref_pic_marking(()
			EmitU( 0, 1 ); // no_output_of_prior_pics_flag = 0
			EmitU( 0, 1 ); // long_term_reference_flag = 0
		EmitSE( -3 ); // slice_qp_delta 

		int k;
		for( k = 0; k < blk_x*blk_y; k++ )
		{
			int kx = k % blk_x;
			int ky = k / blk_x;
			// this is a "macroblock_layer"

			EmitUE( 25 ); //I_PCM=25 (mb_type)
			EmitFlush();
			// "Sample construction process for I_PCM macroblocks "
			int j;
			for( j = 0; j < 256; j++ )
			{
				int px = j % 16;
				int py = j / 16;
				px -= 8;
				py -= 8;
				int r = sqrt( px*px+py*py );
				EmitU( sin(r+i+kx+ky*5)*127+128, 8 );
			}
			for( j = 0; j < 64; j++ )
			{
				//U (Colors)
				EmitU( kx*15+2, 8 );
			}
			for( j = 0; j < 64; j++ )
			{
				//V (Colors)
				EmitU( ky*15+2, 8 );
			}
		}
		EmitU( 1, 1 ); // Stop bit from rbsp_trailing_bits()
		EmitFlush();
	}
}

