# Testing
Set up [vaapi-fits](https://github.com/intel/vaapi-fits), e.g. in the local directory.
Setup venv with the local `requirements.txt` as well as those of `vaapi-fits`.
In addition to environment variables described on project level, set `VAAPI_FITS_CAPS=.` to use the custom V4L2 capabilities.
Run tests:
```
./vaapi-fits/vaapi-fits run --platform V4L2 test/gst-vaapi/decode test/ffmpeg-vaapi/decode
```
