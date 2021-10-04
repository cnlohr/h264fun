#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>

// Based on
// https://yumichan.net/video-processing/video-compression/introduction-to-h264-nal-unit/
// https://www.cardinalpeak.com/blog/the-h-264-sequence-parameter-set

FILE * fOut;
static uint8_t bytesofar;
static uint8_t bitssofarm7 = 7;

// Sometimes written u(#)
void EmitU( uint32_t data, int bits )
{
//	printf( "%d / %d / %02x\n", bits, bitssofarm7, bytesofar );
	for( bits--; bits >= 0 ; bits-- )
	{
		uint8_t b = ( data >> bits ) & 1;
		bytesofar |= b << bitssofarm7;
		if( bitssofarm7-- == 0 )
		{
	//		printf( "Emit: %02x\n", bytesofar );
			fputc( bytesofar, fOut );
			bytesofar = 0;
			bitssofarm7 = 7;
		}
	}
}

void EmitFlush()
{
	if( bitssofarm7 != 7 )
	{
		fputc( bytesofar, fOut ); 
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

//int blk_x = 40;
//int blk_y = 30;

int blk_x = 128;
int blk_y = 64;
int slices = 4;

int main()
{
	fOut = fopen( "testbase.h264", "wb" );
	EmitU( 0x00000001, 32 );

	//seq_parameter_set_rbsp()
	EmitU( BuildNALU( 3, 7 ), 8 ); //NALU "7 = sequence parameter set"
	EmitU( 66, 8 ); // Baseline Profile  (WAS ORIGINALLY 66)  // profile_idc 
	EmitU( 0, 1 );  // We're not going to honor constraints. constraint_set0_flag? (We are 66 compliant) TODO: REVISIT
	EmitU( 0, 1 );  // We're not going to honor constraints. constraint_set1_flag?  XXX TODO: Without this we can't have multiple groupled slices.
	EmitU( 0, 1 );  // We're not going to honor constraints. constraint_set2_flag?
	EmitU( 0, 1 );  // We're not going to honor constraints. / reserved
	EmitU( 0, 4 );  // Reserved 
	EmitU( 41, 8 ); // level_idc = 41 (4.1)       Level 1, sec A.3.1.   See "Table A-1 – Level limits"
		//Conformance to a particular level shall be specified by setting the syntax element level_idc equal to a value of ten times
		// the level number specified in Table A-1.
	EmitUE( 0 );    // seq_parameter_set_id = 0

#if 0
		// THIS DOES NOT WORK!!!
		// profile_idc = 100 -> lets us choose chroma. or select other from  //Selected from cbs_h264_syntax_template.c --> Not supported on Android.
		EmitUE( 0 );   // chroma_format_idc = 3  => [ 256, 384, 512, 768 ] ff_h264_mb_sizes
		//EmitU( 0, 1 ); // separate_colour_plane_flag = 1
		EmitUE( 0 );   // bit_depth_luma_minus8 =0
		EmitUE( 0 );   // bit_depth_chroma_minus8 = 0
		EmitU( 0, 1 ); // qpprime_y_zero_transform_bypass_flag = 0
		EmitU( 0, 1 ); // seq_scaling_matrix_present_flag = 0
#endif

	EmitUE( 12 );   // log2_max_frame_num_minus4  (16-bit frame numbers)
	EmitUE( 0 );    // pic_order_cnt_type
		EmitUE( 0 );    // log2_max_pic_order_cnt_lsb_minus4
	EmitUE( 0 );    // num_ref_frames (we only send I slices) (I think this is right)
	EmitU( 0, 1 );	// gaps_in_frame_num_value_allowed_flag  ( =0 says we can't skip frames.)
	EmitUE( blk_x-1 ); // pic_width_in_mbs_minus_1.  (x16 px)
	EmitUE( blk_y-1 ); // pic_height_in_map_units_minus_1.   (x16 px)
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
			EmitU( 60000, 32 ); // time_scale = 50
			EmitU( 0, 1 ); // fixed_frame_rate_flag = 0
		EmitU( 0, 1 ); // nal_hrd_parameters_present_flag = 0
		EmitU( 0, 1 ); // vcl_hrd_parameters_present_flag = 0
		EmitU( 0, 1 ); // pic_struct_present_flag = 0
		EmitU( 1, 1 ); // bitstream_restriction_flag = 1
			EmitU( 0, 1 ); // No motion vectors over pic boundaries
			EmitUE( 1 ); //max_bytes_per_pic_denom
			EmitUE( 1 ); //max_bits_per_mb_denom // 1 should be sufficient for all PCM's but Android still no like it.
			EmitUE( 9 ); //log2_max_mv_length_horizontal
			EmitUE( 10 ); //log2_max_mv_length_vertical
			EmitUE( 0 ); //num_reorder_frames
			EmitUE( 0 ); //max_dec_frame_buffering (?)

	EmitU( 1, 1 ); // Stop bit from rbsp_trailing_bits()
	EmitFlush();

	//pps (need to be ID 0)
	// 00 00 00 01 68 // EB E3 CB 22 C0 (OLD)

	// "PPS"
	EmitU( 0x00000001, 32 );
	EmitU( BuildNALU( 3, 8 ), 8 ); // nal_unit_type = 8 = pic_parameter_set_rbsp( )
	EmitUE( 0 ); // pic_parameter_set_id  (Do we need more, so we can define other PPS types to encode with?)
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
	EmitU( 1, 1 ); //constrained_intra_pred_flag=1 don't use predictive coding everywhere.
	EmitU( 0, 1 ); //redundant_pic_cnt_present_flag = 0
	EmitU( 1, 1 ); // Stop bit from rbsp_trailing_bits()
	EmitFlush();


	/*
		From the spec, a truncated glossary and notes:
			access unit: A set of NAL units always containing exactly one primary coded picture.

			arbitrary slice order: (We probably want this off) the order of coded slice of an
				IDR picture NAL units shall be in the order of increasing macroblock address
				for the first macroblock of each coded slice of an IDR picture NAL unit.

		Gotchas:
			* On Android, specifically, it appears an I-Frame's size may not exceed 90ish% of the
				uncompressed size.  This means you can't fill an IFRAME with purely I_PCM
				blocks.  Only 95ish% of your blocks can be I_PCM.

	*/

	int i;
	for( i = 0; i < 400; i++ )
	{
		if( i == 0 )
		//if( (i%10) == 0 )
		//if( 1 )
		{
			int slice = 0;
			for( slice = 0; slice < slices; slice++ )
			{
				//slice_layer_without_partitioning_rbsp()
				EmitU( 0x00000001, 32 );

				//NALU "5 = coded slice of an IDR picture"   nal_ref_idc = 3, nal_unit_type = 5 
				// IDR = A coded picture containing only slices with I or SI slice types
				EmitU( BuildNALU( 3, 5 ), 8 ); 

				// slice_header();
				EmitUE( slice*(blk_x*(blk_y/slices )));    //first_mb_in_slice 0 = new frame.
				EmitUE( 7 );    //I-slice only. (slice_type == 7 (I slice))
				EmitUE( 0 );    //pic_parameter_set_id = 0 (referencing pps 0)
				EmitU( i, 16 );	//frame_num
				EmitUE( 0 ); // idr_pic_id
					//pic_order_cnt_type => 0
					EmitU( i, 4 ); //pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4+4)  (TODO: REVISIT)?

				//ref_pic_list_reordering() -> Nothing
				//dec_ref_pic_marking(()
					EmitU( 0, 1 ); // no_output_of_prior_pics_flag = 0
					EmitU( 0, 1 ); // long_term_reference_flag = 0
				EmitSE( 0 ); // slice_qp_delta 

				int k;
				for( k = 0; k < 1; k++ )
				{
					int kx = k % blk_x;
					int ky = 
							slice;
							//k / blk_x;

					//if( ( k + slice ) & 1 )
					//if( ky < blk_y-1 || kx < blk_x-4)
					//if( 1 )
					if(kx < 1 )
					{
						// SEE: ff_h264_decode_mb_cavlc

						// this is a "macroblock_layer"
						//Send an I_PCM macroblock, lossless.
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
							EmitU( kx*15+1, 8 );
						}
						for( j = 0; j < 64; j++ )
						{
							//V (Colors)
							EmitU( ky*15+1, 8 );
						}
					}
					else
					{ 
						 // The last few blocks need to be something simple.
						// macroblock_layer()
						EmitUE( 1 ); // mb_type = I_16x16_0_0_0
						// I_16x16_0_0_0 -> Intra16x16PredMode = 0, CodedBlockPatternChroma = 0, CodedBlockPatternLuma = 0
						// MbPartPredMode( mb_type, 0 ) = Intra_16x16
						//mb_pred()
							EmitUE( 0 ); // intra_chroma_pred_mode = 0 for DC.
						//coded_block_pattern -> me(v)
						// me(v): mapped Exp-Golomb-coded syntax element with the left bit first. 
						//			The parsing process for this descriptor is specified in subclause 9.1.
						// ...
						// Otherwise, if the syntax element is coded as me(v), the value of the syntax element
						// is derived by invoking the mapping process for coded block pattern as specified in
						// subclause 9.1.2 with codeNum as the input.
						EmitSE( -5 ); //        dquant= get_se_golomb(&sl->gb);
						EmitU( 5, 8 ); // ce(v) = 1 (ugly/tricky) + something else?  Not sure how residual_block_cavlc works.
						EmitU( 0 + 0 * 32, i);
						//EmitU( 5, 8 ); // ce(v) = 1 (ugly/tricky) + something else?  Not sure how residual_block_cavlc works.
						EmitU( 1, 1 );
					}
				}
				EmitU( 1, 1 ); // Stop bit from rbsp_trailing_bits()
				EmitFlush();
			}
		}
		else
		{
			int slice = 0;
			for( slice = 0; slice < slices; slice++ )
			{
				//slice_layer_without_partitioning_rbsp()
				EmitU( 0x00000001, 32 );

				//NALU "1 = coded slice of a non-IDR picture"   nal_ref_idc = 3, nal_unit_type = 1 
				// IDR = A coded picture containing only slices with I or SI slice types
				EmitU( BuildNALU( 3, 1 ), 8 ); 

				// slice_layer_without_partitioning_rbsp()

				int linestride = blk_y/slices*blk_x;

				// slice_header();
				EmitUE( slice*linestride );    //first_mb_in_slice 0 = new frame.
				EmitUE( 5 );    //P-slice only. (slice_type == 5 (P slice))  (P and I allowed macroblocks)
				EmitUE( 0 );    //pic_parameter_set_id = 0 (referencing pps 0)
				EmitU( i, 16 );	//frame_num

				EmitU( 0, 4 ); //pic_order_cnt_lsb (log2_max_pic_order_cnt_lsb_minus4+4)

				EmitU( 0, 1 ); // num_ref_idx_active_override_flag = 0

				//ref_pic_list_reordering()
					EmitU( 0, 1 ); //ref_pic_list_reordering_flag_l0

				//ref_pic_list_reordering() -> Nothing
				//dec_ref_pic_marking(()
					EmitU( 0, 1 ); // adaptive_ref_pic_marking_mode_flag = 0

				EmitSE( 0 ); // slice_qp_delta 
				
				int k;
				for( k = 0; k < 1; k++ )
				{
					int kx = k % blk_x;
					int ky = slice;//k / blk_x;

					//slice_data(()

					int toskip = rand()%linestride;
					EmitUE( toskip );  //mb_skip_run

					int col = (rand()%4);
					// this is a "macroblock_layer"

					{
						//Send an I_PCM macroblock, lossless.
						EmitUE( 25+5 ); //I_PCM=25 (mb_type)  (see 
							// "The macroblock types for P and SP slices are specified in Table 7-10 and Table 7-8. mb_type values 0 to 4 are specified
							// in Table 7-10 and mb_type values 5 to 30 are specified in Table 7-8, indexed by subtracting 5 from the value of
							// mb_type."
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
							if( col == 0 )
								EmitU( 0, 8 );
							else if( col == 1 )
								EmitU( 255, 8 );
							else if( col == 2 )
								EmitU( ((px+py)&1)*255, 8 );
							else if( col == 3 )
								EmitU( (j), 8 );
							else
								EmitU( sin(r+i+kx+ky*5)*127+128, 8 );
						}
						for( j = 0; j < 64; j++ )
						{
							//U (Colors)
							EmitU( 128, 8 );
						}
						for( j = 0; j < 64; j++ )
						{
							//V (Colors)
							EmitU( 128, 8 );
						}
					}

					//Skip rest of line.
					if( toskip != (linestride-1) )
						EmitUE( (linestride-1)-toskip );  //mb_skip_run
				}
				EmitU( 1, 1 ); // Stop bit from rbsp_trailing_bits()
				EmitFlush();
			}
		}
	}
	
	fclose( fOut );
}



