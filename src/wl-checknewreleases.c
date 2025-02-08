#include "winlibs_common.h"
#define _GNU_SOURCE     //needed for asprintf
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <pthread.h>
#ifdef USE_MYHTML
#include <myhtml/api.h>
#else
#include <gumbo.h>
#endif
#ifdef STATIC
#define PCRE2_STATIC
#endif
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <miniargv.h>
#include <versioncmp.h>
#include "sorted_unique_list.h"
#include "sorted_item_queue.h"
#include "pkgfile.h"
#include "downloader.h"
#include "common_output.h"
#include "version_check_db.h"
#include "memory_buffer.h"
#include "handle_interrupts.h"

#define PROGRAM_NAME            "wl-checknewreleases"
#define PROGRAM_DESC            "Command line utility to check for new source versions since the previous run"
#define PROGRAM_NAME_VERSION    PROGRAM_NAME " " WINLIBS_VERSION_STRING
#define PROGRAM_USER_AGENT      PROGRAM_NAME "/" WINLIBS_VERSION_STRING

#define DEFAULT_CACHE_LIFETIME  14400
#define DEFAULT_DATABASE        "versioncheck.sq3"
#define DEFAULT_THREADS         8

#define VERSION_REGEX "^(%s[-._ ]?|%s)?(v|version[- ]?|v\\.|r|release[- ]?|r\\.|)([0-9]{1,}([-._][0-9]{1,}){0,16}(|[-._][0-9a-z]{1,16}|[-._ ]\\(?(src|source|amalgamation|release|stable|final|bugfix)\\)?))(|\\.md5|\\.md5sum|\\.sha1|\\.sha1sum|\\.sha256|\\.sha256sum|\\.news|\\.changes|\\.changelog)(%s|\\.tar\\.gz|\\.tgz|\\.tar\\.bz|\\.tar\\.bz2|\\.tbz2|\\.tar\\.xz|\\.txz|\\.tar\\.lz|\\.tlz|\\.zip|\\.7z|)$"
#define VERSION_REGEX_VECTOR 3

#define STRINGIZE_(value) #value
#define STRINGIZE(value) STRINGIZE_(value)

unsigned int interrupted = 0;   //variable set by signal handler

////////////////////////////////////////////////////////////////////////

/* quote data so it can be used in regular expression */
char* quote_for_regex (const char* data)
{
  char* result;
  const char* p;
  char* q;
  if (!data)
    return NULL;
  if ((result = (char*)malloc(strlen(data) * 2 + 1)) == NULL)
    return NULL;
  p = data;
  q = result;
  while (*p) {
    switch (*p) {
/*
      case '\0':
      case '\a':
      case '\b':
      case '\f':
      case '\n':
      case '\r':
      case '\t':
      case '\v':
*/
      case '\\':
      case '.':
      case '?':
      case '*':
      case '+':
      case '\'':
      case '\"':
        *q++ = '\\';
      default:
        *q++ = *p++;
        break;
    }
  }
  *q = 0;
  return result;
  /////TO DO: quote leading - (case: espeak: export DOWNLOADURL="http://espeak.sourceforge.net/download.html espeak- -source.zip")
}

////////////////////////////////////////////////////////////////////////

struct search_html_for_versions_struct {
  pcre2_code* re;
  pcre2_match_data *match_data;
  PCRE2_SIZE* ovector;
  sorted_unique_list* versions;
  sorted_unique_list* subfolders;
  unsigned int level;
  char* url;
};

void process_html_tag_a (const char* href, const char* rel, struct search_html_for_versions_struct* data)
{
  const char* filepart;
  size_t filepartlen;
  char* filename;
  if (href && (!rel || strcasecmp(rel, "nofollow") != 0) && (filepart = get_url_download_file_part(href, &filepartlen)) != NULL) {
    //URL decode
    if ((filename = url_decode(filepart, filepartlen)) != NULL) {
      if (pcre2_match(data->re, (PCRE2_UCHAR*)filename, PCRE2_ZERO_TERMINATED, 0, 0, data->match_data, NULL) > 0) {
        //check if this is a subfolder (only inside main link)
        if (data->level == 0 && *href && href[strlen(href) - 1] == '/') {
          //subfolder: add link to list of subfolders only if at the top level
          if (data->level == 0)
            sorted_unique_list_add(data->subfolders, href);
        } else {
          //not a subfolder: add extracted version to list of versions
          //if (pcre2_get_ovector_count(data->match_data) >= VERSION_REGEX_VECTOR)
          sorted_unique_list_add_buf(data->versions, filename + data->ovector[VERSION_REGEX_VECTOR * 2 + 0], data->ovector[VERSION_REGEX_VECTOR * 2 + 1] - data->ovector[VERSION_REGEX_VECTOR * 2 + 0]);
        }
      }
      free(filename);
    }
  }
}

