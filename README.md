# winlibs build tools
Tools for building [winlibs](https://winlibs.com/) packages from source using the MinGW-w64 GCC compiler for Windows.

These tools are needed to natively build winlibs packages from source on Windows.

## Goal

In order to build binary packages from source certain build and package management tools are needed.

The primary goal of [winlibs.com](https://winlibs.com/) is to build C/C++ source code on native Windows 32-bit and 64-bit platforms using [MinGW-w64](https://www.mingw-w64.org/) [GCC](https://gcc.gnu.org/).

The [MinGW-w64](https://www.mingw-w64.org/) [GCC (GNU Compiler Collection)](https://gcc.gnu.org/) itself and all of its dependencies built with these tools can be downloaded from [winlibs.com](https://winlibs.com/).

But these tools can be used to build thousands of other software packages for Windows.

This allows C/C++ developers to use existing software libraries with [MinGW-w64](https://www.mingw-w64.org/) [GCC (GNU Compiler Collection)](https://gcc.gnu.org/), which improves portability to/from other platforms, making it easier to use the same code for multiple platforms (e.g. Windows, Linux, macOS).

These tools are intended to run on Windows, but the development of these tools was kept as platform independent as possible, in case there is also a future case for using them on other platforms.

## Recipe Format

A `.winlib` build recipe is written as a series of shell commands that can be run manually in the [MSYS2](https://www.msys2.org/) shell.
At the same time they contain fixed commands that allow the build tools to extract information and run them in an automated and unattended way.

## Package Format

Binary packages are compressed into `.7z` archives, so they can also be opened with [7-Zip](https://7-zip.org/).

A binary package file will have a name that consists of the package name, the version number and the architecture, followed by the `.7z` extension (`<name>-<version>-<arch>.7z`, e.g.: `libbz2-1.0.8.x86_64.7z`).

In the archive there will be a `.packageinfo.xml` file containing package metadata and a folder `.license` containing license file(s).

## Provided Tools

 * `wl-showstatus`: show status (used during build process)
 * `wl-download`: download file (used to download source code archives)
 * `wl-wait4deps`: wait for package dependencies
 * `wl-listall`: list available package recipes
 * `wl-info`: display package recipe information
 * `wl-showdeps`: display package dependency tree
 * `wl-checknewreleases`: check source code website(s) for new versions
 * `wl-makepackage`: create package file from isolated package install folder
 * `wl-install`: install package file
 * `wl-uninstall`: uninstall package(s)
 * `wl-build`: build package from source
 * `wl-find`: search in installed winlibs packages

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
