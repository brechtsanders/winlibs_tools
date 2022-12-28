#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <archive.h>
#include <archive_entry.h>
#include <expat.h>
#include <miniargv.h>
#include "winlibs_common.h"
#include "filesystem.h"
#include "memory_buffer.h"
#include "sorted_unique_list.h"
//#include "text_buffer.h"
//#include "text_list.h"
#include "pkgdb.h"

#define PROGRAM_NAME    "wl-install"
#define PROGRAM_DESC    "Command line utility to install a package"

#if defined(_WIN32) && !defined(__MINGW64_VERSION_MAJOR)
#define strcasecmp _stricmp
#endif
#define ARCHIVE_READ_BLOCK_SIZE 4096

////////////////////////////////////////////////////////////////////////

static const char* xml_package_attrs[PACKAGE_METADATA_TOTAL_FIELDS] = {
  "basename",
  "version",
  "name",
  "description",
  "url",
  "downloadurl",
  "downloadsourceurl",
  "category",
  "type",
  "versiondate",
  "licensefile",
  "licensetype",
  "status"
};

struct xml_data_struct {
  XML_Parser xmlparser;
  struct package_metadata_struct* metadata;
};

void initialize_xml_data (struct xml_data_struct* xmldata, struct package_metadata_struct* metadata)
{
  xmldata->xmlparser = XML_ParserCreate(NULL);
  xmldata->metadata = metadata;
  XML_SetUserData(xmldata->xmlparser, xmldata);
}

void cleanup_xml_data (struct xml_data_struct* xmldata)
{
  XML_ParserFree(xmldata->xmlparser);
}

#if defined(_WIN32) && !defined(__MINGW64_VERSION_MAJOR)
#define strcasecmp _stricmp
#endif
#ifdef _WIN32
#define wcscasecmp _wcsicmp
#endif


#define XLSXIOCHAR XML_Char

#if !defined(XML_UNICODE_WCHAR_T) && !defined(XML_UNICODE)

//UTF-8 version
#define X(s) s
#ifdef _WIN32
#define XML_Char_icmp stricmp
#else
#define XML_Char_icmp strcasecmp
#endif
#define XML_Char_len strlen
#define XML_Char_dup strdup
#define XML_Char_cpy strcpy
#define XML_Char_poscpy(d,p,s,l) memcpy(d + p, s, l)
#define XML_Char_malloc(n) ((char*)malloc(n))
#define XML_Char_realloc(m,n) ((char*)realloc((m), (n)))
#define XML_Char_tol(s) strtol((s), NULL, 10)
#define XML_Char_tod(s) strtod((s), NULL)
#define XML_Char_strtol strtol
#define XML_Char_sscanf sscanf
#define XML_Char_printf printf

#else

//UTF-16 version
#include <wchar.h>
#define X(s) L##s
#define XML_Char_icmp wcscasecmp
#define XML_Char_len wcslen
#define XML_Char_dup wcsdup
#define XML_Char_cpy wcscpy
#define XML_Char_poscpy(d,p,s,l) wmemcpy(d + p, s, l)
#define XML_Char_malloc(n) ((XML_Char*)malloc((n) * sizeof(XML_Char)))
#define XML_Char_realloc(m,n) ((XML_Char*)realloc((m), (n) * sizeof(XML_Char)))
#define XML_Char_tol(s) wcstol((s), NULL, 10)
#define XML_Char_tod(s) wcstod((s), NULL)
#define XML_Char_strtol wcstol
#define XML_Char_sscanf swscanf
#define XML_Char_printf wprintf

#endif

//compare XML name ignoring case and ignoring namespace (returns 0 on match)
#ifdef ASSUME_NO_NAMESPACE
#define XML_Char_icmp_ins XML_Char_icmp
#else
int XML_Char_icmp_ins (const XML_Char* value, const XML_Char* name)
{
  size_t valuelen = XML_Char_len(value);
  size_t namelen = XML_Char_len(name);
  if (valuelen == namelen)
    return XML_Char_icmp(value, name);
  if (valuelen > namelen) {
    if (value[valuelen - namelen - 1] != ':')
      return 1;
    return XML_Char_icmp(value + (valuelen - namelen), name);
  }
  return -1;
}
#endif

