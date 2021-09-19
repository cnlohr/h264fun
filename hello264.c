/*
 * World's Smallest h.264 Encoder, by Ben Mesander.
 *
 * For background, see the post http://cardinalpeak.com/blog?p=488
 *
 * Copyright (c) 2010, Cardinal Peak, LLC.  http://cardinalpeak.com
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1) Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2) Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 
 * 3) Neither the name of Cardinal Peak nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * CARDINAL PEAK, LLC BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* SQCIF */
#define LUMA_WIDTH 128
#define LUMA_HEIGHT 96
#define CHROMA_WIDTH LUMA_WIDTH / 2
#define CHROMA_HEIGHT LUMA_HEIGHT / 2

/* YUV planar data, as written by ffmpeg */
typedef struct
{
  uint8_t Y[LUMA_HEIGHT][LUMA_WIDTH];
  uint8_t Cb[CHROMA_HEIGHT][CHROMA_WIDTH];
  uint8_t Cr[CHROMA_HEIGHT][CHROMA_WIDTH];
} __attribute__((__packed__)) frame_t;

frame_t frame;

/* H.264 bitstreams */
const uint8_t sps[] = { 0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x0a, 0xf8, 0x41, 0xa2 };
const uint8_t pps[] = { 0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x38, 0x80 };
const uint8_t slice_header[] = { 0x00, 0x00, 0x00, 0x01, 0x05, 0x88, 0x84, 0x21, 0xa0 };
const uint8_t macroblock_header[] = { 0x0d, 0x00 };

/* Write a macroblock's worth of YUV data in I_PCM mode */
void macroblock(const int i, const int j)
{
  int x, y;

  if (! ((i == 0) && (j == 0)))
  {
    fwrite(&macroblock_header, 1, sizeof(macroblock_header), stdout);
  }

  for(x = i*16; x < (i+1)*16; x++)
    for (y = j*16; y < (j+1)*16; y++)
      fwrite(&frame.Y[x][y], 1, 1, stdout);
  for (x = i*8; x < (i+1)*8; x++)
    for (y = j*8; y < (j+1)*8; y++)
      fwrite(&frame.Cb[x][y], 1, 1, stdout);
  for (x = i*8; x < (i+1)*8; x++)
    for (y = j*8; y < (j+1)*8; y++)
      fwrite(&frame.Cr[x][y], 1, 1, stdout);
}

/* Write out PPS, SPS, and loop over input, writing out I slices */
int main(int argc, char **argv)
{
  int i, j;

  fwrite(sps, 1, sizeof(sps), stdout);
  fwrite(pps, 1, sizeof(pps), stdout);
  
  int frameno = 0;
  //while (! feof(stdin))
  for( frameno = 0; frameno < 10; frameno++ )
  {
   // fread(&frame, 1, sizeof(frame), stdin);
    fwrite(slice_header, 1, sizeof(slice_header), stdout);

    for (i = 0; i < LUMA_HEIGHT/16 ; i++)
      for (j = 0; j < LUMA_WIDTH/16; j++)
				macroblock(i, j);

    fputc(0x80, stdout); /* slice stop bit */
  }

  return 0;
}
