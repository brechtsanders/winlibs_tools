#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <miniargv.h>
#include <dirtrav.h>
#include <pcre2_finder.h>
#include <archive.h>
#include <archive_entry.h>
#ifndef _WIN32
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif
#ifndef NO_PEDEPS
#include <pedeps.h>
#endif
#include "winlibs_common.h"
#include "fstab.h"
#include "memory_buffer.h"
#ifndef NO_PEDEPS
#include "sorted_unique_list.h"
#endif

#define PCRE2_SUCCESS 0  ////

#define PROGRAM_NAME    "wl-makepackage"
#define PROGRAM_DESC    "Command line utility to create a package file"

#ifdef _WIN32
#define realpath(N,R) _fullpath((R),(N),_MAX_PATH)
#if defined(_WIN32) && !defined(__MINGW64_VERSION_MAJOR)
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif
#endif

//#define ARCHIVEFORMAT_ZIP
//#define ARCHIVEFORMAT_TAR
//#define ARCHIVEFORMAT_TAR_XZ
//#define ARCHIVEFORMAT_ZIP
//#define PACKAGE_EXTENSION ".zip"
#define ARCHIVEFORMAT_7Z
#define PACKAGE_EXTENSION ".7z"

#undef METADATA_CONTENTS
#undef CHECK_DEPENDANCIES
//#define CHECK_DEPENDANCIES

#ifdef CHECK_DEPENDANCIES
#include <pedeps.h>
#include "text_list.h"
#endif

struct packager_callback_struct {
  struct archive* arch;
  size_t level;
  int verbose;
  const char* srcdir;
  const char* dstdir;
  struct fstab_data_struct* fstabdata;
#ifndef NO_PEDEPS
  sorted_unique_list* ownpemodules;
  sorted_unique_list* pedeps;
  char* pelastmodule;
#endif
};

int add_folder_to_archive (const char* relativepath, struct packager_callback_struct* callbackdata)
{
  struct archive_entry *entry;
  char* path;
  size_t pathlen;
  //strip leading slash
  if (!relativepath || (pathlen = strlen(relativepath)) == 0)
    return 0;
  if (relativepath[pathlen - 1] == '/'
#ifdef _WIN32
      || relativepath[pathlen - 1] == '\\'
#endif
  )
    pathlen--;
  if ((path = (char*)malloc(pathlen + 1)) == NULL)
    return 0;
  memcpy(path, relativepath, pathlen);
  path[pathlen] = 0;
  //add directory to archive
  entry = archive_entry_new();
  archive_entry_set_pathname(entry, path);
  archive_entry_set_size(entry, 0);
  archive_entry_set_filetype(entry, AE_IFDIR);
  archive_entry_set_perm(entry, 0644);
  archive_write_header(callbackdata->arch, entry);
  //archive_entry_clear(entry); //needed if free follows???
  archive_entry_free(entry);
  //clean up
  free(path);
  return 1;
}

int add_file_to_archive (const char* fullpath, const char* relativepath, struct packager_callback_struct* callbackdata)
{
  int fd;
  int len;
  char buff[8192];
  struct stat st;
  struct archive_entry *entry;
  //get file information
  if (stat(fullpath, &st) != 0)
    return 0;
  //add file to archive
  entry = archive_entry_new();
  archive_entry_set_pathname(entry, relativepath);
  archive_entry_set_size(entry, st.st_size);      //required but not always known in advance
  archive_entry_set_filetype(entry, AE_IFREG);
  archive_entry_set_perm(entry, 0644);
  archive_write_header(callbackdata->arch, entry);
  if ((fd = open(fullpath, O_RDONLY | O_BINARY)) != -1) {
    len = read(fd, buff, sizeof(buff));
    while (len > 0) {
      archive_write_data(callbackdata->arch, buff, len);
      len = read(fd, buff, sizeof(buff));
    }
    close(fd);
  }
  //archive_entry_clear(entry); //needed if free follows???
  archive_entry_free(entry);
  return 1;
}

int add_memory_to_archive (const void* data, size_t datalen, const char* relativepath, struct packager_callback_struct* callbackdata)
{
  struct archive_entry *entry;
  //add file to archive
  entry = archive_entry_new();
  archive_entry_set_pathname(entry, relativepath);
  archive_entry_set_size(entry, datalen);
  archive_entry_set_filetype(entry, AE_IFREG);
  archive_entry_set_perm(entry, 0644);
  archive_write_header(callbackdata->arch, entry);
  archive_write_data(callbackdata->arch, data, datalen);
  //archive_entry_clear(entry); //needed if free follows???
  archive_entry_free(entry);
  return 1;
}

struct find_replace_callback_struct {
  struct pcre2_finder* finder;
  struct packager_callback_struct* folderinfo;
  int nextflags;
  int nextid;
};

#define PKG_REPLACE_INST_SH         0x01  //replace install paths in shell scripts
#define PKG_REPLACE_INST_REL        0x02  //replace install paths in libtool archives
#define PKG_REPLACE_INST_REL_PC     0x04  //replace install paths in pkg-config metadata (.pc) files (using ${pcfiledir})
#define PKG_REPLACE_INST_REL_CMAKE  0x08  //replace install paths in CMake (.cmake) files (using ${CMAKE_CURRENT_LIST_DIR})
#define PKG_REPLACE_DST_REL         0x10  //replace destination paths in libtool archives and pkg-config metadata files
#define PKG_REPLACE_LIB_PATH        0x20  //remove -L paths
#define PKG_REPLACE_LIB_ARG         0x40  //replace full library paths with -l arguments
#define PKG_REPLACE_LIBDIR          0x80  //comment out libdir=

typedef void (*path_callback_fn)(const char* path, void* callbackdata);

