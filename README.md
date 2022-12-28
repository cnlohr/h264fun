# h264fun

Fun with h264 (mostly encoding)

 * H264 lossless encoding works
 * RTSP works with VLC and VRChat, and has ~40ms latency.

# Debugging notes

Use RTSP-Simple-Server

```
docker run --rm -it --network=host -e RTSP_LOGLEVEL=debug aler9/rtsp-simple-server
```


This is a pretty helpful thing for spot-checking your h.264 streams:
https://mradionov.github.io/h264-bitstream-viewer/