ifeq ($(OS),)
OS = $(shell uname -s)
endif
PREFIX = /usr/local
CC   = gcc
CPP  = g++
AR   = ar
LIBPREFIX = lib
LIBEXT = .a
ifeq ($(OS),Windows_NT)
BINEXT = .exe
SOLIBPREFIX =
SOEXT = .dll
else ifeq ($(OS),Darwin)
BINEXT =
SOLIBPREFIX = lib
SOEXT = .dylib
else
BINEXT =
SOLIBPREFIX = lib
SOEXT = .so
endif
ifeq ($(OS),Darwin)
STRIPFLAG =
else
STRIPFLAG = -s
endif
MKDIR = mkdir -p
RM = rm -f
#RMDIR = rm -rf
RMDIR = rmdir
CP = cp -f
CPDIR = cp -rf
PKG_CONFIG = pkg-config

ifeq ($(OBJDIR),)
OBJDIR = .
endif
ifeq ($(BINDIR),)
BINDIR = .
endif

CFLAGS = 
LDFLAGS = 
ifdef STATIC
CFLAGS += -DSTATIC
LDFLAGS += -static
PKG_CONFIG += --static
endif
ifdef DEBUG
CFLAGS += -g
STRIPFLAG = 
else
CFLAGS += -O3
endif

MINIARGV_LDFLAGS = -lminiargv
PORTCOLCON_LDFLAGS = -lportcolcon
VERSIONCMP_LDFLAGS = -lversioncmp
CROSSRUN_LDFLAGS = -lcrossrun
PEDEPS_LDFLAGS = -lpedeps
AVL_LDFLAGS = -lavl
ifdef STATIC
#CURL_LDFLAGS = $(shell $(PKG_CONFIG) --libs libcurl librtmp libbrotlidec libgcrypt shishi gnutls libidn)
CURL_LDFLAGS = $(shell $(PKG_CONFIG) --static --libs libcurl librtmp libbrotlidec libgcrypt shishi gnutls libidn libntlm)
else
CURL_LDFLAGS = $(shell $(PKG_CONFIG) --libs libcurl)
endif
GUMBO_LDFLAGS = $(shell $(PKG_CONFIG) --libs gumbo)
PCRE2_LDFLAGS = $(shell $(PKG_CONFIG) --libs libpcre2-8)
ifdef STATIC
SQLITE3_LDFLAGS = $(shell $(PKG_CONFIG) --static --libs sqlite3)
else
SQLITE3_LDFLAGS = $(shell $(PKG_CONFIG) --libs sqlite3)
endif
EXPAT_LDFLAGS = $(shell $(PKG_CONFIG) --libs expat)
ifdef STATIC
LIBARCHIVE_LDFLAGS = $(shell $(PKG_CONFIG) --static --libs libarchive)
ifeq ($(OS),Windows_NT)
LIBARCHIVE_LDFLAGS += -liconv
endif
else
LIBARCHIVE_LDFLAGS = $(shell $(PKG_CONFIG) --libs libarchive)
endif
LIBDIRTRAV_LDFLAGS = -ldirtrav
PCRE2_FINDER_LDFLAGS = -lpcre2_finder
ifdef STATIC
PCRE2_FINDER_LDFLAGS += -lpcre2-8
endif
PTHREADS_LDFLAGS = -lpthread
ifdef STATIC
ifeq ($(OS),Windows_NT)
LDFLAGS += -Wl,--allow-multiple-definition
endif
endif

CFLAGS += -I/usr/local/include
#PORTCOLCON_LDFLAGS += -L/usr/local/lib
#VERSIONCMP_LDFLAGS += -L/usr/local/lib
#PEDEPS_LDFLAGS += -L/usr/local/lib
#CFLAGS += -I../portcolcon/include
#PORTCOLCON_LDFLAGS += -L../portcolcon
#CFLAGS += -I../versioncmp/include
#VERSIONCMP_LDFLAGS += -L../versioncmp