/* Trunk


#if 0 //Intra 4x4
						//Instead, let's try some I_4x4's
						// macroblock_layer()
						EmitUE( 0 ); //mb_type = I_4x4 
								//Intra_4x4 specifies the macroblock prediction mode and specifies that the Intra_4x4 prediction process is invoked as
								//specified in subclause 8.3.1. Intra_4x4 is an Intra macroblock prediction mode.

								//The luma component of a macroblock consists of 16 blocks of 4x4 luma samples. These blocks are inverse scanned
								//using the 4x4 luma block inverse scanning process as specified in subclause 6.4.3.
								//For all 4x4 luma blocks of the luma component of a macroblock with luma4x4BlkIdx = 0..15, the variable
								//Intra4x4PredMode[ luma4x4BlkIdx ] is derived as specified in subclause 8.3.1.1.

								// NOTE: 6.4.3 specifies I_4x4 Scan order two layers of Z ordering.

								// We will want to use Intra4x4PredMode[ luma4x4BlkIdx ] = 2 --> Intra_4x4_DC

								//set coded_block_pattern = 0  (codeNum = 3)


						//mb_pred( mb_type )  -> mb_pred( 0 )
							EmitU( 0, 16 );
							EmitUE( 0 ); // intra_chroma_pred_mode = 0 for DC.

						//coded_block_pattern
						//mb_qp_delta
						EmitSE( -1 );
#endif
// From FFMPeg  -> ff_h264_i_mb_type_info[1] = { MB_TYPE_INTRA16x16, 2,   0 },  { type, pred mode, cbp }
//        cbp                      = ff_h264_i_mb_type_info[mb_type].cbp;
//        sl->intra16x16_pred_mode = ff_h264_i_mb_type_info[mb_type].pred_mode;
//        mb_type                  = ff_h264_i_mb_type_info[mb_type].type;

*/