#ifdef USE_MYHTML

void search_html_for_versions (const char* htmldata, struct search_html_for_versions_struct* data)
{
  myhtml_t* myhtml;
  myhtml_tree_t* tree;
  myhtml_collection_t* collection;
  myhtml_tree_attr_t* attr;
  const char* href;
  const char* rel;
  if ((myhtml = myhtml_create()) == NULL)
    return;
  if (myhtml_init(myhtml, MyHTML_OPTIONS_DEFAULT, 1, 0) == MyHTML_STATUS_OK) {
    if ((tree = myhtml_tree_create()) != NULL) {
      if (myhtml_tree_init(tree, myhtml) == MyHTML_STATUS_OK) {
        //parse HTML
        if (myhtml_parse(tree, MyENCODING_UTF_8, htmldata, strlen(htmldata)) == MyHTML_STATUS_OK) {
          //get collection of links
          if ((collection = myhtml_get_nodes_by_name(tree, NULL, "a", 1, NULL)) != NULL) {
            //iterate through all nodes in collection
            for (size_t i = 0; i < collection->length; ++i) {
              href = NULL;
              if ((attr = myhtml_attribute_by_key(collection->list[i], "href", 4)) != NULL)
                href = myhtml_attribute_value(attr, NULL);
              rel = NULL;
              if ((attr = myhtml_attribute_by_key(collection->list[i], "rel", 3)) != NULL)
                rel = myhtml_attribute_value(attr, NULL);
              process_html_tag_a(href, rel, data);
            }
          }
        }
      }
      myhtml_tree_destroy(tree);
    }
  }
  myhtml_destroy(myhtml);
}

#else

void search_gumbo_node_for_versions (GumboNode* node, struct search_html_for_versions_struct* data)
{
  GumboAttribute* href;
  GumboAttribute* rel;
  GumboVector* children;
  unsigned int i;
  //abort processing if not element node
  if (node->type != GUMBO_NODE_ELEMENT)
    return;
  //check if this is a link
  if (node->v.element.tag == GUMBO_TAG_A && (href = gumbo_get_attribute(&node->v.element.attributes, "href")) != NULL) {
    rel = gumbo_get_attribute(&node->v.element.attributes, "rel");
    process_html_tag_a(href->value, (rel ? rel->value : NULL), data);
  }
  //recurse through child nodes
  children = &node->v.element.children;
  for (i = 0; i < children->length; i++)
    search_gumbo_node_for_versions((GumboNode*)(children->data[i]), data);
}

void search_html_for_versions (const char* htmldata, struct search_html_for_versions_struct* data)
{
  GumboOutput* parsedhtml;
  parsedhtml = gumbo_parse(htmldata);
  //parsedhtml = gumbo_parse_with_options(&kGumboDefaultOptions, htmldata, strlen(htmldata));
  search_gumbo_node_for_versions(parsedhtml->root, data);
  gumbo_destroy_output(&kGumboDefaultOptions, parsedhtml);
}

#endif

////////////////////////////////////////////////////////////////////////

static void search_text_list_versions (const char* text, struct search_html_for_versions_struct* data)
{
  const char* beginpos;
  const char* endpos;
  beginpos = text;
  while (*beginpos) {
    endpos = beginpos;
    while (*endpos && *endpos != '\r' && *endpos != '\n')
      endpos++;
    if (pcre2_match(data->re, (PCRE2_UCHAR*)beginpos, endpos - beginpos, 0, 0, data->match_data, NULL) > 0) {
      //if (pcre2_get_ovector_count(data->match_data) >= VERSION_REGEX_VECTOR)
      sorted_unique_list_add_buf(data->versions, beginpos + data->ovector[VERSION_REGEX_VECTOR * 2 + 0], data->ovector[VERSION_REGEX_VECTOR * 2 + 1] - data->ovector[VERSION_REGEX_VECTOR * 2 + 0]);
    }
    while (*endpos && (*endpos == '\r' || *endpos == '\n'))
      endpos++;
    beginpos = endpos;
  }
}

