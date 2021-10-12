all : testbase testbase.h264 128x128.h264 testdec testbase.mp4 testfile.mp4 rtsptest

#~/git/h264Bitstream/h264_analyze testbase.h264  | less
#ffmpeg -i testbase.h264 -vcodec copy -vbsf h265_mp4toannexb -an of.h264

#FFMPEG:=~/git/FFmpeg/ffmpeg
FFMPEG:=ffmpeg

testbase : testbase.c
	$(CC) -g -o $@ $^ -lm

testbase.h264 : testbase
	./$<

testbase.mp4 : testbase.h264
	$(FFMPEG) -i testbase.h264 -vcodec copy -fflags +genpts -an testbase.mp4

testfile : testfile.c
	$(CC) -g -o $@ $^ -lm -g

testfile.h264 : testfile
	./$<

testfile.mp4 : testfile.h264
	$(FFMPEG) -i testfile.h264 -vcodec copy -fflags +genpts -an testfile.mp4

rtsptest : rtsptest.c
	gcc -g -o $@ $^ -lpthread -Os -s

128x128.h264 : 128x128.png
	$(FFMPEG) -loop 1 -i $^ -c:v h264 -t 1 -pix_fmt yuv420p $@

testdec : testdec.c
	$(CC) -o $@ $^

clean :
	rm -rf testbase testbase.h264 128x128.h264 testdec testbase.mp4 testfile testfile.h264 testfile.mp4 rtsptest

