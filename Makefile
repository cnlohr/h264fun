all : testbase testbase.h264 testdec rtsptest rtmptest

#~/git/h264Bitstream/h264_analyze testbase.h264  | less
#ffmpeg -i testbase.h264 -vcodec copy -vbsf h265_mp4toannexb -an of.h264

#FFMPEG:=~/git/FFmpeg/ffmpeg


ifeq ($(shell uname), Linux)
CFLAGS:=-g -Os
LDFLAGS:=-lm -lpthread
else
CFLAGS:=-g -Os
LDFLAGS:=-lm -luser32 -lws2_32
endif
   
FFMPEG:=ffmpeg

testbase : testbase.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

testbase.h264 : testbase
	./$<

testbase.mp4 : testbase.h264
	$(FFMPEG) -i testbase.h264 -vcodec copy -fflags +genpts -an testbase.mp4

testfile : testfile.c h264fun.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

testfile.h264 : testfile
	./$<

testfile.mp4 : testfile.h264
	$(FFMPEG) -i testfile.h264 -vcodec copy -fflags +genpts -an testfile.mp4

rtsptest : rtsptest.c rtspfun.c h264fun.c
	gcc $(CFLAGS) -o $@ $< $(LDFLAGS)

rtmptest : rtmptest.c rtmpfun.c h264fun.c
	gcc $(CFLAGS) -o $@ $< $(LDFLAGS)

#128x128.h264 : 128x128.png
#	$(FFMPEG) -loop 1 -i $^ -c:v h264 -t 1 -pix_fmt yuv420p $@

testdec : testdec.c
	$(CC) -o $@ $^

clean :
	rm -rf testbase testbase.h264 testdec testbase.mp4 testfile testfile.h264 testfile.mp4 rtsptest rtmptest

