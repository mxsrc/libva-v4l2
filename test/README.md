# Testing
Ensure `git` and `git-lfs` are present.
Clone [https://github.com/intel/vaapi-fits]().
Setup venv with the requirements `vaapi-fits` requirements.
In addition to environment variables described on project level, set `VAAPI_FITS_CAPS=.` to use the custom V4L2 capabilities.
Run tests:
```
./vaapi-fits/vaapi-fits run --platform V4L2 test/gst-vaapi/decode test/ffmpeg-vaapi/decode
```
