# avpack

avpack is a fast C library that can pack and unpack data to/from the popular multimedia container formats.

avpack code is header-only (`.h`-only) and doesn't need to be built into `.a/.so/.dll` before use - you just include `.h` file and that's all.

* Features
* How to use
* Test


## Features

* read/write meta tags, audio track info
* read/write audio frames
* convenient for asynchronous I/O model (no I/O callback functions)
* .avi read: `avpack/avi-read.h`
* .caf read: `avpack/caf-read.h`
* .mkv read: `avpack/mkv-read.h`
* .mp4/.m4a/.mov read/write: `avpack/mp4-read.h`, `avpack/mp4-write.h`
* .ogg/.opus read/write: `avpack/ogg-read.h`, `avpack/ogg-write.h`
* .wav read/write: `avpack/wav-read.h`, `avpack/wav-write.h`

It doesn't contain code that reads or writes files - this is user's responsibility.


## How to use

1. Clone repos:

		$ git clone https://github.com/stsaz/ffbase
		$ git clone https://github.com/stsaz/avpack

2. In your build script:

		-IFFBASE_DIR -IAVPACK_DIR

where `FFBASE_DIR` is your ffbase/ directory,
and `AVPACK_DIR` is your avpack/ directory.

3. And then just use the necessary files:

		#include <avpack/mp4-read.h>


## Test

	git clone https://github.com/stsaz/ffbase
	git clone https://github.com/stsaz/avpack
	cd avpack/test
	make
	./avpack-test all


## License

This code is absolutely free.