static void iterate_path_permutations (const char* path, path_callback_fn callbackfn, struct find_replace_callback_struct* callbackdata, int pcre2_flags, int pcre2_id)
{
  int i;
  char* s;
  char* p;
  size_t pathlen;
  if (!path || !*path)
    return;
  callbackdata->nextflags = pcre2_flags;
  callbackdata->nextid = pcre2_id;
  pathlen = strlen(path);
  //resolve path
  if (strstr(path, "/../") || (pathlen > 3 && strcmp(path + pathlen - 3, "/..") == 0)) {
    char full_path[PATH_MAX];
    if (realpath(path, full_path) && strcmp(full_path, path) != 0) {
      iterate_path_permutations(full_path, callbackfn, callbackdata, pcre2_flags, pcre2_id);
    }
  }
  //original path
  (*callbackfn)(path, callbackdata);
  //allocate temporary buffer
  if ((s = (char*)malloc(pathlen + 1)) == NULL)
    return;
  //notation with only backslashes and drive letter written as D: instead of /D
  for (i = 0; i <= pathlen; i++) {
    if (path[i] == '/')
      s[i] = '\\';
    else
      s[i] = path[i];
  }
  if (path[0] == '/' && ((s[1] >= 'A' && s[1] <= 'Z') || (s[1] >= 'a' && s[1] <= 'z')) && path[2] == '/') {
    s[0] = s[1];
    s[1] = ':';
  }
  if (strcmp(s, path) != 0)
    (*callbackfn)(s, callbackdata);

  //try to convert to mount point notation
  if ((p = fstabpath_from_fullpath(callbackdata->folderinfo->fstabdata, s)) != NULL) {
    //(*callbackfn)(p, callbackdata);
    iterate_path_permutations(p, callbackfn, callbackdata, pcre2_flags, pcre2_id);
    free(p);
  }

  //notation with only forward slashes
  for (i = 0; i < pathlen; i++) {
    if (s[i] == '\\')
      s[i] = '/';
  }
  if (strcmp(s, path) != 0)
    (*callbackfn)(s, callbackdata);

  //notation with only forward slashes and drive letter written as /D instead of D:
  if (((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) && s[1] == ':') {
    s[1] = s[0];
    s[0] = '/';
    if (strcmp(s, path) != 0)
      (*callbackfn)(s, callbackdata);
  }

#if 0
/////CRASHES, but not needed
  //try to convert from mount point notation
  if ((p = fstabpath_to_fullpath(callbackdata->folderinfo->fstabdata, s)) != NULL) {
printf("?%s\n",p);/////
    (*callbackfn)(p, callbackdata);
    free(p);
  }
#endif

  //notation with only forward slashes and drive letter written as D: instead of /D
  if (s[0] == '/' && ((s[1] >= 'A' && s[1] <= 'Z') || (s[1] >= 'a' && s[1] <= 'z')) && (s[2] == '/' || s[2] == 0)) {
    s[0] = s[1];
    s[1] = ':';
    if (strcmp(s, path) != 0)
      (*callbackfn)(s, callbackdata);
  }

  {
    char full_path[PATH_MAX];
    if (realpath(s, full_path) && strcmp(s, path) != 0) {
      //iterate_path_permutations(s, callbackfn, callbackdata, pcre2_flags, pcre2_id);
    }
  }

  //release temporary buffer
  free(s);
}

//find position of a string in a string starting from the end
int reverse_find_str_pos (const char* data, size_t datalen, const char* searchstr)
{
  int i;
  size_t searchstrlen = strlen(searchstr);
  if (datalen < searchstrlen)
    return -1;
  i = datalen - searchstrlen;
  while (strncasecmp(data + i, searchstr, searchstrlen) != 0) {
    if (i-- == 0)
      return -1;
  }
  return i;
}

/////typedef int (*pcre2_finder_match_fn)(struct pcre2_finder* finder, const char* data, size_t datalen, void* callbackdata, int matchid);
//static int package_file_match_found (unsigned int id, unsigned long long from, unsigned long long to, unsigned int flags, struct hs_finder* finder)
static int package_file_match_found (struct pcre2_finder* finder, const char* data, size_t datalen, void* pcallbackdata, int matchid)
{
  size_t i;
  struct find_replace_callback_struct* callbackdata = (struct find_replace_callback_struct*)pcallbackdata;
  switch (matchid) {
    case PKG_REPLACE_INST_SH :
      //replace with shell script path
      i = callbackdata->folderinfo->level;
      pcre2_finder_output(finder, "$(dirname $0)", 13);
      while (i-- > 0) {
        pcre2_finder_output(finder, "/..", 3);
      }
      break;
    case PKG_REPLACE_INST_REL :
      //replace with relative path
      i = callbackdata->folderinfo->level;
      while (i-- > 0) {
        pcre2_finder_output(finder, "..", 2);
        if (i > 0)
          pcre2_finder_output(finder, "/", 1);
      }
      break;
    case PKG_REPLACE_INST_REL_PC :
      //replace with relative path using pkg-config variable ${pcfiledir}
      i = callbackdata->folderinfo->level;
      pcre2_finder_output(finder, "${pcfiledir}", 12);
      while (i-- > 0) {
        pcre2_finder_output(finder, "/..", 3);
      }
      break;
    case PKG_REPLACE_INST_REL_CMAKE :
      //replace with relative path using CMake variable ${CMAKE_CURRENT_LIST_DIR}
      i = callbackdata->folderinfo->level;
      pcre2_finder_output(finder, "${CMAKE_CURRENT_LIST_DIR}", 25);
      while (i-- > 0) {
        pcre2_finder_output(finder, "/..", 3);
      }
      break;
    case PKG_REPLACE_DST_REL :
    case PKG_REPLACE_LIB_PATH :
      //remove unnecesary -L flag (but keep trailing single quote if present)
      if (datalen > 0 && data[datalen - 1] == '\'')
        pcre2_finder_output(finder, "'", 1);
      break;
    case PKG_REPLACE_LIB_ARG :
      {
        int i;
        int j;
        if ((j = reverse_find_str_pos(data, datalen, ".la")) >= 0) {
          if ((i = reverse_find_str_pos(data, j, "/lib")) >= 0) {
            i += 4;
            pcre2_finder_output(finder, "-l", 2);/////
            pcre2_finder_output(finder, data + i, j - i);/////
          }
        }
      }
      break;
    case PKG_REPLACE_LIBDIR :
      //replace absolute path with environment variable
      pcre2_finder_output(finder, "libdir='", 8);
      pcre2_finder_output(finder, "$MINGWPREFIX", 12);
      break;
    default :
//fprintf(stderr, "Invalid match id: %u\n", id);/////
      break;
  }
  return 0;
}

char* add_prefix_suffix (char** data, const char* prefix, const char* suffix)
{
  size_t datalen = (*data ? strlen(*data) : 0);
  size_t prefixlen = (prefix ? strlen(prefix) : 0);
  size_t suffixlen = (suffix ? strlen(suffix) : 0);
  if ((*data = (char*)realloc(*data, datalen + prefixlen + suffixlen + 1)) != NULL) {
    if (prefixlen) {
      if (datalen)
        memmove(*data + prefixlen, *data, datalen);
      memcpy(*data, prefix, prefixlen);
    }
    if (suffixlen)
      memcpy(*data + prefixlen + datalen, suffix, suffixlen);
    (*data)[prefixlen + datalen + suffixlen] = 0;
  }
  return *data;
}

static const char regex_special_characters[] = "\\.*?+[]|^$";

char* regex_escape_string (const char* data)
{
  char* result;
  size_t i = 0;
  size_t n = 0;
  //determine number of characters to quote
  while (data[i]) {
    if (strchr(regex_special_characters, data[i]))
      n++;
    i++;
  }
  //allocate data and fill with string with quotes characters
  if ((result = (char*)malloc(i + n + 1)) != NULL) {
    const char* p = data;
    char* q = result;
    while (*p) {
      if (strchr(regex_special_characters, *p))
        *q++ = '\\';
      *q++ = *p++;
    }
    *q = 0;
  }
  return result;
}

void inst_path_permutation_libdir_callback (const char* path, void* callbackdata)
{
  if (callbackdata && path) {
    char* expr;
    if ((expr = regex_escape_string(path)) != NULL) {
      if (add_prefix_suffix(&expr, "^libdir='", NULL) != NULL) {
/////DLL_EXPORT_PCRE2_FINDER int pcre2_finder_add_expr (struct pcre2_finder* finder, const char* expr, unsigned int flags, pcre2_finder_match_fn matchfn, void* callbackdata, int matchid);
        pcre2_finder_add_expr(((struct find_replace_callback_struct*)callbackdata)->finder, expr, ((struct find_replace_callback_struct*)callbackdata)->nextflags, package_file_match_found, (struct find_replace_callback_struct*)callbackdata, ((struct find_replace_callback_struct*)callbackdata)->nextid);
      }
      free(expr);
    }
  }
}

void inst_path_permutation_lib_arg_callback (const char* path, void* callbackdata)
{
  if (callbackdata && path) {
    char* expr;
    if ((expr = regex_escape_string(path)) != NULL) {
      //if (add_prefix_suffix(&expr, NULL, "/lib/lib[^ /]*\\.la") != NULL) {  /////crashes in Hyperscan (avoid slash in square brackets)
      if (add_prefix_suffix(&expr, NULL, "/lib/lib[^ ]*\\.la") != NULL) {
        pcre2_finder_add_expr(((struct find_replace_callback_struct*)callbackdata)->finder, expr, ((struct find_replace_callback_struct*)callbackdata)->nextflags, package_file_match_found, (struct find_replace_callback_struct*)callbackdata, ((struct find_replace_callback_struct*)callbackdata)->nextid);
      }
      free(expr);
    }
  }
}

void inst_path_permutation_lib_path_callback (const char* path, void* callbackdata)
{
  if (callbackdata && path) {
    char* expr;
    if ((expr = regex_escape_string(path)) != NULL) {
      if (add_prefix_suffix(&expr, "-L=?", "(/[^ /.]*/\\.\\.)?/lib[ ']") != NULL) {
        pcre2_finder_add_expr(((struct find_replace_callback_struct*)callbackdata)->finder, expr, ((struct find_replace_callback_struct*)callbackdata)->nextflags, package_file_match_found, (struct find_replace_callback_struct*)callbackdata, ((struct find_replace_callback_struct*)callbackdata)->nextid);
      }
      free(expr);
    }
  }
}

void inst_path_permutation_add_callback (const char* path, void* callbackdata)
{
  if (callbackdata && path) {
    char* expr;
    if ((expr = regex_escape_string(path)) != NULL) {
      pcre2_finder_add_expr(((struct find_replace_callback_struct*)callbackdata)->finder, expr, ((struct find_replace_callback_struct*)callbackdata)->nextflags, package_file_match_found, (struct find_replace_callback_struct*)callbackdata, ((struct find_replace_callback_struct*)callbackdata)->nextid);
      free(expr);
    }
  }
}

unsigned int* grow_uint_list (unsigned int** list, size_t oldsize, size_t newsize, unsigned int value)
{
  if (!list)
    return NULL;
  if ((*list = (unsigned int*)realloc(*list, newsize * sizeof(unsigned int))) != NULL) {
    size_t i;
    for (i = oldsize; i < newsize; i++)
      (*list)[i] = value;
  }
  return *list;
}

#define FILE_READ_BUFFER_SIZE 256

size_t pcre2_finder_output_to_filedescriptor (void* callbackdata, const char* data, size_t datalen)
{
  return write(*(int*)callbackdata, data, datalen);
}

int add_modified_file_to_archive (const char* fullpath, const char* relativepath, unsigned int replace_flags, struct packager_callback_struct* callbackdata)
{
  char* tempfilename;
  /////TO DO: use mkstemp()
#ifdef _WIN32
  if ((tempfilename = _tempnam(NULL, "pckgr")) == NULL) {
#else
  //if ((tempfilename = tempnam("/tmp", "pckgr")) == NULL) {
  if ((tempfilename = tempnam(NULL, "pckgr")) == NULL) {
#endif
    fprintf(stderr, "Error determining temporary file\n");
  } else {
    int src;
    int tmp;
    char buf[FILE_READ_BUFFER_SIZE];
    size_t buflen;
    if ((src = open(fullpath, O_RDONLY | O_BINARY)) != -1) {
      if ((tmp = open(tempfilename, O_CREAT | O_EXCL | O_WRONLY | O_BINARY, S_IRUSR | S_IWUSR)) == -1) {
        fprintf(stderr, "Error creating temporary file: %s\n", tempfilename);
        return 0;
      } else {
        struct find_replace_callback_struct pathcallbackdata;
        //initialize find/replace
        pathcallbackdata.finder = pcre2_finder_initialize(package_file_match_found, &pathcallbackdata);
        pathcallbackdata.folderinfo = callbackdata;
        pathcallbackdata.nextflags = 0;
        pathcallbackdata.nextid = 0;
        if (replace_flags & PKG_REPLACE_LIBDIR) {
          //add libdir= to be replaced
          pcre2_finder_add_expr(pathcallbackdata.finder, "^libdir='\\.\\.(/\\.\\.)*", PCRE2_MULTILINE | PCRE2_CASELESS, package_file_match_found, (struct find_replace_callback_struct*)callbackdata, PKG_REPLACE_LIBDIR);
          iterate_path_permutations(callbackdata->srcdir, inst_path_permutation_libdir_callback, &pathcallbackdata, PCRE2_MULTILINE | PCRE2_CASELESS, replace_flags & PKG_REPLACE_LIBDIR);
        }
        if (replace_flags & PKG_REPLACE_LIB_ARG) {
          //add installation paths to be replaced
          pcre2_finder_add_expr(pathcallbackdata.finder, "\\$MINGWPREFIX/lib/lib[^ ]*\\.la", PCRE2_MULTILINE, package_file_match_found, (struct find_replace_callback_struct*)callbackdata, PKG_REPLACE_LIB_ARG);
          iterate_path_permutations(callbackdata->srcdir, inst_path_permutation_lib_arg_callback, &pathcallbackdata, PCRE2_MULTILINE | PCRE2_CASELESS, replace_flags & PKG_REPLACE_LIB_ARG);
          iterate_path_permutations(callbackdata->dstdir, inst_path_permutation_lib_arg_callback, &pathcallbackdata, PCRE2_MULTILINE | PCRE2_CASELESS, replace_flags & PKG_REPLACE_LIB_ARG);
        }
        if (replace_flags & PKG_REPLACE_LIB_PATH) {
          //add library include paths to be removed
          pcre2_finder_add_expr(pathcallbackdata.finder, "/bin/\\.\\.", PCRE2_MULTILINE, package_file_match_found, (struct find_replace_callback_struct*)callbackdata, PKG_REPLACE_LIB_PATH);
          pcre2_finder_add_expr(pathcallbackdata.finder, "-L=?/?(\\.\\./|lib/)*lib[ ']|-L=?/(mingw|usr/local|usr)/lib[ ']|-L=?[^ ]*/\\.libs[ ']", PCRE2_MULTILINE, package_file_match_found, (struct find_replace_callback_struct*)callbackdata, PKG_REPLACE_LIB_PATH);
          iterate_path_permutations(callbackdata->srcdir, inst_path_permutation_lib_path_callback, &pathcallbackdata, PCRE2_MULTILINE, replace_flags & PKG_REPLACE_LIB_PATH);
          iterate_path_permutations(callbackdata->dstdir, inst_path_permutation_lib_path_callback, &pathcallbackdata, PCRE2_MULTILINE, replace_flags & PKG_REPLACE_LIB_PATH);
        }
        if (replace_flags & (PKG_REPLACE_INST_SH | PKG_REPLACE_INST_REL | PKG_REPLACE_INST_REL_PC | PKG_REPLACE_INST_REL_CMAKE)) {
          //add installation paths to be replaced
          iterate_path_permutations(callbackdata->srcdir, inst_path_permutation_add_callback, &pathcallbackdata, PCRE2_MULTILINE | PCRE2_CASELESS, replace_flags & (PKG_REPLACE_INST_SH | PKG_REPLACE_INST_REL | PKG_REPLACE_INST_REL_PC | PKG_REPLACE_INST_REL_CMAKE));
        }
        if (replace_flags & PKG_REPLACE_DST_REL) {
          //add run paths to be replaced
          iterate_path_permutations(callbackdata->dstdir, inst_path_permutation_add_callback, &pathcallbackdata, PCRE2_MULTILINE | PCRE2_CASELESS, replace_flags & PKG_REPLACE_DST_REL);
        }
        //process data
        if (pcre2_finder_open(pathcallbackdata.finder, pcre2_finder_output_to_filedescriptor, &tmp) == PCRE2_SUCCESS) {
          int status;
          //process data and write result
          while ((buflen = read(src, buf, sizeof(buf))) > 0) {
            if ((status = pcre2_finder_process(pathcallbackdata.finder, buf, buflen)) != PCRE2_SUCCESS)
              break;
          }
          pcre2_finder_close(pathcallbackdata.finder);
        }
        //clean up
        pcre2_finder_cleanup(pathcallbackdata.finder);
        close(tmp);
        //add modified file
        if (add_file_to_archive(tempfilename, relativepath, callbackdata) == 0) {
          fprintf(stderr, "Error adding file: %s\n", relativepath);
          return 0;
        }
        //clean up temporary file
        unlink(tempfilename);
      }
      close(src);
    }
    free(tempfilename);
  }
  return 1;
}

#ifndef NO_PEDEPS
int check_pe_dependencies (const char* modulename, const char* functionname, void* callbackdata)
{
  struct packager_callback_struct* data = (struct packager_callback_struct*)callbackdata;
  if (!data->pelastmodule || strcasecmp(modulename, data->pelastmodule) != 0) {
    if (data->pelastmodule)
      free(data->pelastmodule);
    data->pelastmodule = strdup(modulename);
    sorted_unique_list_add(data->pedeps, modulename);
  }
  return 0;
}

int check_pe_dependencies_callback (dirtrav_entry info)
{
  struct packager_callback_struct* callbackdata = (struct packager_callback_struct*)info->callbackdata;
  const char* srcfile = dirtrav_prop_get_path(info);
  const char* ext = dirtrav_prop_get_extension(info);
  //add any other file without changes
  if (callbackdata->pedeps && ext && (strcasecmp(ext, ".exe") == 0 || strcasecmp(ext, ".dll") == 0)) {
    pefile_handle pehandle;
    //add PE file to list of own modules
    if (strcasecmp(ext, ".exe") != 0) {
      sorted_unique_list_add(callbackdata->ownpemodules, dirtrav_prop_get_name(info));
    }
    //check PE file for dependencies
    if ((pehandle = pefile_create()) != NULL) {
      if ((pefile_open_file(pehandle, srcfile)) == 0) {
        pefile_list_imports(pehandle, check_pe_dependencies, callbackdata);
        pefile_close(pehandle);
      }
      pefile_destroy(pehandle);
    }
  }
  return 0;
}
#endif

int packager_file_callback (dirtrav_entry info)
{
  struct packager_callback_struct* callbackdata = (struct packager_callback_struct*)info->callbackdata;
  const char* srcfile = dirtrav_prop_get_path(info);
  const char* path = dirtrav_prop_get_relative_path(info);
  const char* ext = dirtrav_prop_get_extension(info);
  size_t pathlen = (path ? strlen(path) : 0);
  int status = 0;
  if (ext && strcasecmp(ext, ".la") == 0) {
    //replace absolute install path with relative path in .la files
    if (callbackdata->verbose) {
      printf("Adding customized file: %s\n", path);
      fflush(stdout);
    }
    status = add_modified_file_to_archive(srcfile, path, PKG_REPLACE_INST_REL | PKG_REPLACE_DST_REL | PKG_REPLACE_LIB_PATH | PKG_REPLACE_LIB_ARG | PKG_REPLACE_LIBDIR, callbackdata);
  } else if (ext && strcasecmp(ext, ".pc") == 0) {
    //replace absolute install path with relative path in .pc files
    if (callbackdata->verbose) {
      printf("Adding customized file: %s\n", path);
      fflush(stdout);
    }
    status = add_modified_file_to_archive(srcfile, path, PKG_REPLACE_INST_REL_PC | PKG_REPLACE_DST_REL | PKG_REPLACE_LIB_PATH | PKG_REPLACE_LIB_ARG | PKG_REPLACE_LIBDIR, callbackdata);
  } else if (ext && strcasecmp(ext, ".cmake") == 0) {
    //replace absolute install path with relative path in .cmake files
    if (callbackdata->verbose) {
      printf("Adding customized file: %s\n", path);
      fflush(stdout);
    }
    status = add_modified_file_to_archive(srcfile, path, PKG_REPLACE_INST_REL_CMAKE, callbackdata);
  } else if (pathlen > 7 && strcasecmp(path + pathlen - 7, "-config") == 0) {
    //replace absolute install path with relative path in *-config scripts
    if (callbackdata->verbose) {
      fflush(stdout);
    }
    status = add_modified_file_to_archive(srcfile, path, PKG_REPLACE_INST_SH, callbackdata);
  } else {
    //add any other file without changes
/*
#ifndef NO_PEDEPS
    if (callbackdata->pedeps && ext && (strcasecmp(ext, ".exe") == 0 || strcasecmp(ext, ".dll") == 0)) {
      pefile_handle pehandle;
      //add PE file to list of own modules
      if (strcasecmp(ext, ".exe") != 0) {
        sorted_unique_list_add(callbackdata->ownpemodules, dirtrav_prop_get_name(info));
      }
      //check PE file for dependencies
      if ((pehandle = pefile_create()) != NULL) {
        if ((pefile_open_file(pehandle, srcfile)) == 0) {
          pefile_list_imports(pehandle, check_pe_dependencies, callbackdata);
          pefile_close(pehandle);
        }
        pefile_destroy(pehandle);
      }
    }
#endif
*/
    if (callbackdata->verbose) {
      printf("Adding file: %s\n", path);
      fflush(stdout);
    }
    status = add_file_to_archive(srcfile, path, callbackdata);
  }
  if (status == 0) {
    fprintf(stderr, "Error adding file: %s\n", path);
    fflush(stdout);
    return 1;
  }
  return 0;
}

int packager_before_folder_callback (dirtrav_entry info)
{
  struct packager_callback_struct* callbackdata = (struct packager_callback_struct*)info->callbackdata;
  //create folder
  add_folder_to_archive(dirtrav_prop_get_relative_path(info), callbackdata);
  //keep track of nesting level
  ((struct packager_callback_struct*)info->callbackdata)->level++;
  return 0;
}

int packager_after_folder_callback (dirtrav_entry info)
{
  //keep track of nesting level
  ((struct packager_callback_struct*)info->callbackdata)->level--;
  return 0;
}

int delete_file_callback (dirtrav_entry info)
{
#ifdef _WIN32
  if (!DeleteFileA(dirtrav_prop_get_path(info))) {
/*
  const char* fullpath = dirtrav_prop_get_path(info);
  size_t fullpathlen = strlen(fullpath);
  char* specialpath = (char*)malloc(5 + fullpathlen);
  memcpy(specialpath, "\\\\?\\", 4);
  strcpy(specialpath + 4, fullpath);
  if (!DeleteFileA(specialpath)) {
    char* errmsg;
    DWORD errorcode = GetLastError();
    if (FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, errorcode, 0, (CHAR*)&errmsg, 0, NULL)) {
      fprintf(stderr, "Error %lu deleting %s: %s\n", (unsigned long)errorcode, specialpath, errmsg);
      LocalFree(errmsg);
    }
  }
*/
#else
  if (unlink(dirtrav_prop_get_path(info)) != 0) {
#endif
    ++*(size_t*)info->callbackdata;
    fprintf(stderr, "Error deleting file: %s\n", dirtrav_prop_get_path(info));
  }
  return 0;
}

int delete_after_folder_callback (dirtrav_entry info)
{
#ifdef _WIN32
  if (!RemoveDirectoryA(dirtrav_prop_get_path(info))) {
#else
  if (rmdir(dirtrav_prop_get_path(info)) != 0) {
#endif
    ++*(size_t*)info->callbackdata;
    fprintf(stderr, "Error deleting folder: %s\n", dirtrav_prop_get_path(info));
  }
  return 0;
}

#ifdef METADATA_CONTENTS
int packagelist_file_callback (dirtrav_entry info)
{
  txtbuf* t = memory_buffer_create();
  memory_buffer_append_printf((txtbuf*)info->callbackdata, "\t<file name=\"%s\" size=\"%" PRIu64 "\" extension=\"%s\"/>\n", memory_buffer_get(memory_buffer_xml_special_chars(memory_buffer_set(t, dirtrav_prop_get_relative_path(info)))), dirtrav_prop_get_size(info), dirtrav_prop_get_extension(info));
  memory_buffer_destroy(t);
  return 0;
}

int packagelist_before_folder_callback (dirtrav_entry info)
{
  txtbuf* t = memory_buffer_create();
  memory_buffer_append_printf((txtbuf*)info->callbackdata, "\t<directory name=\"%s\"/>\n", memory_buffer_get(memory_buffer_xml_special_chars(memory_buffer_set(t, dirtrav_prop_get_relative_path(info)))));
  memory_buffer_destroy(t);
  return 0;
}
#endif

////////////////////////////////////////////////////////////////////////

struct strings_linked_list {
  char* data;
  struct strings_linked_list* next;
};

struct strings_linked_list* strings_linked_list_append (struct strings_linked_list** list, const char* data)
{
  struct strings_linked_list** p = list;
  if (!p)
    return NULL;
  while (*p) {
    p = &((*p)->next);
  }
  if ((*p = (struct strings_linked_list*)malloc(sizeof(struct strings_linked_list))) == NULL) {
    return NULL;
  }
  if (((*p)->data = strdup(data)) == NULL) {
    free(*p);
    return NULL;
  }
  (*p)->next = NULL;
  return *p;
};

void strings_linked_list_free (struct strings_linked_list** list)
{
  struct strings_linked_list* p;
  struct strings_linked_list* q;
  if (list)
    return;
  p = *list;
  while (p) {
    q = p->next;
    free(p->data);
    free(p);
    p = q;
  }
  *list = NULL;
};

////////////////////////////////////////////////////////////////////////

#ifdef CHECK_DEPENDANCIES
struct get_dependencies_imports {
  pefile_handle pehandle;
  char* lastdependency;
  struct text_list_struct* dependencies;
  struct text_list_struct* binaries;
};

int get_dependencies_imports (const char* modulename, const char* functionname, void* callbackdata)
{
  struct get_dependencies_imports* depsdata = (struct get_dependencies_imports*)callbackdata;
  //add dependency to list if it was different than the previous one
  if (depsdata->lastdependency == NULL || strcmp(modulename, depsdata->lastdependency) != 0) {
    if (depsdata->lastdependency)
      free(depsdata->lastdependency);
    depsdata->lastdependency = strdup(modulename);
    text_list_add(&(depsdata->dependencies), modulename);
  }
  return 0;
}

int dependencies_file_callback (dirtrav_entry info)
{
  struct get_dependencies_imports* depsdata = (struct get_dependencies_imports*)info->callbackdata;
  if (pefile_open_file(depsdata->pehandle, dirtrav_prop_get_path(info)) == PE_RESULT_SUCCESS) {
    //add file to list of binaries
    text_list_add(&(depsdata->binaries), dirtrav_prop_get_name(info));
    //add dependencies to list of dependencies
    pefile_list_imports(depsdata->pehandle, get_dependencies_imports, depsdata);
    pefile_close(depsdata->pehandle);
  }
  return 0;
}

char* find_file_in_path_list (const char* filename, struct strings_linked_list* pathlist)
{
  struct stat st;
  char* fullpath;
  size_t pathlen;
  size_t filenamelen = (filename ? strlen(filename) : 0);
  struct strings_linked_list* p = pathlist;
  while (p) {
    pathlen = strlen(p->data);
    if ((fullpath = (char*)malloc(pathlen + filenamelen + 2))) {
      memcpy(fullpath, p->data, pathlen);
#ifdef _WIN32
      fullpath[pathlen] = '\\';
#else
      fullpath[pathlen] = '/';
#endif
      strcpy(fullpath + pathlen + 1, filename);
      if (stat(fullpath, &st) == 0 && (st.st_mode & S_IFREG) != 0)
        return fullpath;
      free(fullpath);
    }
    p = p->next;
  }
  return NULL;
}

int list_dependencies (const struct text_list_struct* textlist, void* callbackdata)
{
  char* fullpath;
  struct strings_linked_list* dstpath = (struct strings_linked_list*)callbackdata;
  fullpath = find_file_in_path_list(textlist->text, dstpath);
  printf("%c %s\n", (fullpath ? '+' : '-'), textlist->text);
  free(fullpath);
  return 0;
}
#endif

typedef int (*iterate_path_list_callback_fn)(const char* path, void* callbackdata);

size_t iterate_path_list (const char* pathlist, char pathseparator, iterate_path_list_callback_fn callbackfunction, void* callbackdata)
{
  char* path;
  int status;
  size_t count = 0;
  size_t startpos = 0;
  size_t endpos = 0;
  if (pathseparator == 0) {
#ifdef _WIN32
    pathseparator = ';';
#else
    pathseparator = ':';
#endif
  }
  while (pathlist[startpos]) {
    endpos = startpos;
    while (pathlist[endpos] && pathlist[endpos] != pathseparator) {
      endpos++;
    }
    if (endpos > startpos && (path = (char*)malloc(endpos - startpos + 1)) != NULL) {
      memcpy(path, pathlist + startpos, endpos - startpos);
      path[endpos - startpos] = 0;
      status = (*callbackfunction)(path, callbackdata);
      free(path);
      if (status != 0)
        break;
    }
    startpos = endpos;
    while (pathlist[startpos] && pathlist[startpos] == pathseparator)
      startpos++;
  }
  return count;
}

int is_in_path (const char* path1, const char* path2)
{
  char full_path1[PATH_MAX];
  char full_path2[PATH_MAX];
  if (realpath(path1, full_path1) && realpath(path2, full_path2)) {
    size_t len1 = strlen(full_path1);
    size_t len2 = strlen(full_path2);
    if (len1 <= len2) {
      if (strncmp(full_path1, full_path2, len1) == 0) {
      if (full_path2[len1] == 0 || full_path2[len1] == '/'
#ifdef _WIN32
          || path2[len1] == '\\'
#endif
      )
        return 1;
      }
    }
  }
  return 0;
}

struct check_if_installation_path_struct {
  const char* dstdir;
  struct strings_linked_list** dst_paths;
};

int check_if_installation_path (const char* path, void* callbackdata)
{
  struct check_if_installation_path_struct* data = (struct check_if_installation_path_struct*)callbackdata;
  if (is_in_path(data->dstdir, path) != 0)
    strings_linked_list_append(data->dst_paths, path);
  return 0;
}

void csv_entries_append (struct memory_buffer* metadata, const char* list, const char* prefix, const char* suffix)
{
  const char* p;
  const char* q;
  p = list;
  while (p && *p) {
    q = p;
    while (*q && *q != ',')
      q++;
    if (q > p) {
      memory_buffer_append(metadata, prefix);
      memory_buffer_append_buf(metadata, p, q - p);
      memory_buffer_append(metadata, suffix);
    }
    p = q;
    if (*p)
      p++;
  }
}

struct metadata_envvar_info_struct {
  const char* envvar;
  const char* xmlattr;
};

struct metadata_envvar_info_struct metadata_envvar_info[] = {
  {"NAME", "name"},
  {"DESCRIPTION", "description"},
  {"URL", "url"},
  {"DOWNLOADURL", "downloadurl"},
  {"DOWNLOADSOURCEURL", "downloadsourceurl"},
  {"CATEGORY", "category"},
  {"TYPE", "type"},
  {"VERSIONDATE", "versiondate"},
/*
  {"DEPENDENCIES", "dependencies"},
  {"DEPENDANCIES", "dependencies"},
  {"OPTIONALDEPENDENCIES", "optionaldependencies"},
  {"OPTIONALDEPENDANCIES", "optionaldependencies"},
  {"BUILDDEPENDENCIES", "builddependencies"},
  {"BUILDDEPENDANCIES", "builddependencies"},
*/
  {"LICENSEFILE", "licensefile"},
  {"LICENSETYPE", "licensetype"},
  {"STATUS", "status"},
  {NULL, NULL}
};

int main (int argc, char** argv, char *envp[])
{
  //process command line parameters
  int i;
  char* p;
  int showhelp = 0;
  const char* packagename = NULL;
  const char* packageversion = NULL;
  char* arch = NULL;
  char* srcdir = NULL;
  char* dstdir = NULL;
  char* pkgdir = NULL;
  char* fstabpath = NULL;
  const char* licfile = NULL;
  struct fstab_data_struct* fstabdata = NULL;
#ifndef NO_PEDEPS
  int checkdeps = 0;
#endif
  int deleteafter = 0;
  int verbose = 0;
  struct strings_linked_list* dst_paths = NULL;
  char* packagefilename = NULL;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",            NULL,      miniargv_cb_increment_int, &showhelp,        "show command line help"},
    {'s', "source",          "PATH",    miniargv_cb_strdup,        &srcdir,          "path containing installed package contents\noverrides environment variable INSTALLPREFIX"},
    {'p', "package-path",    "PATH",    miniargv_cb_strdup,        &pkgdir,          "path where package files are stored\noverrides environment variable PACKAGEDIR"},
    {'i', "install-path",    "PATH",    miniargv_cb_strdup,        &dstdir,          "package installation path\noverrides environment variable MINGWPREFIX"},
    {'f', "fstab",           "PATH",    miniargv_cb_strdup,        &fstabpath,       "full path of specific fstab file (used for resolving MSYS paths)"},
    {'n', "package-version", "VERSION", miniargv_cb_set_const_str, &packageversion,  "package version number (defaults to $VERSION)"},
    {'a', "arch",            "ARCH",    miniargv_cb_strdup,        &arch,            "target architecture (i686 / x86_64)\noverrides environment variable RUNPLATFORM"},
    {'l', "license",         "FILE",    miniargv_cb_set_const_str, &licfile,         "relative path of license file\noverrides environment variable LICENSEFILE"},
#ifndef NO_PEDEPS
    {'c', "dependencies",    NULL,      miniargv_cb_increment_int, &checkdeps,       "check for DLL dependencies and list them"},
#endif
    {'d', "delete",          NULL,      miniargv_cb_increment_int, &deleteafter,     "delete original package content files after creating package"},
    {'v', "verbose",         NULL,      miniargv_cb_increment_int, &verbose,         "verbose mode"},
    {0,   NULL,              "PACKAGE", miniargv_cb_set_const_str, &packagename,     "package name\noverrides environment variable BASENAME"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "INSTALLPREFIX",   NULL,      miniargv_cb_strdup,        &srcdir,          "path containing installed package contents"},
    {0,   "BASENAME",        NULL,      miniargv_cb_set_const_str, &packagename,     "package name"},
    {0,   "VERSION",         NULL,      miniargv_cb_set_const_str, &packageversion,  "package version number"},
    {0,   "PACKAGEDIR",      NULL,      miniargv_cb_strdup,        &pkgdir,          "path where package files are stored"},
    {0,   "MINGWPREFIX",     NULL,      miniargv_cb_strdup,        &dstdir,          "package installation path"},
    {0,   "RUNPLATFORM",     NULL,      miniargv_cb_strdup,        &arch,            "target architecture (i686-w64-mingw32 / x86_64-w64-mingw32)\nonly the part up to the first \"-\" is used"},
    {0,   "LICENSEFILE",     NULL,      miniargv_cb_set_const_str, &licfile,         "relative path of license file"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //parse environment and command line flags
  if (miniargv_process(argv, envp, argdef, envdef, NULL, NULL) != 0)
    return 1;
  //show help if requested
  if (showhelp) {
    printf(
      PROGRAM_NAME " - Version " WINLIBS_VERSION_STRING " - " WINLIBS_LICENSE " - " WINLIBS_CREDITS "\n"
      PROGRAM_DESC "\n"
      "Usage: " PROGRAM_NAME " "
    );
    miniargv_arg_list(argdef, 1);
    printf("\n");
    miniargv_help(argdef, envdef, 32, 0);
#ifdef PORTCOLCON_VERSION
    printf(WINLIBS_HELP_COLOR);
#endif
    return 0;
  }
  //check parameters
  if ((i = miniargv_get_next_arg_param(0, argv, argdef, NULL)) > 0) {
    if (miniargv_get_next_arg_param(i, argv, argdef, NULL) > 0) {
      fprintf(stderr, "Only one package name allowed\n");
      return 1;
    }
  }
  if (!packagename || !*packagename) {
    fprintf(stderr, "Missing package name ($BASENAME or command line parameter)\n");
    return 2;
  }
  if (!packageversion || !*packageversion) {
    fprintf(stderr, "Missing version number ($VERSION or -n parameter)\n");
    return 3;
  }
  if (!arch || !*arch) {
    fprintf(stderr, "Missing target architecture ($RUNPLATFORM or -a parameter)\n");
    return 4;
  } else if ((p = strchr(arch, '-')) != NULL) {
    *p = 0;
  }
  if (!srcdir || !*srcdir) {
    fprintf(stderr, "Missing package source directory ($INSTALLPREFIX or -s parameter)\n");
    return 5;
  }
  if (!dstdir || !*dstdir) {
    fprintf(stderr, "Missing installation directory ($MINGWPREFIX or -i parameter)\n");
    return 6;
  }
#ifdef __MINGW32__
  {
    char s[_MAX_PATH];
    if (!_fullpath(s, srcdir,_MAX_PATH)) {
      fprintf(stderr, "Unable to get real path for package source directory\n");
      return 7;
    }
    free(srcdir);
    srcdir = strdup(s);
  }
#endif
  if (!pkgdir || !*pkgdir) {
    fprintf(stderr, "Missing package destination directory ($PACKAGEDIR or -p parameter)\n");
    return 8;
  }
#ifdef __MINGW32__
  {
    char s[_MAX_PATH];
    //realpath
    if (!_fullpath(s, pkgdir,_MAX_PATH)) {
      fprintf(stderr, "Unable to get real path for package destination directory\n");
      return 9;
    }
    free(pkgdir);
    pkgdir = strdup(s);
  }
#endif
  if (!fstabpath)
    fstabpath = get_fstab_path();
  if (fstabpath && *fstabpath) {
    if ((fstabdata = read_fstab_data(fstabpath)) == NULL) {
      fprintf(stderr, "Error reading fstab mount points (see also -f parameter)\n");
      return 10;
    }
  }

  //get list of PATH entries that are under the installation path
  {
    struct check_if_installation_path_struct data;
    data.dstdir = dstdir;
    data.dst_paths = &dst_paths;
    iterate_path_list(getenv("PATH"), 0, check_if_installation_path, &data);
/*
    struct strings_linked_list* p = dst_paths;
    while (p) {
      printf("- %s\n", p->data);
      p = p->next;
    }
*/
  }

  //show information
  if (verbose) {
    printf("Target architecture: %s\n", arch);
    printf("Package base name: %s\n", packagename);
    printf("Package version: %s\n", (packageversion ? packageversion : "(none)"));
    printf("Package source folder: %s\n", srcdir);
    printf("Package installation folder: %s\n", dstdir);
    printf("Package file destination folder: %s\n", pkgdir);
    printf("Full path to fstab file: %s\n", fstabpath);
  }

  //determine package filename
  packagefilename = (char*)malloc(strlen(pkgdir) + strlen(packagename) + (packageversion ? strlen(packageversion) + 1 : 0) + strlen(arch) + strlen(PACKAGE_EXTENSION) + 3);
  strcpy(packagefilename, pkgdir);
#ifdef _WIN32
  strcat(packagefilename, "\\");
#else
  strcat(packagefilename, "/");
#endif
  strcat(packagefilename, packagename);
  if (packageversion) {
    strcat(packagefilename, "-");
    strcat(packagefilename, packageversion);
  }
  strcat(packagefilename, ".");
  strcat(packagefilename, arch);
  strcat(packagefilename, PACKAGE_EXTENSION);
  printf("Creating package: %s\n", packagefilename);

  //create archive
  int status;
  struct packager_callback_struct callbackdata;
  callbackdata.level = 0;
  callbackdata.verbose = verbose;
  callbackdata.srcdir = srcdir;
  callbackdata.dstdir = dstdir;
  callbackdata.fstabdata = fstabdata;
#ifndef NO_PEDEPS
  callbackdata.ownpemodules = (checkdeps ? sorted_unique_list_create(strcasecmp, free) : NULL);
  callbackdata.pedeps = (checkdeps ? sorted_unique_list_create(strcasecmp, free) : NULL);
  callbackdata.pelastmodule = NULL;
#endif
  //delete archive if it already exists
  unlink(packagefilename);
  //create archive
  callbackdata.arch = archive_write_new();
#if defined(ARCHIVEFORMAT_ZIP)
  archive_write_set_format_zip(callbackdata.arch);  //zip archive
#elif defined(ARCHIVEFORMAT_TAR)
  archive_write_set_format_pax_restricted(callbackdata.arch);  //tar archive
#elif defined(ARCHIVEFORMAT_TAR_XZ)
  archive_write_set_format_pax_restricted(callbackdata.arch);  //tar archive
/*
#if ARCHIVE_VERSION_NUMBER < 3000000
  archive_write_set_compression_xz(callbackdata.arch); //xz compression
#else
  archive_write_add_filter_xz(callbackdata.arch); //xz compression
#endif
*/
#elif defined(ARCHIVEFORMAT_7Z)
  archive_write_set_format_7zip(callbackdata.arch);  //7-zip archive
#else
  archive_write_set_format_filter_by_ext(callbackdata.arch, packagefilename);
#endif
  if ((status = archive_write_open_filename(callbackdata.arch, packagefilename)) != 0) {
    fprintf(stderr, "Error %i opening archive: %s\n", status, archive_error_string(callbackdata.arch));
    return 1;
  }

#ifdef CHECK_DEPENDANCIES
  //check dependencies
  struct get_dependencies_imports depsdata;
  if ((depsdata.pehandle = pefile_create()) != NULL) {
    depsdata.lastdependency = NULL;
    depsdata.dependencies = NULL;
    depsdata.binaries = NULL;
    status = dirtrav_traverse_directory(srcdir, dependencies_file_callback, NULL, NULL, &depsdata);
    pefile_destroy(depsdata.pehandle);
    free(depsdata.lastdependency);
    //list external dependencies
    printf("Dependancies:\n");
    text_list_compare_lists(depsdata.dependencies, depsdata.binaries, NULL, list_dependencies, NULL, dst_paths);
    text_list_destroy(&(depsdata.dependencies));
    text_list_destroy(&(depsdata.binaries));
  }
#endif

  //add metadata file to archive
  {
    int i;
    struct memory_buffer* t;
    struct memory_buffer* metadata;
    t = memory_buffer_create();
    metadata = memory_buffer_create();
    memory_buffer_set(metadata, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    memory_buffer_append(metadata, "<package");
    memory_buffer_append_printf(metadata, " basename=\"%s\"", memory_buffer_get(memory_buffer_xml_special_chars(memory_buffer_set(t, packagename))));
    memory_buffer_append_printf(metadata, " version=\"%s\"", memory_buffer_get(memory_buffer_xml_special_chars(memory_buffer_set(t, packageversion))));
    memory_buffer_append_printf(metadata, " architecture=\"%s\"", memory_buffer_get(memory_buffer_xml_special_chars(memory_buffer_set(t, arch))));
    i = 0;
    while (metadata_envvar_info[i].envvar) {
      const char* s;
      if ((s = getenv(metadata_envvar_info[i].envvar)) != NULL && *s) {
        if (callbackdata.verbose) {
          printf("Attribute[%s]: %s\n", metadata_envvar_info[i].xmlattr, s);/////
        }
        memory_buffer_append_printf(metadata, " %s=\"%s\"", metadata_envvar_info[i].xmlattr, memory_buffer_get(memory_buffer_xml_special_chars(memory_buffer_set(t, s))));
      }
      i++;
    }
    memory_buffer_append(metadata, ">\n");
    memory_buffer_append(metadata, "\t<dependencies>\n");
    csv_entries_append(metadata, getenv("DEPENDENCIES"), "\t\t<dependency type=\"mandatory\" name=\"", "\"/>\n");
    csv_entries_append(metadata, getenv("DEPENDANCIES"), "\t\t<dependency type=\"mandatory\" name=\"", "\"/>\n");
    csv_entries_append(metadata, getenv("OPTIONALDEPENDENCIES"), "\t\t<dependency type=\"optional\" name=\"", "\"/>\n");
    csv_entries_append(metadata, getenv("OPTIONALDEPENDANCIES"), "\t\t<dependency type=\"optional\" name=\"", "\"/>\n");
    csv_entries_append(metadata, getenv("BUILDDEPENDENCIES"), "\t\t<dependency type=\"build\" name=\"", "\"/>\n");
    csv_entries_append(metadata, getenv("BUILDDEPENDANCIES"), "\t\t<dependency type=\"build\" name=\"", "\"/>\n");
    memory_buffer_append(metadata, "\t</dependencies>\n");
#ifndef NO_PEDEPS
    dirtrav_traverse_directory(srcdir, check_pe_dependencies_callback, NULL, NULL, &callbackdata);
    if (callbackdata.pedeps) {
      char* modulename;
      printf("Dependencies:\n");
      memory_buffer_append(metadata, "\t<dlldependencies>\n");
      while ((modulename = sorted_unique_list_get_and_remove_first(callbackdata.pedeps)) != NULL) {
        if (!sorted_unique_list_find(callbackdata.ownpemodules, modulename)) {
          printf("- %s\n", modulename);
          memory_buffer_append_printf(metadata, "\t\t<dll name=\"%s\"/>\n", memory_buffer_get(memory_buffer_xml_special_chars(memory_buffer_set(t, modulename))));
        }
      }
      memory_buffer_append(metadata, "\t</dlldependencies>\n");
    }
#endif
    memory_buffer_append(metadata, "\t<exclude>\n");
    memory_buffer_append(metadata, "\t\t<file name=\".packageinfo.xml\"/>\n");
    memory_buffer_append(metadata, "\t\t<directory name=\"" PACKAGE_INFO_LICENSE_FOLDER "\"/>\n");
    memory_buffer_append(metadata, "\t</exclude>\n");
#ifdef METADATA_CONTENTS
    memory_buffer_append(metadata, "<contents>\n");
    status = dirtrav_traverse_directory(srcdir, packagelist_file_callback, packagelist_before_folder_callback, NULL, metadata);
    memory_buffer_append(metadata, "</contents>\n");
#endif
    memory_buffer_append(metadata, "</package>\n");
    add_memory_to_archive(memory_buffer_get(metadata), memory_buffer_length(metadata), ".packageinfo.xml", &callbackdata);
    memory_buffer_free(t);
    memory_buffer_free(metadata);
  }

  //add licence folder and file to archive
  if (add_folder_to_archive(PACKAGE_INFO_LICENSE_FOLDER, &callbackdata) > 0 && licfile && *licfile) {
    struct memory_buffer* relativepath = memory_buffer_create();
    memory_buffer_set(relativepath, PACKAGE_INFO_LICENSE_FOLDER "/");
    memory_buffer_append(relativepath, packagename);
    memory_buffer_append(relativepath, "/");
    memory_buffer_append(relativepath, licfile);
    if (callbackdata.verbose) {
      printf("Adding license file: %s\n", licfile);
      fflush(stdout);
    }
    if (add_file_to_archive(licfile, memory_buffer_get(relativepath), &callbackdata) == 0) {
      fprintf(stderr, "Error adding license file: %s\n", licfile);
    }
    memory_buffer_free(relativepath);
  }

  //add files to archive
  status = dirtrav_traverse_directory(srcdir, packager_file_callback, packager_before_folder_callback, packager_after_folder_callback, &callbackdata);

  //close archive
  if (archive_write_close(callbackdata.arch) != ARCHIVE_OK)
    fprintf(stderr, "There was an error writing to the archive\n");
#if ARCHIVE_VERSION_NUMBER < 3000000
  archive_write_finish(callbackdata.arch);
#else
  archive_write_free(callbackdata.arch);
#endif

/*
  //list dependencies
#ifndef NO_PEDEPS
  if (callbackdata.pedeps) {
    char* modulename;
    printf("Dependencies:\n");
    while ((modulename = sorted_unique_list_get_and_remove_first(callbackdata.pedeps)) != NULL) {
      if (!sorted_unique_list_find(callbackdata.ownpemodules, modulename))
        printf("- %s\n", modulename);
    }
  }
#endif
*/

  //finished creating the archive
  if (verbose)
    printf("Done creating archive, status code: %i\n", status);

  //delete source files and directories
  if (deleteafter) {
    size_t errors = 0;
    if (verbose)
      printf("Deleting folder: %s\n", srcdir);
    status = dirtrav_traverse_directory(srcdir, delete_file_callback, NULL, delete_after_folder_callback, &errors);
#ifdef _WIN32
    if (!RemoveDirectoryA(srcdir)) {
#else
    if (rmdir(srcdir) != 0) {
#endif
      errors++;
      fprintf(stderr, "Error deleting folder: %s\n", srcdir);
    }
    if (verbose)
      printf("Cleanup done, %lu errors encountered.\n", (unsigned long)errors);
  }

  //clean up
#ifndef NO_PEDEPS
  if (callbackdata.pedeps) {
    sorted_unique_list_free(callbackdata.ownpemodules);
    sorted_unique_list_free(callbackdata.pedeps);
    if (callbackdata.pelastmodule)
      free(callbackdata.pelastmodule);
  }
#endif
  free(arch);
  free(srcdir);
  free(dstdir);
  free(pkgdir);
  free(fstabpath);
  cleanup_fstab_data(fstabdata);
  strings_linked_list_free(&dst_paths);
  free(packagefilename);
  return 0;
}

/////TO DO: add error detection (e.g. if folder does not exist)
/////TO DO: add information if optional dependancy was installed during build