$(OBJDIR)/%.o: src/%.c objdir
	$(CC) -c -o $@ $< $(CFLAGS) 

UTILS_BIN = $(BINDIR)/wl-showstatus$(BINEXT) $(BINDIR)/wl-download$(BINEXT) $(BINDIR)/wl-wait4deps$(BINEXT) $(BINDIR)/wl-listall$(BINEXT) $(BINDIR)/wl-info$(BINEXT) $(BINDIR)/wl-showdeps$(BINEXT) $(BINDIR)/wl-checknewreleases$(BINEXT) $(BINDIR)/wl-makepackage$(BINEXT) $(BINDIR)/wl-install$(BINEXT) $(BINDIR)/wl-uninstall$(BINEXT) $(BINDIR)/wl-build$(BINEXT) $(BINDIR)/wl-find$(BINEXT)

.PHONY: all
all: $(UTILS_BIN)

.PHONY: objdir
objdir:
	$(MKDIR) $(OBJDIR)
	$(MKDIR) $(BINDIR)

$(BINDIR)/wl-showstatus$(BINEXT): $(OBJDIR)/wl-showstatus.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(PORTCOLCON_LDFLAGS)

$(BINDIR)/wl-download$(BINEXT): $(OBJDIR)/wl-download.o $(OBJDIR)/exclusive_lock_file.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(CURL_LDFLAGS)

$(BINDIR)/wl-wait4deps$(BINEXT): $(OBJDIR)/wl-wait4deps.o $(OBJDIR)/filesystem.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(PORTCOLCON_LDFLAGS)

$(BINDIR)/wl-listall$(BINEXT): $(OBJDIR)/wl-listall.o $(OBJDIR)/pkg.o $(OBJDIR)/pkgfile.o $(OBJDIR)/sorted_unique_list.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(VERSIONCMP_LDFLAGS) $(AVL_LDFLAGS)

$(BINDIR)/wl-info$(BINEXT): $(OBJDIR)/wl-info.o $(OBJDIR)/pkg.o $(OBJDIR)/pkgfile.o $(OBJDIR)/sorted_unique_list.o $(OBJDIR)/filesystem.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(VERSIONCMP_LDFLAGS) $(AVL_LDFLAGS)

$(BINDIR)/wl-showdeps$(BINEXT): $(OBJDIR)/wl-showdeps.o $(OBJDIR)/filesystem.o $(OBJDIR)/pkg.o $(OBJDIR)/pkgfile.o $(OBJDIR)/sorted_unique_list.o $(OBJDIR)/filesystem.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(VERSIONCMP_LDFLAGS) $(AVL_LDFLAGS)

$(BINDIR)/wl-checknewreleases$(BINEXT): $(OBJDIR)/wl-checknewreleases.o $(OBJDIR)/pkg.o $(OBJDIR)/pkgfile.o $(OBJDIR)/version_check_db.o $(OBJDIR)/common_output.o $(OBJDIR)/download_cache.o $(OBJDIR)/downloader.o $(OBJDIR)/memory_buffer.o $(OBJDIR)/sorted_unique_list.o $(OBJDIR)/sorted_item_queue.o $(OBJDIR)/filesystem.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(VERSIONCMP_LDFLAGS) $(AVL_LDFLAGS) $(CURL_LDFLAGS) $(GUMBO_LDFLAGS) $(PCRE2_LDFLAGS) $(SQLITE3_LDFLAGS) -pthread

$(BINDIR)/wl-makepackage$(BINEXT): $(OBJDIR)/wl-makepackage.o $(OBJDIR)/memory_buffer.o $(OBJDIR)/fstab.o $(OBJDIR)/filesystem.o $(OBJDIR)/sorted_unique_list.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(LIBDIRTRAV_LDFLAGS) $(LIBARCHIVE_LDFLAGS) $(PCRE2_FINDER_LDFLAGS) $(PEDEPS_LDFLAGS) $(AVL_LDFLAGS)

