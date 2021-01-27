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
* .mp4/.m4a/.mov read/write (`avpack/mp4read.h`, `avpack/mp4write.h`)

It doesn't contain code that reads or writes files - this is the responsibility of the user.


## How to use

1. Clone repos:

		$ git clone https://github.com/stsaz/ffbase
		$ git clone https://github.com/stsaz/avpack

2. In your build script:

		-IFFBASE_DIR -IAVPACK_DIR

where `FFBASE_DIR` is your ffbase/ directory,
and `AVPACK_DIR` is your avpack/ directory.

3. And then just use the necessary files:

		#include <avpack/mp4read.h>


## Test

	git clone https://github.com/stsaz/ffbase
	git clone https://github.com/stsaz/avpack
	cd avpack/test
	make
	./avpack-test all


## License

This code is absolutely free.