//get expat attribute by name, returns NULL if not found
const XML_Char* get_expat_attr_by_name (const XML_Char** atts, const XML_Char* name)
{
  const XML_Char** p = atts;
  if (p) {
    while (*p) {
      //if (XML_Char_icmp(*p++, name) == 0)
      if (XML_Char_icmp_ins(*p++, name) == 0)
        return *p;
      p++;
    }
  }
  return NULL;
}

void xml_element_start_root (void *callbackdata, const XML_Char *name, const XML_Char **atts);
void xml_element_end_root (void *callbackdata, const XML_Char *name);
void xml_element_start_package (void *callbackdata, const XML_Char *name, const XML_Char **atts);
void xml_element_end_package (void *callbackdata, const XML_Char *name);
void xml_element_start_exclude (void *callbackdata, const XML_Char *name, const XML_Char **atts);
void xml_element_end_exclude (void *callbackdata, const XML_Char *name);
void xml_element_start_dependencies (void *callbackdata, const XML_Char *name, const XML_Char **atts);
void xml_element_end_dependencies (void *callbackdata, const XML_Char *name);

void xml_element_start_root (void *callbackdata, const XML_Char *name, const XML_Char **atts)
{
  struct xml_data_struct* data = (struct xml_data_struct*)callbackdata;
  if (XML_Char_icmp_ins(name, X("package")) == 0) {
    int i;
    const XML_Char* value;
    for (i = 0; i < PACKAGE_METADATA_TOTAL_FIELDS; i++) {
      if ((value = get_expat_attr_by_name(atts, xml_package_attrs[i])) != NULL) {
        if (data->metadata->datafield[i])
          free(data->metadata->datafield[i]);
        data->metadata->datafield[i] = strdup(value);
      }
    }
    XML_SetElementHandler(data->xmlparser, xml_element_start_package, xml_element_end_package);
  }
}

void xml_element_end_root (void *callbackdata, const XML_Char *name)
{
}

void xml_element_start_package (void *callbackdata, const XML_Char *name, const XML_Char **atts)
{
  struct xml_data_struct* data = (struct xml_data_struct*)callbackdata;
  if (XML_Char_icmp_ins(name, X("exclude")) == 0) {
    XML_SetElementHandler(data->xmlparser, xml_element_start_exclude, xml_element_end_exclude);
  } else
  if (XML_Char_icmp_ins(name, X("dependencies")) == 0) {
    XML_SetElementHandler(data->xmlparser, xml_element_start_dependencies, xml_element_end_dependencies);
  }
}

void xml_element_end_package (void *callbackdata, const XML_Char *name)
{
  if (XML_Char_icmp_ins(name, X("package")) == 0) {
    struct xml_data_struct* data = (struct xml_data_struct*)callbackdata;
    XML_SetElementHandler(data->xmlparser, xml_element_start_root, xml_element_end_root);
  }
}

void xml_element_start_exclude (void *callbackdata, const XML_Char *name, const XML_Char **atts)
{
  struct xml_data_struct* data = (struct xml_data_struct*)callbackdata;
  if (XML_Char_icmp_ins(name, X("file")) == 0) {
    const char* name;
    if ((name = get_expat_attr_by_name(atts, "name")) != NULL) {
      sorted_unique_list_add(data->metadata->fileexclusions, name);
    }
  } else if (XML_Char_icmp_ins(name, X("directory")) == 0) {
    const char* name;
    if ((name = get_expat_attr_by_name(atts, "name")) != NULL) {
      sorted_unique_list_add(data->metadata->folderexclusions, name);
    }
  }
}

void xml_element_end_exclude (void *callbackdata, const XML_Char *name)
{
  if (XML_Char_icmp_ins(name, X("exclude")) == 0) {
    struct xml_data_struct* data = (struct xml_data_struct*)callbackdata;
    XML_SetElementHandler(data->xmlparser, xml_element_start_package, xml_element_end_package);
  }
}

