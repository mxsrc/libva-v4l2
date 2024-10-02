# v4l2 libVA Backend

## About

This libVA backend is designed to work with the Linux Video4Linux2 Request API that is used by a number of video codecs drivers, including the Video Engine found in most Allwinner SoCs.

## Status

The v4l2 libVA backend  currently is not in a usable state.
Development has ceased in 2019, before the V4L2-m2m interface has been stable.
Work on reviving the project is in progress. 

## Instructions

### V4L2
Your system's V4L2 capabilities can be interrogated with `v4l2-ctl`:
```
v4l2-ctl -d $LIBVA_V4L2_VIDEO_PATH {-l,--list-formats{,-out}}
```
In particular, your device needs to offer the "Video Memory-to-Memory Multiplanar", "Streaming", and "Device" capabilities.

### Environment Variables
Specify which V4L2 device is to be used:
- `LIBVA_V4L2_VIDEO_PATH=/dev/videoX`
- `LIBVA_V4L2_MEDIA_PATH=/dev/mediaY`

libVA can be instructed to load drivers from additional paths, and to fix the used driver if multiple are present:
- `LIBVA_DRIVERS_PATH=<project dir>/build/src`
- `LIBVA_DRIVER_NAME=v4l2`

When using gstreamer, opt-in to non-whitelisted drivers to be used:
- `GST_VAAPI_ALL_DRIVERS=1`
