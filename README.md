# GStreamer gzdec

## License

This code is provided under a MIT license [MIT], which basically means "do
with it as you wish, but don't blame us if it doesn't work". You can use
this code for any project as you wish, under any license as you wish. We
recommend the use of the LGPL [LGPL] license for applications and plugins,
given the minefield of patents the multimedia is nowadays. See our website
for details [Licensing].

## Dependencies
To install the plugin, you'll need to satisfy the following dependencies:

* [pkg-config](http://www.freedesktop.org/wiki/Software/pkg-config/)
* [zlib](http://zlib.net/)
* [bzip2](http://www.bzip.org/)
* [gstreamer](https://gstreamer.freedesktop.org/)
* [meson](https://mesonbuild.com/)

All of those libraries are usually available on the most common
distributions.

## Build
Use meson to setup and build project, from the project root directory call:

    meson setup builddir
    ninja -C builddir

To install plugin shared libs in gstreamer plugins directory, call:

    cd builddir
    meson install

Build tested on MacOS Sonoma and Debian 6.5.13-1.
## Test
After sucessfull build of a project the test executable should be located in a builddir, run it with single argument that is a path to some test file. Also make sure to specify GST_PLUGIN_PATH so that gstreamer can find a gzdec plugin, for example run:

    GST_PLUGIN_PATH=builddir/gst-plugin/ ./builddir/gst-plugin/gstgzdec_test ./COPYING.LIB

If test runs sucessfully it should exit with code 0.

## Usage
Run gzdec as part of pipeline using gst-launch-1.0, for example:

    gst-launch-0.10 filesrc location=file.txt.gz ! gzdec ! filesink location="file.txt"

In case that gst-launch-1.0 can not find the gzdec plugin make sure to specify the GST_PLUGIN_PATH to point to the libgstgzdec.dylib