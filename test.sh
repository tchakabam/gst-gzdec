#!/bin/sh

make clean
make && sudo make install

# FIXME: check if compile fails before starting this here

echo "\n\n\nRunning GzDec test suite now ->\n\n\n"
echo "gst-inspect check:\n"

export GST_PLUGIN_PATH=/usr/local/lib/gstreamer-1.0
export GST_DEBUG="*:2,gzdec:9"

gst-inspect-1.0 | grep gzdec

echo "\nLaunching pipeline:"

gst-launch-1.0 filesrc location=test.png ! gzdec ! filesink location=output.txt

echo "\n"