$(BINDIR)/wl-install$(BINEXT): $(OBJDIR)/wl-install.o $(OBJDIR)/filesystem.o $(OBJDIR)/memory_buffer.o $(OBJDIR)/sorted_unique_list.o $(OBJDIR)/pkg.o $(OBJDIR)/pkgdb.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(AVL_LDFLAGS) $(EXPAT_LDFLAGS) $(LIBARCHIVE_LDFLAGS) $(SQLITE3_LDFLAGS)

$(BINDIR)/wl-uninstall$(BINEXT): $(OBJDIR)/wl-uninstall.o $(OBJDIR)/filesystem.o $(OBJDIR)/memory_buffer.o $(OBJDIR)/sorted_unique_list.o $(OBJDIR)/pkg.o $(OBJDIR)/pkgdb.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(LIBDIRTRAV_LDFLAGS) $(AVL_LDFLAGS) $(LIBARCHIVE_LDFLAGS) $(SQLITE3_LDFLAGS)

$(BINDIR)/wl-build$(BINEXT): $(OBJDIR)/wl-build.o $(OBJDIR)/pkg.o $(OBJDIR)/pkgfile.o $(OBJDIR)/pkgdb.o $(OBJDIR)/memory_buffer.o $(OBJDIR)/sorted_unique_list.o $(OBJDIR)/filesystem.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(LIBDIRTRAV_LDFLAGS) $(VERSIONCMP_LDFLAGS) $(AVL_LDFLAGS) $(CROSSRUN_LDFLAGS) $(PTHREADS_LDFLAGS) $(SQLITE3_LDFLAGS)

$(BINDIR)/wl-find$(BINEXT): $(OBJDIR)/wl-find.o $(OBJDIR)/pkg.o $(OBJDIR)/pkgfile.o $(OBJDIR)/pkgdb.o $(OBJDIR)/memory_buffer.o $(OBJDIR)/sorted_unique_list.o $(OBJDIR)/filesystem.o
	$(CC) $(STRIPFLAG) $(LDFLAGS) -o $@ $^ $(MINIARGV_LDFLAGS) $(PORTCOLCON_LDFLAGS) $(AVL_LDFLAGS) $(SQLITE3_LDFLAGS)

.PHONY: install
install: all
	$(MKDIR) $(PREFIX)/bin
	$(CP) $(UTILS_BIN) $(PREFIX)/bin/

