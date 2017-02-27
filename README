
# GstGzDec 

## Usage

Supports gzip and bzip streams. The stream type is auto-detected based on the first two bytes. 

See function `stream_is_bzip`, `stream_is_bzip` and `setup_decoder` in the private functions declarations.

## Files

`gstgzdec.*` files contain the element declarations, plugin registration and implementations of `GstElement` and `GObject` class functions.

`gstgzdec_priv.h` contains private functions not directly called by the base class implementations.

`gstgzdec_bzipdecstream.h` and `gstgzdec_zipdecstream.h` contain a stream-like binding to the Gzip/Bzip lib (libbzip2 and zlib respectively) that provide an implementation of a generic decoding function which allows abstraction between the two formats.

`gstgzdec_compat.h` provides polyfill declarations to allow backward compatibility towards GStreamer 0.10 API.

`test.sh` is a shell script for producing test data and running pipelines with the two respective formats that produce output to validate behavior.

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


