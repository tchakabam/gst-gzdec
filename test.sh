#!/bin/sh

make clean
make && sudo make install

# FIXME: check if compile fails before starting this here

echo "\n\n\nRunning GzDec test suite now ->\n\n\n"
echo "gst-inspect check:\n"

export GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0
export GST_DEBUG="*:2,gzdec:6"

gst-inspect-1.0 | grep gzdec

echo "\nProducing test data":

cat test/test.tiff | zlib-flate -compress > test/test.tiff.zip
cat test/test.tiff | bzip2 -zc > test/test.tiff.bzip

echo "\nLaunching zlib pipeline:\n"

gst-launch-1.0 filesrc location=test/test.tiff.zip ! gzdec ! filesink location=test/test.out.zip.tiff

echo "\nLaunching bzip pipeline:\n"

gst-launch-1.0 filesrc location=test/test.tiff.bzip ! gzdec ! filesink location=test/test.out.bzip.tiff

echo "\n"