.PHONY: clean
clean:
	$(RM) $(UTILS_BIN) $(OBJDIR)/*.o
ifneq ($(OBJDIR),.)
	$(RMDIR) $(OBJDIR)
endif
ifneq ($(BINDIR),.)
	$(RMDIR) $(BINDIR)
endif

.PHONY: version
version:
	sed -ne "s/^#define\s*WINLIBS_VERSION_[A-Z]*\s*\([0-9]*\)\s*$$/\1./p" src/winlibs_common.h | tr -d "\n" | sed -e "s/\.$$//" > version

OSALIAS = $(OS)
ifeq ($(OS),Windows_NT)
ifneq (,$(findstring x86_64,$(shell $(CC) --version)))
OSALIAS := win64
else
OSALIAS := win32
endif
endif

COMMON_PACKAGE_FILES = README.md LICENSE Changelog.txt

.PHONY: binarypackage
binarypackage: version
ifneq ($(OS),Windows_NT)
	#$(MAKE) PREFIX=binarypackage_temp_$(OSALIAS) install
	#tar cfJ winlibs-tools-$(shell cat version)-$(OSALIAS).tar.xz --transform="s?^binarypackage_temp_$(OSALIAS)/??" $(COMMON_PACKAGE_FILES) binarypackage_temp_$(OSALIAS)/*
else
	$(MAKE) STATIC=1 BINDIR=binarypackage_temp_$(OSALIAS) OBJDIR=binarypackage_temp_$(OSALIAS)/tmp
	rm -rf binarypackage_temp_$(OSALIAS)/tmp
	cp -f $(COMMON_PACKAGE_FILES) binarypackage_temp_$(OSALIAS)/
	rm -f winlibs-tools-$(shell cat version)-$(OSALIAS).zip
	cd binarypackage_temp_$(OSALIAS) && zip -r9 ../winlibs-tools-$(shell cat version)-$(OSALIAS).zip $(COMMON_PACKAGE_FILES) * && cd ..
endif
	rm -rf binarypackage_temp_$(OSALIAS)

#ldconfig
##apt-get install pkg-config libavl-dev libcurl4-gnutls-dev libgumbo-dev libpcre2-dev libsqlite3-dev
#apt-get install pkg-config libavl-dev libcurl4-openssl-dev libgumbo-dev libpcre2-dev libsqlite3-dev
#cd /data/users/brecht/sources/CPP/wl-
#make clean && make all && valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./wl-checknewreleases -u /data/users/brecht/sources/porting_to_win32/build_scripts -v libdl
##nohup valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./wl-checknewreleases -u /data/users/brecht/sources/porting_to_win32/build_scripts -d linux_versioncheck.sq3 -vv -l output.txt -n2 -j20 all &
##nohup valgrind --leak-check=full --track-origins=yes ./wl-checknewreleases -u /data/users/brecht/sources/porting_to_win32/build_scripts -d linux_versioncheck.sq3 -vv -l output.txt -n3 -j20 all &                                           
##cd /data/users/brecht/sources/CPP/wl- && make clean && make all DEBUG=1 && rm -f output.txt && (echo Starting at: $(date)) > output.txt && ( nohup valgrind --leak-check=full --track-origins=yes ./wl-checknewreleases -u /data/users/brecht/sources/porting_to_win32/build_scripts -d linux_versioncheck.sq3 -v3 -l output.txt -n3 -j40 all & ); tail -f output.txt
##cd /data/users/brecht/sources/CPP/wl- && make clean && make all DEBUG=1 && rm -f output.txt && (echo Starting at: $(date)) > nohup.out && (echo Starting at: $(date)) > output.txt && ( nohup valgrind --leak-check=full --track-origins=yes ./wl-checknewreleases -u /data/users/brecht/sources/porting_to_win32/build_scripts -d linux_versioncheck.sq3 -c linux_cache.sq3 -v3 -l output.txt -o report.txt -n3 -j400 all & ); tail -f output.txt
#cd /data/users/brecht/sources/CPP/wl- && make clean && make all DEBUG=1 && rm -f output.txt && (echo Starting at: $(date)) > nohup.out && (echo Starting at: $(date)) > output.txt && ( nohup valgrind --leak-check=full --track-origins=yes ./wl-checknewreleases  -u /data/users/brecht/sources/porting_to_win32/build_scripts -d /data/users/brecht/sources/porting_to_win32/versioncheck.sq3 -o /data/users/brecht/sources/porting_to_win32/new_versions.txt -x 7200 -v0 -l /data/users/brecht/sources/porting_to_win32/new_versions.errors -j4 -n3 all & ); tail -f output.txt
#sqlite3 -header -column linux_versioncheck.sq3 "SELECT COUNT(basename) AS packages FROM package"
#sqlite3 -header -column linux_versioncheck.sq3 "SELECT basename, responsecode, status, DATETIME(updated, 'unixepoch', 'localtime') AS updated, DATETIME(created, 'unixepoch', 'localtime') AS created FROM package ORDER BY updated DESC LIMIT 10"
#sqlite3 -header -column linux_versioncheck.sq3 "SELECT basename, version, DATETIME(updated, 'unixepoch', 'localtime') AS updated, DATETIME(created, 'unixepoch', 'localtime') AS created FROM package_version ORDER BY created DESC LIMIT 10"
#sqlite3 -header -column linux_versioncheck.sq3 "SELECT basename, version, DATETIME(updated, 'unixepoch', 'localtime') AS updated, DATETIME(created, 'unixepoch', 'localtime') AS created FROM package_version WHERE updated = created ORDER BY created DESC LIMIT 20"