////////////////////////////////////////////////////////////////////////

typedef int (*package_version_callback_fn)(const char* packagename, const char* version, void* callbackdata);

struct check_package_versions_struct {
  const char* packageinfopath;
  struct commonoutput_stuct* logoutput;
  struct commonoutput_stuct* reportoutput;
  //struct downloadcache* cache;
  struct downloadcachedb* cachedb;
  struct versioncheckmasterdb* versionmasterdb;
  unsigned long limitsuburls;
  size_t totalpackages;
};

struct check_package_versions_thread_info_struct {
  int threadindex;
  struct downloader* dl;
  struct versioncheckdb* versiondb;
};

sorted_unique_list* get_package_versions_from_url (struct check_package_versions_struct* info, struct check_package_versions_thread_info_struct* threadinfo, const char* url, const char* basename, const char* filematchprefix, const char* filematchsuffix, long* presponsecode, char** pstatus)
{
  char* versionmatchregex;
  int rc;
  PCRE2_SIZE erroroffset;
  char* prefix;
  char* suffix;
  char* mimetype;
  char* htmldata;
  struct search_html_for_versions_struct data;
  struct download_info_struct urlinfo;
  //compile regular expression to determine version
  prefix = quote_for_regex(filematchprefix);
  suffix = quote_for_regex(filematchsuffix);
  if (asprintf(&versionmatchregex, VERSION_REGEX, basename, (prefix ? prefix : ""), (suffix ? suffix : "")) < 0) {
    commonoutput_printf(info->logoutput, -1, "[%i] Memory allocation error in asprintf()", threadinfo->threadindex);
    exit(13);
  }
  free(prefix);
  free(suffix);
  if ((data.re = pcre2_compile((PCRE2_UCHAR*)versionmatchregex, PCRE2_ZERO_TERMINATED, PCRE2_UTF | PCRE2_CASELESS, &rc, &erroroffset, NULL))  == NULL) {
    PCRE2_UCHAR buffer[256];
    pcre2_get_error_message(rc, buffer, sizeof(buffer));
    commonoutput_printf(info->logoutput, 0, "[%i] PCRE2 compilation failed at offset %i: %s, expression: %s\n", threadinfo->threadindex, (int)erroroffset, buffer, versionmatchregex);
    commonoutput_flush(info->logoutput);
    free(versionmatchregex);
    return NULL;
  }
  free(versionmatchregex);
  //initialize lists
  if ((data.versions = sorted_unique_list_create(versioncmp, free)) == NULL) {
    commonoutput_printf(info->logoutput, -1, "[%i] Memory allocation error in sorted_unique_list_create()", threadinfo->threadindex);
    exit(11);
  }
  if ((data.subfolders = sorted_unique_list_create(versioncmpreverse, free)) == NULL) {
    commonoutput_printf(info->logoutput, -1, "[%i] Memory allocation error in sorted_unique_list_create()", threadinfo->threadindex);
    exit(12);
  }
  //create match_data block for holding result
  data.match_data = pcre2_match_data_create_from_pattern(data.re, NULL);
  //get output vector
  data.ovector = pcre2_get_ovector_pointer(data.match_data);
  //load and parse data from URL
  commonoutput_printf(info->logoutput, 1, "[%i] %s: loading URL: %s\n", threadinfo->threadindex, basename, url);
  commonoutput_flush(info->logoutput);
  data.level = 0;
  data.url = NULL;
  mimetype = NULL;
  urlinfo.status = pstatus;
  urlinfo.actualurl = &data.url;
  urlinfo.mimetype = &mimetype;
  htmldata = downloader_get_file(threadinfo->dl, url, &urlinfo);
  if (presponsecode)
    *presponsecode = urlinfo.responsecode;
  if (!htmldata) {
    commonoutput_printf(info->logoutput, 1, "[%i] %s: failed loading URL: %s%s%s\n", threadinfo->threadindex, basename, url, (urlinfo.status && *urlinfo.status ? " - " : ""), (urlinfo.status && *urlinfo.status ? *urlinfo.status : ""));
    commonoutput_flush(info->logoutput);
  } else {
    unsigned int i;
    unsigned int n;
    char* suburl;
    //process downloaded contents
    if (mimetype_is_html(mimetype)) {
      //parse HTML
      search_html_for_versions(htmldata, &data);
    } else if (mimetype && strcmp(mimetype, custom_mimetype_ftp_listing) == 0) {
      search_text_list_versions(htmldata, &data);
    } else {
      commonoutput_printf(info->logoutput, 1, "[%i] %s: unsupported MIME type \"%s\" from URL: %s\n", threadinfo->threadindex, basename, (mimetype ? mimetype : "(none)"), url);
      commonoutput_flush(info->logoutput);
    }
    free(htmldata);
    //process subfolders
    n = sorted_unique_list_size(data.subfolders);
    commonoutput_printf(info->logoutput, 2, "[%i] %s: done loading URL%s (%u subfolders found): %s\n", threadinfo->threadindex, basename, (urlinfo.cached ? " from cache" : ""), n, url);
    commonoutput_flush(info->logoutput);
    if (info->limitsuburls && n > info->limitsuburls)
      n = info->limitsuburls;
    urlinfo.status = NULL;
    urlinfo.actualurl = NULL;
    data.level = 1;
    for (i = 0; !interrupted && i < n; i++) {
      if ((suburl = resolve_url(data.url, sorted_unique_list_get(data.subfolders, i))) != NULL) {
        //load and parse data from URL
        commonoutput_printf(info->logoutput, 2, "[%i] %s: loading sub-URL [%u/%u]: %s\n", threadinfo->threadindex, basename, i + 1, n, suburl);
        commonoutput_flush(info->logoutput);
        free(mimetype);
        mimetype = NULL;
        if ((htmldata = downloader_get_file(threadinfo->dl, suburl, &urlinfo)) == NULL) {
          commonoutput_printf(info->logoutput, 2, "[%i] %s: failed loading sub-URL [%u/%u]: %s\n", threadinfo->threadindex, basename, i + 1, n, suburl);
        } else {
          commonoutput_printf(info->logoutput, 3, "[%i] %s: done loading sub-URL%s [%u/%u]: %s\n", threadinfo->threadindex, basename, (urlinfo.cached ? " from cache" : ""), i + 1, n, suburl);
          //process downloaded contents
          if (mimetype_is_html(mimetype)) {
            search_html_for_versions(htmldata, &data);
          } else if (strcmp(mimetype, custom_mimetype_ftp_listing) == 0) {
            search_text_list_versions(htmldata, &data);
          } else {
            commonoutput_printf(info->logoutput, 1, "[%i] %s: unsupported MIME type \"%s\" from URL: %s\n", threadinfo->threadindex, basename, mimetype, url);
          }
          free(htmldata);
        }
        commonoutput_flush(info->logoutput);
        free(suburl);
      }
    }
  }
  //clean up
  free(mimetype);
  free(data.url);
  pcre2_match_data_free(data.match_data);
  pcre2_code_free(data.re);
  sorted_unique_list_free(data.subfolders);
  return data.versions;
}

