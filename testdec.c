#include <stdio.h>
#include <stdint.h>

int main()
{
}
#if 0

byte_stream_nal_unit( NumBytesInNALunit ) {               // Descriptor 
	while(next_bits(24) != 0x000001 &&
		next_bits(32) != 0x00000001)
			leading_zero_8bits /* equal to 0x00 */        // f(8)

	if(next_bits(24) != 0x000001)
		zero_byte /* equal to 0x00 */                     // f(8)
		
	start_code_prefix_one_3bytes /* equal to 0x000001 */  // f(24)
	nal_unit( NumBytesInNALunit )   
	while(more_data_in_byte_stream() &&
		next_bits(24) != 0x000001 &&
		next_bits(32) != 0x00000001)
			trailing_zero_8bits /* equal to 0x00 */       // f(8)
}
#endif