void xml_element_start_dependencies (void *callbackdata, const XML_Char *name, const XML_Char **atts)
{
  struct xml_data_struct* data = (struct xml_data_struct*)callbackdata;
  if (XML_Char_icmp_ins(name, X("dependency")) == 0) {
    const char* name;
    const char* type;
    if ((name = get_expat_attr_by_name(atts, "name")) != NULL) {
      type = get_expat_attr_by_name(atts, "type");
      if (strcasecmp(type, "build") == 0) {
        sorted_unique_list_add(data->metadata->builddependencies, name);
      } else if (strcasecmp(type, "optional") == 0) {
        sorted_unique_list_add(data->metadata->optionaldependencies, name);
      } else {
        sorted_unique_list_add(data->metadata->dependencies, name);
      }
    }
  }
}

void xml_element_end_dependencies (void *callbackdata, const XML_Char *name)
{
  if (XML_Char_icmp_ins(name, X("dependencies")) == 0) {
    struct xml_data_struct* data = (struct xml_data_struct*)callbackdata;
    XML_SetElementHandler(data->xmlparser, xml_element_start_package, xml_element_end_package);
  }
}

////////////////////////////////////////////////////////////////////////

int list_remove_entry (const char* data, void* callbackdata)
{
  sorted_unique_list_remove((sorted_unique_list*)callbackdata, data);
  return 0;
}

int filelist_delete_file (const char* data, void* callbackdata)
{
  size_t dirlen;
  char* path;
  dirlen = strlen((char*)callbackdata);
  if ((path = (char*)malloc(dirlen + strlen(data) + 2)) != NULL) {
    memcpy(path, callbackdata, dirlen);
    path[dirlen] = PATH_SEPARATOR;
    strcpy(path + dirlen + 1, data);
    if (unlink(path) != 0)
      fprintf(stderr, "Error deleting file: %s\n", path);
    free(path);
  }
  return 0;
}

int filelist_delete_file_verbose (const char* data, void* callbackdata)
{
  printf("Deleting: %s\n", data);
  return filelist_delete_file(data, callbackdata);
  return 0;
}

int filelist_show_old (const char* data, void* callbackdata)
{
  printf("- %s\n", data);
  return 0;
}

int filelist_show_new (const char* data, void* callbackdata)
{
  printf("+ %s\n", data);
  return 0;
}

////////////////////////////////////////////////////////////////////////

int is_directory_excluded (struct package_metadata_struct* metadata, const char* folderpath)
{
  char* partialpath;
  if (sorted_unique_list_find(metadata->folderexclusions, folderpath))
    return 1;
  if ((partialpath = strdup(folderpath)) != NULL) {
    size_t i = strlen(partialpath);
    while (i > 0) {
      i--;
      if (partialpath[i] == '/' /*|| partialpath[i] == '\\'*/) {
        partialpath[i] = 0;
        if (sorted_unique_list_find(metadata->folderexclusions, partialpath)) {
          free(partialpath);
          return 2;
        }
      }
    }
    free(partialpath);
  }
  return 0;
}

int is_file_excluded (struct package_metadata_struct* metadata, const char* filepath)
{
  if (sorted_unique_list_find(metadata->fileexclusions, filepath))
    return 1;
  return is_directory_excluded(metadata, filepath);
}

char* strdup_slashed (const char* path)
{
  char* result;
  if (!path)
    return NULL;
  if ((result = strdup(path)) != NULL) {
    char* p = result;
    while ((p = strchr(p, '\\')) != NULL)
      *p++ = '/';
  }
  return result;
}

////////////////////////////////////////////////////////////////////////

