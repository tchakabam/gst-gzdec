
# GstGzDec - or how to make a GStreamer element for async processing of anything ...

## ... that ships in a GST plugin with autotools etc ...

## What & Why

This is an example GStreamer element. Why has it been published: Mainly to deliver a proof-of-concept around producing plugins to this framework - the ready-to-use way.

It's an example of how a GStreamer element will wrap some functionnality of existing C libraries X/Y/Z (in this case zlib and libbzip2) - and of a few more things.

Consider it like some sort of template that actually does something meaningful. It is a generic processing element with one sink and one src (single input/ouput, thus a typical "filter" from a systems POV). The way it has been implemented it can easily be recrafted by substituting parts easily to process anything (take some input, produce some output). The processing implementation has been well abstracted.

Furthermore it will allow non-blocking behaviour on both the upstream src-pad as well as not blocking it's on processing worker in case downstream pads are blocking at any point. This is achieved by double-queuing on pre-  and post processing data. The way it has been done here is consciously not using GStreamer queue elements as bin'd pipelines but to actually integrate this into the very elements functionnality. This makes us independent from GST's queue's implementation(s). Thus it might allow multi-threaded processing if the task is requiring it or takes any advantage from it.

Additionnaly the challenge given has been to allow compilation towards GStreamer 0.10 based legacy systems. This has been done in principle, but remains untested (some things might be broken at this level, but probably the fix is not too far as basic seperation of critical codepaths has been done).

## Features

* Double-queuing, non-blocking input, non-blockable output

* Easily craftable to any (multi-threaded) processing task

* Currenlty supports gzip and bzip streams. The zip stream-type is auto-detected based on the first two bytes (see function `stream_is_bzip`, `stream_is_bzip` and `setup_decoder` in the private functions declarations).

* Measures taken to allow compilation towards deprecated GStreamer 0.10 API, if you would wish to! However by default we use GStreamer 1.0. Using the deprecated 0.10 API is unrecommended.

## Usage

Currently on sytem install, the element bundled in this plugin will get registered as `gzdec` in the factory.

See the compilation section to move further and use the plugin.

## Compilation

To compile towards GStreamer 0.10 API set `USE_GSTREAMER_1_DOT_0_API` in `gstgzdec_compat.h` to `FALSE`. Also it will be necessary to set `USE_GSTATIC_REC_MUTEX` to `TRUE`. The latter can also be used with GStreamer 1.0 API but will produce deprecation warnings.

An autotools project is provided to check presence of necessary libraries and generate a Makefile for compilation and install.
	
To produce Makefile, compile and install run respectively:

```
./autogen.sh
make
sudo make install
```

Plugin will install to `/usr/local/lib/gstreamer-1.0` which may or may not be your default plugin dir. Eventually set `GST_PLUGIN_PATH` accordingly.

You will need compatible zlib (1.2.8) and libbzip2 (1.0.6) versions on your system. See notes in comments section further below on this topic.

## Source files

`gstgzdec.*` files contain the element declarations, plugin registration and implementations of `GstElement` and `GObject` class functions.

`gstgzdec_priv.h` contains private functions not directly called by the base class implementations.

`gstgzdec_bzipdecstream.h` and `gstgzdec_zipdecstream.h` contain a stream-like binding to the Gzip/Bzip lib (libbzip2 and zlib respectively) that provide an implementation of a generic decoding function which allows abstraction between the two formats.

`gstgzdec_compat.h` provides polyfill declarations to allow backward compatibility towards GStreamer 0.10 API.

`test.sh` is a shell script for producing test data and running pipelines with the two respective formats that produce output to validate behavior.

## Testing

### Acceptance test

When running `test.sh` two output files are produced from the respective two input gzip/bzip archives. These should be identical with the original uncompressed test file.

Priorly the presence of the plugin is checked with `gst-inspect-1.0` and test data is produced as described further on.

Logging is set to the DEBUG level (6) with `GST_DEBUG` for our element during test; and to WARNING (2) for all other debug categories. The scope of log can be modified in `test.sh` accordingly.

Very verbose output suitable for debugging will be obtained from the element when setting to TRACE level.

### How test data is produced

For gzip:

```
cat test/test.tiff | zlib-flate -compress > test/test.tiff.zip
```

For bzip:

```
cat test/test.tiff | bzip2 -zc > test/test.tiff.bzip
```

## Comments

In reference to the requirements, some notes:

* The Zlib version that is being checked against is set as "ZLIB_REQUIRED=1.2.8". We will require at least this version. This might need to be adapted if necessary on another system to compile, while API should be compatible.

* Same applies for the used bzip2 lib version on your system.

* Compatibility with the GStreamer 0.10 could not be tested on my system. However it has been taken care of to wrap or fill-in all the critical functions so that in principle against GStreamer 0.10 API it should work - but can't be guaranteed. The principle of a compile-time switch and necessary declarations to adapt the code to both APIs has been realized however.

* All the compatibility issues between the two APIs are handled inside the `gstgzdec_compat.h` file, except the ones related to buffer mapping/unmapping. The only solution here was to have compile-time switches in place where buffers are actually read. It would not be safe in principle re-implementing something such as 0.10's `GST_BUFFER_DATA` as we would have to return an unmapped buffer.

* It has also been tested that the pipeline does not stall in case of a file error (e.g wrong filename).

* We are currently not producing a proper Gzip archive as test data but a zlib format stream.

* When producing a gzip archive with the actual `gzip` utility, this wouldn't work here. ZLib's `inflate` will produce `Z_DATA_ERROR` (-3). This is odd as the window-parameter in the Zlib initialization used (32) should allow for any kind of zlib/gzip streams to work. See documentation about `inflateInit2` in http://zlib.net/manual.html. Possibly I am missing something here about how we initialize Zlib inflation, how or which data we provide, or not using `gzip` utility correctly to produce the correct test data.

* Linker arguments are not generated directly using `pkg-config` but set statically in the `src/Makefile.am` file under `libgstgzdec_la_LDFLAGS`. In actual Makefiles it is possible to do something such as `$(shell pkg-config --libs zlib)`, however autotools "am" files don't seem to support that. Help is welcome about how to actually use pkg-config inside autotools.

* Extensive documentation of functions has not been done as most of it seems self-explanatory. Some comments are provided in the code where it may be beneficial.

* We are checking for a specific Zlib version via `configure.ac` statements (via PKG_CHECK_MODULES). On my system it was not possible to do that for libbzip2, as the package was not recognized as a pkg-config module (missing .pc file?).


