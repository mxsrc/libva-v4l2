# V4L2 libVA Backend
This libVA backend is designed to work with the [Video for Linux Memory-To-Memory API](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-mem2mem.html) that is used by a number of video codecs drivers, in particular SoCs found on SBCs.
After an initial implementation by Bootlin, development on this backend ceased before the Stateless V4L2-M2M API it uses became stable.
Development has since been picked up again, see [#Status] for details.

## Building
The project is built using meson:
```
meson setup build
meson compile -Cbuild
```

## Usage
Applications using the backend can be launched by adding the build directory to the libVA driver path, and, optionally, setting the driver name to load:
```
LIBVA_DRIVERS_PATH=<project dir>/build/src LIBVA_DRIVER_NAME=v4l2 vainfo
```

Alternatively, the library can be installed to the default driver path:
```
meson install -Cbuild
```

The driver probes the system for appropriate V4L2 devices, advertising all of their capabilities.
This can be overriden by explicitly specifying a device pair to use:
```
export LIBVA_V4L2_VIDEO_PATH=/dev/videoX LIBVA_V4L2_MEDIA_PATH=/dev/mediaY
```

Note that some applications need further configuration to load the library.
In particular, gstreamer based applications have a whitelist for supported drivers, that can be disabled manually (`GST_VAAPI_ALL_DRIVERS=1`).

## Status
The project currently supports these codecs: MPEG2, H264, VP8, (and VP9).
VP9 support depends on a part of gstreamer that is not likely to be present in the version shipped by your distribution.
The implementation has been tested using Intel's [vaapi-fits](https://github.com/intel/vaapi-fits) on an RK3399, which is supported by the `hantro` and `rockchip` drivers.
Feedback on results for other platforms are very welcome, do not expect the library to simply work smoothly though.
Future development on this project aims to improve stability, add supported codecs, and support the stateful V4L2-M2M API.