struct new_version_callback_struct {
  size_t counter;
  time_t starttime;
  struct package_metadata_struct* pkginfo;
  struct memory_buffer* outputbuffer;
};

int new_version_callback (const char* version, struct new_version_callback_struct* callbackdata)
{
  //display package information before the first entry
  if (callbackdata->counter++ == 0) {
    char buf[20] = "(missing timestamp)";
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&callbackdata->starttime));
    memory_buffer_append_printf(callbackdata->outputbuffer, "%s - %s - line %lu - lastest version: %s%s\n", buf, callbackdata->pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], (unsigned long)(callbackdata->pkginfo->nextversion_linenumber ? callbackdata->pkginfo->nextversion_linenumber : callbackdata->pkginfo->version_linenumber), callbackdata->pkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION], (callbackdata->pkginfo->buildok ? "" : " (not building)"));
  }
  //display version
  memory_buffer_append_printf(callbackdata->outputbuffer, "  %s\n", (version ? version : "NULL"));
  return 0;
}

int check_package_versions (struct check_package_versions_struct* info, struct check_package_versions_thread_info_struct* threadinfo, const char* packagename)
{
  char* url;
  char* prefix;
  char* suffix;
  long responsecode;
  char* status;
  sorted_unique_list* versions;
  struct package_metadata_struct* pkginfo;
  time_t starttime = time(NULL);
  //read package information
  if ((pkginfo = read_packageinfo(info->packageinfopath, packagename)) == NULL) {
    commonoutput_printf(info->logoutput, 0, "[%i] Error: package information not found for package: %s", threadinfo->threadindex, packagename);
    commonoutput_flush(info->logoutput);
    return 0;
  }
  //get versions from package download page (if specified)
  get_package_downloadurl_info(pkginfo, &url, &prefix, &suffix);
  if (!url || !*url) {
    versioncheckdb_update_package(threadinfo->versiondb, packagename, -1, "No URL", pkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION], pkginfo->datafield[PACKAGE_METADATA_INDEX_DOWNLOADURL]);
  } else {
    unsigned int i;
    unsigned int n = 0;
    responsecode = 0;
    status = NULL;
    if ((versions = get_package_versions_from_url(info, threadinfo, url, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], prefix, suffix, &responsecode, &status)) != NULL && (n = sorted_unique_list_size(versions)) > 0) {
      versioncheckdb_group_start(threadinfo->versiondb);
      for (i = 0; i < n; i++) {
        versioncheckdb_update_package_version(threadinfo->versiondb, packagename, sorted_unique_list_get(versions, i));
      }
      versioncheckdb_update_package(threadinfo->versiondb, packagename, responsecode, status, pkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION], pkginfo->datafield[PACKAGE_METADATA_INDEX_DOWNLOADURL]);
      versioncheckdb_group_end(threadinfo->versiondb);
    } else {
      versioncheckdb_update_package(threadinfo->versiondb, packagename, responsecode, status, pkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION], pkginfo->datafield[PACKAGE_METADATA_INDEX_DOWNLOADURL]);
    }
    if (versions)
      sorted_unique_list_free(versions);
    //report detected new versions
    if (n > 0) {
      struct new_version_callback_struct newpackagedata = {0, starttime, pkginfo, NULL};
      if ((newpackagedata.outputbuffer = memory_buffer_create()) != NULL) {
        versioncheckdb_list_new_package_versions(threadinfo->versiondb, packagename, starttime, (versioncheckdb_list_new_package_versions_callback_fn)new_version_callback, &newpackagedata);
        if (newpackagedata.outputbuffer->datalen)
          commonoutput_putbuf(info->reportoutput, -9999, newpackagedata.outputbuffer->data, newpackagedata.outputbuffer->datalen);
        memory_buffer_free(newpackagedata.outputbuffer);
        commonoutput_flush(info->reportoutput);
      }
    }
    free(status);
  }
  //clean up
  free(url);
  free(prefix);
  free(suffix);
  package_metadata_free(pkginfo);
  return 0;
}