int read_metadata (const char* packagefilename, struct package_metadata_struct* metadata)
{
  //get metadata from package file
  struct archive* pkg = archive_read_new();
  //archive_read_support_filter_all(pkg);
  //archive_read_support_format_all(pkg);
  archive_read_support_format_7zip(pkg);
  if (archive_read_open_filename(pkg, packagefilename, ARCHIVE_READ_BLOCK_SIZE) != ARCHIVE_OK) {
    fprintf(stderr, "Error opening package file: %s\n", packagefilename);
    return 4;
  } else {
    struct archive_entry* pkgentry;
    char* pathname;
    while (archive_read_next_header(pkg, &pkgentry) == ARCHIVE_OK) {
      if ((pathname = strdup_slashed(archive_entry_pathname(pkgentry))) != NULL) {
        if (strcasecmp(archive_entry_pathname(pkgentry), PACKAGE_INFO_METADATA_FILE) == 0) {
          //get metadata
          char buf[256];
          ssize_t len;
          struct xml_data_struct xmldata;
          initialize_xml_data(&xmldata, metadata);
          XML_SetElementHandler(xmldata.xmlparser, xml_element_start_root, xml_element_end_root);
/*
const void *buff;
size_t size;
la_int64_t offset;
len = archive_read_data_block(pkg, &buff, &size, &offset);
*/
          while ((len = archive_read_data(pkg, buf, sizeof(buf))) > 0) {
            XML_Parse(xmldata.xmlparser, buf, len, 0);
          }
          XML_Parse(xmldata.xmlparser, NULL, 0, 1);
          cleanup_xml_data(&xmldata);
          free(pathname);
        } else if (archive_entry_filetype(pkgentry) & AE_IFREG) {
          //add file path to list
          sorted_unique_list_add_allocated(metadata->filelist, pathname);
          //update total size to extract
          metadata->totalsize += archive_entry_size(pkgentry);
          /*archive_read_data_skip(pkg);*/
        } else if (archive_entry_filetype(pkgentry) & AE_IFDIR) {
          //add folder path to list
          sorted_unique_list_add_allocated(metadata->folderlist, pathname);
/*
          //remove trailing path separator(s)
          char* path = (*folderlistlast)->text;
          size_t pathlen = strlen(path);
          while (pathlen > 0 && (path[pathlen - 1] == '/'
#ifdef _WIN32
            || path[pathlen - 1] == '\\'
#endif
          ))
            path[--pathlen] = 0;
*/
          /*archive_read_data_skip(pkg);*/
        } else {
          //unhandled file type
          /*archive_read_data_skip(pkg);*/
          free(pathname);
        }
      }
    }
    archive_read_close(pkg);
    //remove listed exlusions from the file list
    //sorted_unique_list_compare_lists(metadata->fileexclusions, metadata->filelist, list_remove_entry, NULL, NULL, &(metadata->filelist));
    {
      int i;
      int n;
      const char* s;
      n = sorted_unique_list_size(metadata->filelist);
      for (i = 0; i < n; i++) {
        if ((s = sorted_unique_list_get(metadata->filelist, i)) != NULL) {
          if (is_file_excluded(metadata, s)) {
            sorted_unique_list_remove(metadata->filelist, s);
          }
        }
      }
    }
    sorted_unique_list_compare_lists(metadata->folderexclusions, metadata->folderlist, list_remove_entry, NULL, NULL, &(metadata->folderlist));
    //remove listed exlusions from the folder list
    {
      int i;
      int n;
      const char* s;
      n = sorted_unique_list_size(metadata->folderlist);
      for (i = 0; i < n; i++) {
        if ((s = sorted_unique_list_get(metadata->folderlist, i)) != NULL) {
          if (is_file_excluded(metadata, s)) {
            sorted_unique_list_remove(metadata->folderlist, s);
          }
        }
      }
    }
  }
#if ARCHIVE_VERSION_NUMBER < 3000000
  archive_write_finish(pkg);
#else
  archive_write_free(pkg);
#endif
  return 0;
}

