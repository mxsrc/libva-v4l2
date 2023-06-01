env = environment({
  'LIBVA_DRIVERS_PATH': meson.project_source_root()/'build/src',
  'LIBVA_DRIVER_NAME': 'v4l2_request',
  'LIBVA_V4L2_REQUEST_VIDEO_PATH': '/dev/video1',
  'LIBVA_V4L2_REQUEST_MEDIA_PATH': '/dev/media0',
  'GST_VAAPI_ALL_DRIVERS': '1',
})

vainfo = find_program('vainfo', required: false, disabler: true)
test(
  'vainfo',
  vainfo,
  args: ['--all'],
  env: env
)

# Running vaapi-fits depends on the environment providing its dependencies: tests should be run from within a python virtualenv (see `requirements.txt`)
vaapi_fits = find_program('vaapi-fits', dirs: [meson.current_source_dir()/'vaapi-fits'], required: false, disabler: true)
test(
  'ffmpeg decoding',
  vaapi_fits,
  workdir: meson.current_source_dir()/'vaapi-fits',
  args: ['run', 'test/ffmpeg-vaapi/decode', '--platform=V4L2'],
  env: env
)

test(
  'gstreamer decoding',
  vaapi_fits,
  workdir: meson.current_source_dir()/'vaapi-fits',
  args: ['run', 'test/gst-vaapi/decode', '--platform=V4L2'],
  env: env
)
