all : testbase testbase.h264 128x128.h264 testdec testbase.mp4 testbase.mp4

#~/git/h264Bitstream/h264_analyze testbase.h264  | less
#ffmpeg -i testbase.h264 -vcodec copy -vbsf h265_mp4toannexb -an of.h264

FFMPEG:=~/git/FFmpeg/ffmpeg
#FFMPEG:=ffmpeg

testbase : testbase.c
	$(CC) -o $@ $^ -lm

testbase.h264 : testbase
	./$<

testbase.mp4 : testbase.h264
	$(FFMPEG) -i testbase.h264 -vcodec copy -fflags +genpts -an testbase.mp4


128x128.h264 : 128x128.png
	$(FFMPEG) -loop 1 -i $^ -c:v h264 -t 1 -pix_fmt yuv420p $@

testdec : testdec.c
	$(CC) -o $@ $^

clean :
	rm -rf testbase testbase.h264 128x128.h264 testdec testbase.mp4 hello264.h264 hello264 hello264.mp4
