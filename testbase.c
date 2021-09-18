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
	EmitU( BuildNALU( 3, 7 ), 8 ); //NALU "7 = sequence parameter set"
	EmitU( 66, 8 ); // Baseline Profile
	EmitU( 0, 1 );  // We're not going to honor constraints.
	EmitU( 0, 1 );  // We're not going to honor constraints.
	EmitU( 0, 1 );  // We're not going to honor constraints.
	EmitU( 0, 1 );  // We're not going to honor constraints.
	EmitU( 0, 4 );  // Reserved 
	EmitU( 10, 8 ); //Level 1, sec A.3.1.
	EmitUE( 0 ); // seq_parameter_set_id = 0
	EmitUE( 12 ); // log2_max_frame_num_minus4
	EmitUE( 0 ); // pic_order_cnt_type
	EmitUE( 0 ); // log2_max_pic_order_cnt_lsb_minus4
	EmitUE( 0 ); // num_ref_frames (we only send I slices) ** Fix me.
	EmitU( 0, 1 );
	EmitUE( blk_x-1 ); // pic_width_in_mbs_minus_1.  (128 px)
	EmitUE( blk_y-1 ); // pic_height_in_map_units_minus_1.   (128 px)
	EmitU( 1, 1 ); // We will not to field/frame encoding.
	EmitU( 0, 1 ); // Used for B slices. We will not send B slices.
	EmitU( 0, 1 ); // We will not do frame cropping.
	EmitU( 0, 1 ); // We will not send VUI data.
	EmitU( 1, 1 ); // Stop bit.
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
	EmitUE( 2 ); //num_ref_idx_l0_active_minus1
	EmitUE( 0 ); //num_ref_idx_l1_active_minus1
	EmitU( 1, 1 ); // weighted_pred_flag
	EmitU( 2, 2 ); // weighted_bipred_idc
	EmitSE( 0 ); //pic_init_qp_minus26 (was -3)
	EmitSE( 0 ); //pic_init_qs_minus26
	EmitSE( 0 ); //chroma_qp_index_offset (was -2 )
	EmitU( 0, 1 ); //deblocking_filter_control_present_flag
	EmitU( 0, 1 ); //constrained_intra_pred_flag
	EmitU( 0, 1 ); //redundant_pic_cnt_present_flag
	EmitFlush();

	int i;
	for( i = 0; i < 200; i++ )
	{
		EmitU( 0x00000001, 32 );
		EmitU( BuildNALU( 3, 5 ), 8 ); //NALU "5 = coded slice of an IDR picture"
		EmitUE( 0 ); //first_mb_in_slice 0 = new frame.
		EmitUE( 7 ); //I-slice only. (slice_type == 7 (I slice))
		EmitUE( 0 ); //pic_parameter_set_id (which pic parameter set are we using?)
		EmitU( i, 16 );	//frame_num
		EmitUE( 0 ); // idr_pic_id
		EmitU( 0, 4 ); //pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4+4)
		EmitU( 0, 2 ); // ??? ??? ???
		EmitSE( 0 ); // slice_qp_delta 

		int k;
		for( k = 0; k < blk_x*blk_y; k++ )
		{
			int kx = k % blk_x;
			int ky = k / blk_x;
			// macroblock_layer

			EmitUE( 25 ); //I_PCM=25 (mb_type)
			EmitFlush();
			//"Sample construction process for I_PCM macroblocks "
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
				//Monochrome = 128 here.
				EmitU( kx*15+2, 8 );
			}
			for( j = 0; j < 64; j++ )
			{
				//Monochrome = 128 here.
				EmitU( ky*15+2, 8 );
			}
		}
		EmitUE( 0 ); // Is this right?
		EmitFlush();
	}
}

