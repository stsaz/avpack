# avpack

avpack is a fast C library that can pack and unpack data to/from the popular multimedia container formats.

avpack code is header-only (`.h`-only) and doesn't need to be built into `.a/.so/.dll` before use - you just include `.h` file and that's all.

avpack is used by fmedia (`github.com/stsaz/fmedia`) - a fast media player/recorder/converter.
avpack is now the only suitable library for reading and writing multimedia files for the asynchronous and economical file I/O which fmedia implements.

Contents:

* [Features](#features)
* [How to use](#how-to-use)
* [Test](#test)


## Features

* read/write meta tags, audio track info
* read/write audio frames
* convenient for asynchronous I/O model (no I/O callback functions)

| Purpose | Include Files |
| --- | --- |
| **Audio file formats:** | |
|  .aac read | [aac-read.h](avpack/aac-read.h) |
|  .ape read | [ape-read.h](avpack/ape-read.h) |
|  .avi read | [avi-read.h](avpack/avi-read.h) |
|  .caf read | [caf-read.h](avpack/caf-read.h) |
|  .flac read | [flac-read.h](avpack/flac-read.h) |
|  .mkv read | [mkv-read.h](avpack/mkv-read.h) |
|  .mp3 read/write | [mp3-read.h](avpack/mp3-read.h), [mp3-write.h](avpack/mp3-write.h) |
|  .mp4/.m4a/.mov read/write | [mp4-read.h](avpack/mp4-read.h), [mp4-write.h](avpack/mp4-write.h) |
|  .mpc read | [mpc-read.h](avpack/mpc-read.h) |
|  .ogg(FLAC) read | [flac-ogg-read.h](avpack/flac-ogg-read.h) |
|  .ogg/.opus read/write | [ogg-read.h](avpack/ogg-read.h), [ogg-write.h](avpack/ogg-write.h) |
|  .ts read | [ts-read.h](avpack/ts-read.h) |
|  .wav read/write | [wav-read.h](avpack/wav-read.h), [wav-write.h](avpack/wav-write.h) |
|  .wv read/write | [wv-read.h](avpack/wv-read.h) |
| **Audio streams:** | |
|  ICY stream read | [icy.h](avpack/icy.h) |
|  MPEG-1 stream read | [mpeg1-read.h](avpack/mpeg1-read.h) |
| **Playlists:** | |
|  .cue read | [cue.h](avpack/cue.h) |
|  .m3u read/write | [m3u.h](avpack/m3u.h) |
|  .pls read | [pls.h](avpack/pls.h) |
| **MM Tags:** | |
|  APETAG read | [apetag.h](avpack/apetag.h) |
|  ID3v1 & ID3v2 read/write | [id3v1.h](avpack/id3v1.h), [id3v2.h](avpack/id3v2.h) |
|  Vorbis tags read/write | [vorbistag.h](avpack/vorbistag.h) |
| **Graphics:** | |
|  .bmp read/write | [bmp-read.h](avpack/bmp-read.h), [bmp-write.h](avpack/bmp-write.h) |
|  .jpg read | [jpg-read.h](avpack/jpg-read.h) |
|  .png read | [png-read.h](avpack/png-read.h) |

It doesn't contain code that reads or writes files - this is user's responsibility.


## How to use

1. Clone repos:

		git clone https://github.com/stsaz/ffbase
		git clone https://github.com/stsaz/avpack

2. Set compiler flags in your build script:

		-IFFBASE_DIR -IAVPACK_DIR

where `FFBASE_DIR` is your ffbase/ directory,
and `AVPACK_DIR` is your avpack/ directory.

3. And then just use the necessary files, e.g.:

		#include <avpack/mp4-read.h>
		#include <avpack/mp4-write.h>


### Reader interface

Each format reader has a similar set of functions:

* `mp4read_open()` - Open reader object
* `mp4read_close()` - Close reader object
* `mp4read_process()` - Process the input data supplied by user and return result:
	* `MP4READ_DATA` - User receives output data - a whole a/v frame
	* `MP4READ_MORE` - User must call the function again with the next chunk of input data
	* `MP4READ_HEADER` - File header is processed and avpack is ready to read a/v frames
	* `MP4READ_SEEK` - User must call the function again with a chunk of input data at the offset returned by `mp4read_offset()`
	* `MP4READ_DONE` - All data is processed
	* `MP4READ_ERROR` - An error is encountered during processing.  User may call `mp4read_error()` to get error message.
* `mp4read_seek()` - Begin the process of seeking to a specific audio position


### Writer interface

Each format writer has a similar set of functions:

* `mp4write_create` - Open writer object
* `mp4write_close` - Close writer object
* `mp4write_process` - Process the input data (a/v frame) supplied by user and return result:
	* `MP4WRITE_DATA` - User receives output data
	* `MP4WRITE_MORE` - User may call the function again with a new chunk of input data - a/v frame
	* `MP4WRITE_SEEK` - User must set the output file's position to the offset returned by `mp4write_offset()`
	* `MP4WRITE_DONE` - Output data is finalized
	* `MP4WRITE_ERROR` - An error is encountered during processing.  User may call `mp4write_error()` to get error message.


## Test

	git clone https://github.com/stsaz/ffbase
	git clone https://github.com/stsaz/avpack
	cd avpack/test
	make
	./avpack-test all


## License

This code is absolutely free.
