# winlibs build tools
Tools for building [winlibs](https://winlibs.com/) packages from source using the MinGW-w64 GCC compiler for Windows.

These tools are needed to natively build winlibs packages from source on Windows.

## Goal

In order to build binary packages from source certain build and package management tools are needed.

The primary goal of [winlibs.com](https://winlibs.com/) is to build C/C++ source code on native Windows 32-bit and 64-bit platforms using [MinGW-w64](https://www.mingw-w64.org/) [GCC](https://gcc.gnu.org/).

The [MinGW-w64](https://www.mingw-w64.org/) [GCC (GNU Compiler Collection)](https://gcc.gnu.org/) itself and all of its dependencies built with these tools can be downloaded from [winlibs.com](https://winlibs.com/).

But these tools can be used to build thousands of other software packages for Windows.

This allows C/C++ developers to use existing software libraries with [MinGW-w64](https://www.mingw-w64.org/) [GCC (GNU Compiler Collection)](https://gcc.gnu.org/), which improves portability to/from other platforms, making it easier to use the same code for multiple platforms (e.g. Windows, Linux, macOS).

## Dependencies

 * [miniargv](https://github.com/brechtsanders/miniargv/)
 * [portcolcon](https://github.com/brechtsanders/portcolcon/)
 * [versioncmp](https://github.com/brechtsanders/versioncmp/)
 * [dirtrav](https://github.com/brechtsanders/libdirtrav/)
 * [crossrun](https://github.com/brechtsanders/crossrun/)
 * [pedeps](https://github.com/brechtsanders/pedeps/)
 * [pcre2_finder](https://github.com/brechtsanders/pcre2_finder/)
 * [avl](https://packages.debian.org/search?keywords=libavl-dev)
 * [libcurl](http://curl.haxx.se/libcurl/)
 * [gumbo-parser](https://github.com/google/gumbo-parser/)
 * [pcre2](http://www.pcre.org/)
 * [sqlite3](http://www.sqlite.org/)
 * [expat](http://www.libexpat.org/)
 * [libarchive](http://www.libarchive.org/)

## License

This software is distributed under the GPL-2.0 license.