////////////////////////////////////////////////////////////////////////

struct package_thread_struct {
  pthread_t thread;
  int threadindex;
  struct check_package_versions_struct* info;
  struct sorted_item_queue_struct* packagelist;
  size_t count;
  struct check_package_versions_thread_info_struct threadinfo; //initialized inside the thread
};

void* package_thread (struct package_thread_struct* threaddata)
{
  char* packagename;
  size_t remaining;
  //initialize
  threaddata->threadinfo.threadindex = threaddata->threadindex;
  //open version database file
  if ((threaddata->threadinfo.versiondb = versioncheckdb_open(threaddata->info->versionmasterdb)) == NULL) {
    commonoutput_printf(threaddata->info->logoutput, -1, "[%i] Error opening version database\n", threaddata->threadinfo.threadindex);
    commonoutput_flush(threaddata->info->logoutput);
    return NULL;
  }
  //create downloader
  if ((threaddata->threadinfo.dl = downloader_create(PROGRAM_USER_AGENT, threaddata->info->cachedb, threaddata->info->logoutput)) == NULL) {
    commonoutput_printf(threaddata->info->logoutput, -1, "[%i] Error in downloader_create()", threaddata->threadinfo.threadindex);
    commonoutput_flush(threaddata->info->logoutput);
    versioncheckdb_close(threaddata->threadinfo.versiondb);
    return NULL;
  }
  //process packages as long as there are any in the queue
  while (!interrupted && (packagename = sorted_item_queue_take_next(threaddata->packagelist, &remaining)) != NULL) {
    threaddata->count++;
    commonoutput_printf(threaddata->info->logoutput, 1, "[%i] Package [%lu/%lu]: %s\n", threaddata->threadinfo.threadindex, (unsigned long)(threaddata->info->totalpackages - remaining), (unsigned long)threaddata->info->totalpackages, packagename);
    commonoutput_flush(threaddata->info->logoutput);
    check_package_versions(threaddata->info, &threaddata->threadinfo, packagename);
    free(packagename);
    sched_yield();
  }
  //clean up
  downloader_free(threaddata->threadinfo.dl);
  threaddata->threadinfo.dl = NULL;
  versioncheckdb_close(threaddata->threadinfo.versiondb);
  commonoutput_printf(threaddata->info->logoutput, 2, "[%i] Thread finished after processing %lu packages\n", threaddata->threadinfo.threadindex, (unsigned long)threaddata->count);
  commonoutput_flush(threaddata->info->logoutput);
  return NULL;
}