int main (int argc, char** argv, char *envp[])
{
  //process command line parameters
  int i;
  char* p;
  int showhelp = 0;
  int showdiff = 0;
  int verbose = 0;
  int abort = 0;
  char* arch = NULL;
  const char* basepath = NULL;
  const char* pkgdir = NULL;
  char* packagefilename = NULL;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",         NULL,      miniargv_cb_increment_int, &showhelp,        "show command line help"},
    {'p', "package-path", "PATH",    miniargv_cb_set_const_str, &pkgdir,          "path where package files are stored\noverrides environment variable PACKAGEDIR"},
    {'i', "install-path", "PATH",    miniargv_cb_set_const_str, &basepath,        "package installation path\noverrides environment variable MINGWPREFIX"},
    {'a', "arch",         "ARCH",    miniargv_cb_strdup,        &arch,            "architecture (i686/x86_64, default based on $RUNPLATFORM)"},
    {'d', "diff",         NULL,      miniargv_cb_increment_int, &showdiff,        "show difference with installed package (files added/removed)"},
    {'v', "verbose",      NULL,      miniargv_cb_increment_int, &verbose,         "verbose mode"},
    {0,   NULL,           "PACKAGE", miniargv_cb_error,         NULL,             "package to install"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "PACKAGEDIR",   NULL,      miniargv_cb_set_const_str, &pkgdir,          "path where package files are stored"},
    {0,   "MINGWPREFIX",  NULL,      miniargv_cb_set_const_str, &basepath,        "package installation path"},
    {0,   "RUNPLATFORM",  NULL,      miniargv_cb_strdup,        &arch,            "target architecture (i686-w64-mingw32/x86_64-w64-mingw32)"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //parse environment and command line flags
  if (miniargv_process_env(envp, envdef, NULL) != 0)
    return 1;
  if (miniargv_process_arg_flags(argv, argdef, NULL, NULL) != 0)
    return 1;
  //show help if requested or if no command line arguments were given
  if (showhelp || argc <= 1) {
    printf(
      PROGRAM_NAME " - Version " WINLIBS_VERSION_STRING " - " WINLIBS_LICENSE " - " WINLIBS_CREDITS "\n"
      PROGRAM_DESC "\n"
      "Usage: " PROGRAM_NAME " "
    );
    miniargv_arg_list(argdef, 1);
    printf("\n");
    miniargv_help(argdef, envdef, 24, 0);
#ifdef PORTCOLCON_VERSION
    printf(WINLIBS_HELP_COLOR);
#endif
    return 0;
  }
  //check parameters
  if (!basepath || !*basepath) {
    fprintf(stderr, "Missing -i parameter or MINGWPREFIX environment variable\n");
    return 2;
  }
/*
#ifdef __MINGW32__
  {
    char s[_MAX_PATH];
    if (!_fullpath(s, basepath,_MAX_PATH)) {
      fprintf(stderr, "Unable to get real path for installation directory\n");
      show_help();
      return 4;
    }
    free(basepath);
    basepath = strdup(s);
  }
#endif
*/
  if (!folder_exists(basepath)) {
    fprintf(stderr, "Path does not exist: %s\n", basepath);
    return 3;
  }
  if ((i = miniargv_get_next_arg_param(0, argv, argdef, NULL)) <= 0) {
    fprintf(stderr, "Missing package name\n");
    return 4;
  } else {
    if (miniargv_get_next_arg_param(i, argv, argdef, NULL) > 0) {
      fprintf(stderr, "Only one package name allowed (additional parameter: %s)\n", argv[i]);
      return 5;
    }
  }
  packagefilename = strdup(argv[i]);
  if (!pkgdir || !*pkgdir) {
    fprintf(stderr, "Missing package directory\n");
    return 6;
  }
/*
#ifdef __MINGW32__
  {
    char s[_MAX_PATH];
    if (!_fullpath(s, pkgdir,_MAX_PATH)) {
      fprintf(stderr, "Unable to get real path for package directory\n");
      show_help();
      return 6;
    }
    free(pkgdir);
    pkgdir = strdup(s);
  }
#endif
*/
  if (!arch || !*arch) {
    fprintf(stderr, "Missing target architecture ($RUNPLATFORM or -a parameter)\n");
    return 7;
  } else if ((p = strchr(arch, '-')) != NULL) {
    *p = 0;
  }
  if (!packagefilename || !*packagefilename) {
    fprintf(stderr, "Missing package name\n");
    return 7;
  }
  if (!file_exists(packagefilename)) {
    size_t packagefilenamelen = strlen(packagefilename);
    //add extension if needed
    if (!(packagefilenamelen >= 3 && strcasecmp(packagefilename + packagefilenamelen - 3, ".7z") == 0)) {
      if ((packagefilename = (char*)realloc(packagefilename, packagefilenamelen + 3 + 1)) != NULL) {
        strcpy(packagefilename + packagefilenamelen, ".7z");
        packagefilenamelen += 3;
      }
    }
    //add architecture if needed
    if (arch && *arch) {
      size_t archlen = strlen(arch);
      if (!(packagefilenamelen >= 4 + archlen && packagefilename[packagefilenamelen - 4 - archlen] == '.' && strcasecmp(packagefilename + packagefilenamelen - 3 - archlen, arch) == 0)) {
        if ((packagefilename = (char*)realloc(packagefilename, packagefilenamelen + archlen + 2)) != NULL) {
          memmove(packagefilename + packagefilenamelen - 3 + 1 + archlen, packagefilename + packagefilenamelen - 3, 4);
          packagefilename[packagefilenamelen - 3] = '.';
          memcpy(packagefilename + packagefilenamelen - 2, arch, archlen);
          packagefilenamelen += 1 + archlen;
        }
      }
    }
    if (!file_exists(packagefilename) && *pkgdir) {
      size_t pkgdirlen = strlen(pkgdir);
      size_t separatorlen = (pkgdir[pkgdirlen - 1] == '/'
#ifdef _WIN32
        || pkgdir[pkgdirlen - 1] == '\\' || pkgdir[pkgdirlen - 1] == ':'
#endif
        ? 0 : 1);
      if ((packagefilename = (char*)realloc(packagefilename, pkgdirlen + separatorlen + packagefilenamelen + 1)) != NULL) {
        memmove(packagefilename + pkgdirlen + separatorlen, packagefilename, packagefilenamelen + 1);
        memcpy(packagefilename, pkgdir, pkgdirlen);
        if (separatorlen)
          packagefilename[pkgdirlen] =
#ifdef _WIN32
            '\\';
#else
            '/';
#endif
      }
    }
    if (!file_exists(packagefilename)) {
      fprintf(stderr, "Package file not found: %s\n", packagefilename);
      return 3;
    }
  }
  //show verbose information
/*
  if (verbose) {
    printf("Package file: %s\n", packagefilename);
    printf("Package folder: %s\n", pkgdir);
    printf("Installation folder: %s\n", basepath);
  }
*/

  //get metadata from package file
  struct package_metadata_struct* metadata = package_metadata_create();
  if (read_metadata(packagefilename, metadata) != 0) {
    fprintf(stderr, "Error opening package file: %s\n", packagefilename);
    return 4;
  }
  //check if metadata was present
  if (!metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME] || !metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME][0]) {
    fprintf(stderr, "Error: metadata missing in package file (basename not set)\n");
    return 5;
  }
  //make sure totalsize is not zero (to avoid division by zero in the progress indicator)
  if (metadata->totalsize == 0)
    metadata->totalsize++;

  //get information about already installed package
  char* installedversion = NULL;
  sorted_unique_list* installedfilelist = sorted_unique_list_create(strcmp, free);
  {
    sorted_unique_list* v = sorted_unique_list_create(strcmp, free);
    struct memory_buffer* pkginfopath = memory_buffer_create();
    struct memory_buffer* filepath = memory_buffer_create();
    //determine package information path
    memory_buffer_set_printf(pkginfopath, "%s%s%c%s%c", basepath, PACKAGE_INFO_PATH, PATH_SEPARATOR, metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME], PATH_SEPARATOR);
    //load version installed package
    sorted_unique_list_load_from_file(&v, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_VERSION_FILE)));
    installedversion = sorted_unique_list_get_and_remove_first(v);
    sorted_unique_list_free(v);
    //load list of installed files
    sorted_unique_list_load_from_file(&installedfilelist, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_CONTENT_FILE)));
    //clean up
    memory_buffer_free(pkginfopath);
    memory_buffer_free(filepath);
  }

  //show information
  if (installedversion)
    printf("Updating %s from version %s to %s\n", metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME], installedversion, metadata->datafield[PACKAGE_METADATA_INDEX_VERSION]);
  else
    printf("Installing %s version %s\n", metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME], metadata->datafield[PACKAGE_METADATA_INDEX_VERSION]);
  if (verbose) {
    printf("Destination: %s\n", basepath);
  }

  //compare contents of text lists
  if (showdiff && installedversion)
    sorted_unique_list_compare_lists(installedfilelist, metadata->filelist, NULL, filelist_show_old, filelist_show_new, NULL);

  //delete files that are no longer used
  sorted_unique_list_compare_lists(installedfilelist, metadata->filelist, NULL, (verbose ? filelist_delete_file_verbose : filelist_delete_file), NULL, (void*)basepath);

  //create folder structure
  {
    int i;
    int n;
    struct memory_buffer* folderpath = memory_buffer_create();
    n = sorted_unique_list_size(metadata->folderlist);
    for (i = 0; i < n; i++)
      recursive_mkdir(memory_buffer_get(memory_buffer_set_printf(folderpath, "%s%c%s", basepath, PATH_SEPARATOR, sorted_unique_list_get(metadata->folderlist, i))));
    //clean up
    memory_buffer_free(folderpath);
  }

  //extract files
  struct archive* pkg = archive_read_new();
  //archive_read_support_filter_all(pkg);
  //archive_read_support_format_all(pkg);
  archive_read_support_format_7zip(pkg);
  if (archive_read_open_filename(pkg, packagefilename, ARCHIVE_READ_BLOCK_SIZE) != ARCHIVE_OK) {
    fprintf(stderr, "Error opening package file: %s\n", packagefilename);
    return 4;
  } else {
    struct archive_entry* pkgentry;
    char* pathname;
    char* fullpath;
    FILE* dst;
    size_t basepathlen = strlen(basepath);
    uint64_t currentsize = 0;
    while (!abort && archive_read_next_header(pkg, &pkgentry) == ARCHIVE_OK) {
      if ((pathname = strdup_slashed(archive_entry_pathname(pkgentry))) != NULL) {
        if (archive_entry_filetype(pkgentry) & AE_IFREG && sorted_unique_list_find(metadata->filelist, pathname)) {
          //remove from file list (optional early cleanup for performance improvement)
          //sorted_unique_list_remove(metadata->filelist, pathname);
          //determine destination name
          if ((fullpath = (char*)malloc(basepathlen + strlen(pathname) + 2)) != NULL) {
            memcpy(fullpath, basepath, basepathlen);
            fullpath[basepathlen] = PATH_SEPARATOR;
            strcpy(fullpath + basepathlen + 1, pathname);
            //show progress
            if (verbose)
              printf("Extracting: %s\n", pathname);
            else
              printf("\r%3i%%", (int)(currentsize * 100 / metadata->totalsize));
            //write file contents
            if ((dst = fopen(fullpath, "wb")) == NULL) {
              fprintf(stderr, "Error creating file: %s\n", fullpath);
              abort = 1;
            } else {
              char buf[256];
              ssize_t len;
              while ((len = archive_read_data(pkg, buf, sizeof(buf))) > 0) {
                if (fwrite(buf, 1, len, dst) < len) {
                  fprintf(stderr, "Error writing to file: %s\n", fullpath);
                  abort = 1;
                  break;
                }
                currentsize += len;
              }
              fclose(dst);
            }
            free(fullpath);
          } else {
            fprintf(stderr, "Memory allocation error\n");
          }
        } else {
          /*archive_read_data_skip(pkg);*/
        }
        free(pathname);
      }
    }
  }
#if ARCHIVE_VERSION_NUMBER < 3000000
  archive_write_finish(pkg);
#else
  archive_write_free(pkg);
#endif

  //create metadata files
  {
    struct memory_buffer* pkginfopath = memory_buffer_create();
    struct memory_buffer* filepath = memory_buffer_create();
    //determine package information path
    memory_buffer_set_printf(pkginfopath, "%s%s%c%s%c", basepath, PACKAGE_INFO_PATH, PATH_SEPARATOR, metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME], PATH_SEPARATOR);
    //create package information path if needed
    recursive_mkdir(memory_buffer_get(pkginfopath));
    //create version file
    write_to_file(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_VERSION_FILE)), metadata->datafield[PACKAGE_METADATA_INDEX_VERSION]);
    //create file listing file
    sorted_unique_list_save_to_file(metadata->filelist, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_CONTENT_FILE)));
    //create folder listing file
    sorted_unique_list_save_to_file(metadata->folderlist, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_FOLDERS_FILE)));
    //create dependency files
    sorted_unique_list_save_to_file(metadata->dependencies, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_DEPENDENCIES_FILE)));
    sorted_unique_list_save_to_file(metadata->optionaldependencies, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_OPTIONALDEPENDENCIES_FILE)));
    sorted_unique_list_save_to_file(metadata->builddependencies, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_BUILDDEPENDENCIES_FILE)));
    //create metadata file
    /////TO DO
    //create folder with license file
    /////TO DO
    //clean up
    memory_buffer_free(pkginfopath);
    memory_buffer_free(filepath);
  }