DEFINE_INTERRUPT_HANDLER_BEGIN(handle_break_signal)
{
  interrupted++;
}
DEFINE_INTERRUPT_HANDLER_END(handle_break_signal)

int main (int argc, char** argv, char *envp[])
{
  int i;
  time_t starttime;
  struct sorted_item_queue_struct* packagelist;
  struct check_package_versions_struct info;
  int showversion = 0;
  int showhelp = 0;
  int verbose = 0;
  int numthreads = DEFAULT_THREADS;
  unsigned long cache_expiration = DEFAULT_CACHE_LIFETIME;
  const char* cachedbfile = NULL;
  const char* versiondbpath = DEFAULT_DATABASE;
  const char* logfile = "";
  const char* reportfile = "";
  //initialize
  info.limitsuburls = 0;
  info.packageinfopath = NULL;
  if ((packagelist = sorted_item_queue_create(versioncmp)) == NULL) {
    fprintf(stderr, "Memory allocation error in sorted_item_queue_create()\n");
    return 1;
  }
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",            NULL,      miniargv_cb_increment_int, &showhelp,             "show command line help", NULL},
    {0,   "version",         NULL,      miniargv_cb_increment_int, &showversion,          "show version information", NULL},
    {'s', "source-path",     "PATH",    miniargv_cb_set_const_str, &info.packageinfopath, "build recipe path\noverrides environment variable BUILDSCRIPTS\ncan be multiple paths separated by \"" WINLIBS_CHR2STR(PATHLIST_SEPARATOR) "\"", NULL},
    {'d', "db-file",         "FILE",    miniargv_cb_set_const_str, &versiondbpath,        "version database file (default: " DEFAULT_DATABASE ")", NULL},
    {'c', "cache",           "FILE",    miniargv_cb_set_const_str, &cachedbfile,          "set cache database (\"-\" for none, default: temporary database)", NULL},
    {'x', "cache-expires",   "S",       miniargv_cb_set_long/*u*/, &cache_expiration,     "set cache expiration in seconds or zero for none (default: " STRINGIZE(DEFAULT_CACHE_LIFETIME) "s)", NULL},
    {'j', "threads",         "N",       miniargv_cb_set_int,       &numthreads,           "set the number of simultaneus threads to use (default: " STRINGIZE(DEFAULT_THREADS) ")", NULL},
    {'n', "sub-limit",       "N",       miniargv_cb_set_long/*u*/, &info.limitsuburls,    "only get this many of the latest version subdirectories, zero for no limit (default)", NULL},
    {'o', "report",          "FILE",    miniargv_cb_set_const_str, &reportfile,           "report file to append to (default: standard output)", NULL},
    {'l', "log",             "FILE",    miniargv_cb_set_const_str, &logfile,              "log file to append to (default: standard output)", NULL},
    {'v', "verbose",         NULL,      miniargv_cb_increment_int, &verbose,              "verbose mode, can be specified multiple times to increase verbosity", NULL},
    {0,   NULL,              "PACKAGE", miniargv_cb_error,         NULL,                  "package(s) to build (or \"all\" to list all packages)", NULL},
    MINIARGV_DEFINITION_END
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "BUILDSCRIPTS",    NULL,      miniargv_cb_set_const_str, &info.packageinfopath, "build recipe path", NULL},
    MINIARGV_DEFINITION_END
  };
  //parse environment and command line flags
  if (miniargv_process_env(envp, envdef, NULL, NULL) != 0)
    return 1;
  if (miniargv_process_arg_flags(argv, argdef, NULL, NULL) != 0)
    return 1;
  //show help if requested or if no command line arguments were given
  if (showhelp || argc <= 1) {
    printf(
      PROGRAM_NAME " - version " WINLIBS_VERSION_STRING " - " WINLIBS_LICENSE " - " WINLIBS_CREDITS "\n"
      PROGRAM_DESC "\n"
      "Usage: " PROGRAM_NAME " "
    );
    miniargv_arg_list(argdef, 1);
    printf("\n");
    miniargv_help(argdef, envdef, 32, 0);
    printf(
      "Remarks:\n"
      "  - Either -s or environment variable BUILDSCRIPTS must be set.\n"
    );
#ifdef PORTCOLCON_VERSION
    printf(WINLIBS_HELP_COLOR);
#endif
    return 0;
  }
  //show version information if requested
  if (showversion) {
    printf(PROGRAM_NAME " - version " WINLIBS_VERSION_STRING " - " WINLIBS_LICENSE " - " WINLIBS_CREDITS "\n");
    return 0;
  }
  //process command line argument values
  i = 0;
  while ((i = miniargv_get_next_arg_param(i, argv, argdef, NULL)) > 0) {
    if (strcasecmp(argv[i], "all") == 0) {
      iterate_packages(info.packageinfopath, sorted_item_queue_add_callback, packagelist);
    } else {
      sorted_item_queue_add(packagelist, argv[i]);
    }
  }
  //check parameters
  if (sorted_item_queue_size(packagelist) <= 0) {
    fprintf(stderr, "No packages specified.\n");
    return 2;
  }
  //install signal handler
  INSTALL_INTERRUPT_HANDLER(handle_break_signal)
  //initialize downloader
  downloader_global_init();
  //open report output
  if ((info.reportoutput = commonoutput_create(reportfile, verbose)) == NULL) {
    fprintf(stderr, "Error opening report file: %s\n", (logfile ? logfile : "(standard output)"));
    return 1;
  }
  //open log output
  if (strcmp(logfile, reportfile) == 0)
    info.logoutput = info.reportoutput;
  else if ((info.logoutput = commonoutput_create(logfile, verbose)) == NULL) {
    fprintf(stderr, "Error opening log file: %s\n", (logfile ? logfile : "(standard output)"));
    return 2;
  }
  //open version database file
  if ((info.versionmasterdb = versioncheckmasterdb_open(versiondbpath)) == NULL) {
    fprintf(stderr, "Error opening version database file: %s\n", versiondbpath);
    return 3;
  }
  //open cache database
  if (cachedbfile && strcmp(cachedbfile, "-") == 0) {
    info.cachedb = NULL;
  } else if ((info.cachedb = downloadcachedb_create(cachedbfile, cache_expiration)) == NULL) {
    fprintf(stderr, "Unable to open download cache database (%s), proceeding without cache\n", (cachedbfile ? cachedbfile : "temporary database"));
  } else if (cachedbfile && cache_expiration != 0 && !downloadcachedb_is_new(info.cachedb)) {
    int n;
    commonoutput_printf(info.logoutput, 1, "Purging entries older than %lu seconds from cache database: %s\n", cache_expiration, cachedbfile);
    commonoutput_flush(info.logoutput);
    if ((n = downloadcachedb_purge(info.cachedb)) < 0)
      commonoutput_printf(info.logoutput, 0, "Error purging cache\n");
    else
      commonoutput_printf(info.logoutput, 2, "Cache entries deleted: %i\n", n);
    commonoutput_flush(info.logoutput);
  }
  //process each package in the list using multiple threads
  struct package_thread_struct* threaddata;
  size_t count;
  if ((threaddata = (struct package_thread_struct*)malloc(numthreads * sizeof(struct package_thread_struct))) == NULL) {
    fprintf(stderr, "Memory allocation error\n");
    return 4;
  }
  starttime = time(NULL);
  info.totalpackages = sorted_item_queue_size(packagelist);
  if (numthreads > info.totalpackages)
    numthreads = info.totalpackages;
  commonoutput_printf(info.logoutput, 2, "Checking %lu packages using %i simultaneous threads\n", (unsigned long)info.totalpackages, numthreads);
  commonoutput_flush(info.logoutput);
  for (i = 0; i < numthreads; i++) {
    threaddata[i].threadindex = i;
    threaddata[i].info = &info;
    threaddata[i].packagelist = packagelist;
    threaddata[i].count = 0;
    if (pthread_create(&threaddata[i].thread, NULL, (void *(*)(void*))package_thread, &threaddata[i]) != 0) {
      fprintf(stderr, "Unable to create thread\n");
      return 5;
    }
  }
  //wait for all threads to finish
  count = 0;
  for (i = 0; i < numthreads; i++) {
    pthread_join(threaddata[i].thread, NULL);
    count += threaddata[i].count;
  }
  commonoutput_printf(info.logoutput, 1, "Finished, total packages processed: %lu\n", count);
  commonoutput_printf(info.logoutput, 3, "Total run time: %lu seconds\n", (unsigned long)(time(NULL) - starttime));
  free(threaddata);
  //purge cache if needed
  if (cachedbfile && info.cachedb)
    downloadcachedb_purge(info.cachedb);
  //clean up
  downloadcachedb_free(info.cachedb);
  versioncheckmasterdb_close(info.versionmasterdb);
  commonoutput_free(info.reportoutput);
  if (strcmp(logfile, reportfile) != 0)
    commonoutput_free(info.logoutput);
  //clean up
  sorted_item_queue_free(packagelist);
  downloader_global_cleanup();
  return 0;
}