/************/
  pkgdb_handle db = pkgdb_open(basepath);
  struct package_metadata_struct* my_metadata = pkgdb_read_package(db, metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
  package_metadata_free(my_metadata);
  pkgdb_uninstall_package(db, metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
  pkgdb_install_package(db, metadata);
  pkgdb_close(db);
/************/

  //show information on success or clean up on error
  if (!abort) {
    //show information
    printf("\rFinished ");
    if (!installedversion)
      printf("installing %s %s\n", metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME], metadata->datafield[PACKAGE_METADATA_INDEX_VERSION]);
/*
    if (strcmp(metadata->datafield[PACKAGE_METADATA_INDEX_VERSION], installedversion) == 0)
      printf("reinstalling %s %s\n", metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME], metadata->datafield[PACKAGE_METADATA_INDEX_VERSION]);
*/
    else
      printf("updating %s %s (previous version: %s)\n", metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME], metadata->datafield[PACKAGE_METADATA_INDEX_VERSION], installedversion);
  } else {
    //clean up
    struct memory_buffer* pkginfopath;
    struct memory_buffer* path;
    fprintf(stderr, "Error installing %s %s, uninstalling\n", metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME], metadata->datafield[PACKAGE_METADATA_INDEX_VERSION]);
    path = memory_buffer_create();
    //remove metadata files
    pkginfopath = memory_buffer_create();
    memory_buffer_set_printf(pkginfopath, "%s%s%c%s", basepath, PACKAGE_INFO_PATH, PATH_SEPARATOR, metadata->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
    unlink(memory_buffer_get(memory_buffer_set_printf(path, "%s%c%s", memory_buffer_get(pkginfopath), PATH_SEPARATOR, PACKAGE_INFO_VERSION_FILE)));
    unlink(memory_buffer_get(memory_buffer_set_printf(path, "%s%c%s", memory_buffer_get(pkginfopath), PATH_SEPARATOR, PACKAGE_INFO_CONTENT_FILE)));
    unlink(memory_buffer_get(memory_buffer_set_printf(path, "%s%c%s", memory_buffer_get(pkginfopath), PATH_SEPARATOR, PACKAGE_INFO_FOLDERS_FILE)));
    unlink(memory_buffer_get(memory_buffer_set_printf(path, "%s%c%s", memory_buffer_get(pkginfopath), PATH_SEPARATOR, PACKAGE_INFO_DEPENDENCIES_FILE)));
    unlink(memory_buffer_get(memory_buffer_set_printf(path, "%s%c%s", memory_buffer_get(pkginfopath), PATH_SEPARATOR, PACKAGE_INFO_OPTIONALDEPENDENCIES_FILE)));
    unlink(memory_buffer_get(memory_buffer_set_printf(path, "%s%c%s", memory_buffer_get(pkginfopath), PATH_SEPARATOR, PACKAGE_INFO_BUILDDEPENDENCIES_FILE)));
    rmdir(memory_buffer_get(pkginfopath));
    memory_buffer_create(pkginfopath);
    //remove files
    int i;
    int n;
    const char* s;
    n = sorted_unique_list_size(metadata->filelist);
    i = 0;
    while (i < n) {
      s = sorted_unique_list_get(metadata->filelist, i);
      unlink(memory_buffer_get(memory_buffer_set_printf(path, "%s%c%s", basepath, PATH_SEPARATOR, s)));
    }
    /////TO DO: remove (empty an no longer used) folders
    memory_buffer_create(path);
  }

  //clean up
  free(installedversion);
  sorted_unique_list_free(installedfilelist);
  package_metadata_free(metadata);
  free(arch);
  free(packagefilename);
  return abort;
}

/////TO DO: make folder .license and copy license file(s)
/////TO DO: clean up (empty) folders
/////TO DO: allow installation of multiple packages