/*
TO DO:
  - don't follow links to URLs like:
      https://www.linkedin.com/company/
  - cache download errors
  - test happens what if the network connection is interrupted or unavailable?
  - use blobs for cached page content
*/

/*
==7123==
==7123== HEAP SUMMARY:
==7123==     in use at exit: 268,183 bytes in 4,736 blocks
==7123==   total heap usage: 293,152,839 allocs, 293,148,103 frees, 102,829,795,121 bytes allocated
==7123==
==7123== 3,783 (48 direct, 3,735 indirect) bytes in 3 blocks are definitely lost in loss record 4 of 6
==7123==    at 0x4837B65: calloc (vg_replace_malloc.c:752)
==7123==    by 0x521659A: ??? (in /usr/lib/x86_64-linux-gnu/libgnutls.so.30.23.2)
==7123==    by 0x520F216: gnutls_session_set_data (in /usr/lib/x86_64-linux-gnu/libgnutls.so.30.23.2)
==7123==    by 0x4AD5173: ??? (in /usr/lib/x86_64-linux-gnu/libcurl-gnutls.so.4.5.0)
==7123==    by 0x4AD5EC9: ??? (in /usr/lib/x86_64-linux-gnu/libcurl-gnutls.so.4.5.0)
==7123==    by 0x4A7DD01: ??? (in /usr/lib/x86_64-linux-gnu/libcurl-gnutls.so.4.5.0)
==7123==    by 0x4A7F84A: ??? (in /usr/lib/x86_64-linux-gnu/libcurl-gnutls.so.4.5.0)
==7123==    by 0x4A8B0FE: ??? (in /usr/lib/x86_64-linux-gnu/libcurl-gnutls.so.4.5.0)
==7123==    by 0x4AA00E5: ??? (in /usr/lib/x86_64-linux-gnu/libcurl-gnutls.so.4.5.0)
==7123==    by 0x4AA11C0: curl_multi_perform (in /usr/lib/x86_64-linux-gnu/libcurl-gnutls.so.4.5.0)
==7123==    by 0x4A97EB9: curl_easy_perform (in /usr/lib/x86_64-linux-gnu/libcurl-gnutls.so.4.5.0)
==7123==    by 0x111B9A: downloader_get_file (downloader.c:165)
==7123==
==7123== 264,208 (56 direct, 264,152 indirect) bytes in 1 blocks are definitely lost in loss record 6 of 6
==7123==    at 0x483577F: malloc (vg_replace_malloc.c:299)
==7123==    by 0x4867A65: avl_insert (in /usr/lib/libavl.so.1.5)
==7123==    by 0x112C04: sorted_unique_list_add (sorted_unique_list.c:45)
==7123==    by 0x1135B7: sorted_item_queue_add (sorted_item_queue.c:79)
==7123==    by 0x1135F1: sorted_item_queue_add_callback (sorted_item_queue.c:90)
==7123==    by 0x10F06D: iterate_packages (package_info.c:302)
==7123==    by 0x10DAE1: main (wl-checknewreleases.c:754)
==7123==
==7123== LEAK SUMMARY:
==7123==    definitely lost: 104 bytes in 4 blocks
==7123==    indirectly lost: 267,887 bytes in 4,720 blocks
==7123==      possibly lost: 0 bytes in 0 blocks
==7123==    still reachable: 192 bytes in 12 blocks
==7123==         suppressed: 0 bytes in 0 blocks
==7123== Reachable blocks (those to which a pointer was found) are not shown.
==7123== To see them, rerun with: --leak-check=full --show-leak-kinds=all
==7123==
==7123== For counts of detected and suppressed errors, rerun with: -v
==7123== ERROR SUMMARY: 2 errors from 2 contexts (suppressed: 0 from 0)
*/
