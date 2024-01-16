#include "winlibs_common.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <time.h>
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
#include <archive.h>
#include <archive_entry.h>
#include "memory_buffer.h"
#include "sorted_unique_list.h"
#include "filesystem.h"
#include "downloader.h"

#define PROGRAM_NAME            "winlibs-setup"
#define PROGRAM_DESC            "Installer for winlibs build environment"
#define PROGRAM_NAME_VERSION    PROGRAM_NAME " " WINLIBS_VERSION_STRING
#define PROGRAM_USER_AGENT      PROGRAM_NAME "/" WINLIBS_VERSION_STRING

#define MSYS2_REPO_URL          "http://repo.msys2.org/distrib/"
#define MSYS2_CHECK_FILE        "\\usr\\bin\\sh.exe"

#define ARCHIVE_READ_BLOCK_SIZE 4096

#if 0

unsigned int interrupted = 0;   //variable set by signal handler

////////////////////////////////////////////////////////////////////////

#define ISHEXDIGIT(c) ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
#define HEXVALUE(c) (unsigned char)(c >= '0' && c <= '9' ? c - '0' : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0)))

/* decode URL encoded string */
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

/* decode URL encoded string */
char* url_decode (const char* data, size_t datalen)
{
  char* result;
  size_t i;
  char* p;
  if (!data)
    return NULL;
  if ((result = (char*)malloc(datalen + 1)) == NULL)
    return NULL;
  i = 0;
  p = result;
  while (i < datalen) {
    if (data[i] == '+') {
      *p++ = ' ';
      i++;
    } else if (data[i] == '%' && i + 2 < datalen && ISHEXDIGIT(data[i + 1]) && ISHEXDIGIT(data[i + 2])) {
      *(unsigned char*)p++ = (HEXVALUE(data[i + 1]) << 4) | HEXVALUE(data[i + 2]);
      i += 3;
    } else {
      *p++ = data[i++];
    }
  }
  *p = 0;
  return result;
}

//return a pointer to the character right after "<scheme>://" or NULL if not found
const char* url_skip_scheme (const char* url)
{
  const char* p = url;
  while (*p && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '+' || *p == '-' || *p == '.'))
    p++;
  if (*p++ != ':')
    return NULL;
  if (*p++ != '/')
    return NULL;
  if (*p++ != '/')
    return NULL;
  return p;
}

/* resolve URL (absolute URLs are returned without change, relative URLs will be resolved) */
char* resolve_url (const char* base_url, const char* url)
{
  char* result;
  const char* afterscheme;
  size_t pos;
  size_t lastslashpos;
  if (!base_url || !*base_url || !url)
    return NULL;
  //check if URL is already absolute
  if (url_skip_scheme(url))
    return strdup(url);
  //abort if base URL doesn't contain scheme
  if ((afterscheme = url_skip_scheme(base_url)) == NULL)
    return NULL;
  if (*url == '/') {
    //URL starts at root, get position of first slash after scheme
    while (*afterscheme && *afterscheme != '/')
      afterscheme++;
    lastslashpos = afterscheme - base_url;
  } else {
    //URL is relative, get position of last slash
    pos = 0;
    lastslashpos = 0;
    while (base_url[pos] && base_url[pos] != '?' && base_url[pos] != '#') {
      if (base_url[pos] == '/')
        lastslashpos = pos;
      pos++;
    }
    if (lastslashpos == 0)
      lastslashpos = pos;
  }
  //create new URL
  if ((result = (char*)malloc(lastslashpos + strlen(url) + 2)) == NULL)
    return NULL;
  memcpy(result, base_url, lastslashpos);
  if (*url != '/')
    result[lastslashpos++] = '/';
  strcpy(result + lastslashpos, url);
  return result;
}

/* returns filename part of a URL, or name of deepest folder if URL ends with a slash */
const char* get_url_file_part (const char* url, size_t* filelen)
{
  size_t urllen = 0;
  const char *p = url;
  const char *result = p;
  int protocoldone = 0;
  const char* afterprotocol = NULL;
  size_t resultlen;
  if (!url || !*url)
    return NULL;
  while (url[urllen] && url[urllen] != '#')
    urllen++;
  resultlen = urllen;
  while (urllen-- > 0 && *p && *p != '?' && *p != '#') {
    if (*p == '/') {
      if (urllen == 0 && resultlen > 0) {
        resultlen--;
      } else {
        result = p + 1;
        resultlen = urllen;
      }
    }
    if (!protocoldone && *p == ':' && urllen >= 2) {
      if (p[1] == '/' && p[2] == '/') {
        protocoldone = 1;
        afterprotocol = p + 3;
      }
    }
    p++;
  }
  if (filelen)
    *filelen = resultlen;
  if (protocoldone && result == afterprotocol)
    return NULL;
  return (*result && resultlen ? result : NULL);
}

/* returns download filename part of a URL */
const char* get_url_download_file_part (const char* url, size_t* filelen)
{
  const char* result;
  size_t resultlen;
  result = get_url_file_part(url, &resultlen);
  if (result && *result && result != url && *(result - 1) == '/' && (strcasecmp(result, "download") == 0 || strcasecmp(result, "download/") == 0)) {
    const char* p = result - 1;
    while (p != url && *(p - 1) != '/')
      p--;
    resultlen = result - 1 - p;
    result = p;
  }
  if (filelen)
    *filelen = resultlen;
  return result;
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

int mimetype_cmp (const char* fullmimetype, const char* basemimetype)
{
  int result;
  size_t basemimetypelen;
  if (!fullmimetype || !*fullmimetype || !basemimetype || !*basemimetype)
    return -1;
  basemimetypelen = strlen(basemimetype);
  if ((result = strncasecmp(fullmimetype, basemimetype, basemimetypelen)) == 0) {
    if (fullmimetype[basemimetypelen] && fullmimetype[basemimetypelen] != ';' && !isspace(fullmimetype[basemimetypelen]))
      return 1;
  }
  return result;
}

static const char* allowed_mime_types[] = {
  "text/html",
  "application/xhtml+xml",
  NULL
};

int mimetype_is_html (const char* fullmimetype)
{
  int i;
  for (i = 0; allowed_mime_types[i]; i++) {
    if (mimetype_cmp(fullmimetype, allowed_mime_types[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

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
  struct package_info_struct* pkginfo;
  struct memory_buffer* outputbuffer;
};

int new_version_callback (const char* version, struct new_version_callback_struct* callbackdata)
{
  //display package information before the first entry
  if (callbackdata->counter++ == 0) {
    char buf[20] = "(missing timestamp)";
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&callbackdata->starttime));
    memory_buffer_append_printf(callbackdata->outputbuffer, "%s - %s - line %lu - lastest version: %s%s\n", buf, callbackdata->pkginfo->basename, (unsigned long)callbackdata->pkginfo->version_linenumber, callbackdata->pkginfo->version, (callbackdata->pkginfo->buildok ? "" : " (not building)"));
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
  struct package_info_struct* pkginfo;
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
    versioncheckdb_update_package(threadinfo->versiondb, packagename, -1, "No URL", pkginfo->version, pkginfo->downloadurldata);
  } else {
    unsigned int i;
    unsigned int n = 0;
    responsecode = 0;
    status = NULL;
    if ((versions = get_package_versions_from_url(info, threadinfo, url, pkginfo->basename, prefix, suffix, &responsecode, &status)) != NULL && (n = sorted_unique_list_size(versions)) > 0) {
      versioncheckdb_group_start(threadinfo->versiondb);
      for (i = 0; i < n; i++) {
        versioncheckdb_update_package_version(threadinfo->versiondb, packagename, sorted_unique_list_get(versions, i));
      }
      versioncheckdb_update_package(threadinfo->versiondb, packagename, responsecode, status, pkginfo->version, pkginfo->downloadurldata);
      versioncheckdb_group_end(threadinfo->versiondb);
    } else {
      versioncheckdb_update_package(threadinfo->versiondb, packagename, responsecode, status, pkginfo->version, pkginfo->downloadurldata);
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
  free_packageinfo(pkginfo);
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

#endif // 0000000000000000000000000000000000000

////////////////////////////////////////////////////////////////////////

struct itemlist_struct {
  char* key;
  char* value;
  struct itemlist_struct* next;
};

void itemlist_free (struct itemlist_struct** itemlist)
{
  struct itemlist_struct* p;
  struct itemlist_struct* q;
  if (!itemlist)
    return;
  p = *itemlist;
  while (p) {
    q = p->next;
    free(p->key);
    free(p->value);
    free(p);
    p = q;
  }
  *itemlist = NULL;
}

struct itemlist_struct** itemlist_append (struct itemlist_struct** itemlist, const char* key, const char* value)
{
  struct itemlist_struct** p;
  if (!itemlist)
    return NULL;
  p = itemlist;
  while (*p)
    p = &((*p)->next);
  if ((*p = (struct itemlist_struct*)malloc(sizeof(struct itemlist_struct))) != NULL) {
    (*p)->key = strdup(key);
    (*p)->value = strdup(value);
    (*p)->next = NULL;
  }
  return p;
};

struct itemlist_struct* itemlist_choice (struct itemlist_struct* itemlist, int defaultchoice, const char* prompt)
{
  char buf[32];
  struct itemlist_struct* p;
  int i = 0;
  int maxchoice = 0;
  //list options and determine maximum index
  p = itemlist;
  while (p) {
    printf("[%i] %s\n", ++maxchoice, p->value);
    p = p->next;
  }
  if (defaultchoice > maxchoice)
    defaultchoice = 0;
  if (defaultchoice == -1)
    defaultchoice = maxchoice;
  //prompt for choice
  printf("%s: [", (prompt ? prompt : "Please make your choice"));
  if (defaultchoice)
    printf("%i", defaultchoice);
  printf("] ");
  do {
    while (!fgets(buf, sizeof(buf), stdin))
      ;
  } while (((i = atoi(buf)) == 0 && defaultchoice == 0) || i > maxchoice);
  if (i == 0)
    i = defaultchoice;
  //go to selected choice and return it
  p = itemlist;
  while (p && --i > 0)
    p = p->next;
  return p;
}

char* prompt_value (const char* defaultchoice, const char* prompt)
{
  char buf[256];
  size_t len;
  printf("%s: [", (prompt ? prompt : "Please make your choice"));
  if (defaultchoice)
    printf("%s", defaultchoice);
  printf("] ");
  if (!fgets(buf, sizeof(buf), stdin))
    return NULL;
  len = strlen(buf);
  if (len > 0 && buf[len - 1] == '\n')
    buf[len - 1] = 0;
  if (!*buf)
    return NULL;
  return strdup(buf);
}

////////////////////////////////////////////////////////////////////////

struct search_html_for_versions_struct {
  sorted_unique_list* versions;
};

void process_html_tag_a (const char* href, const char* rel, struct search_html_for_versions_struct* data)
{
  const char* filepart;
  size_t filepartlen;
  if (href && (!rel || strcasecmp(rel, "nofollow") != 0) && (filepart = get_url_download_file_part(href, &filepartlen)) != NULL) {
    //URL decode
    if (strncmp(filepart, "msys2-", 6) == 0 && filepartlen > 7 && strncmp(filepart + filepartlen - 7, ".tar.xz", 7) == 0) {
      sorted_unique_list_add_buf(data->versions, filepart, filepartlen);
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

/*
typedef int (*extract_archive_callback_fn)(const char* data, void* callbackdata);

struct extract_archive_callback_struct {
  clock_t lasttime;
};

static size_t extract_archive_callback (void* data, size_t size, size_t nmemb, void* userp, extract_archive_callback_fn callback, void* callbackdata)
{
  return 0;
}
*/

int extract_archive (const char* archivefile, const char* basepath)
{
  int abort = 0;
  struct archive* pkg = archive_read_new();
  archive_read_support_filter_all(pkg);
  archive_read_support_format_all(pkg);
  if (archive_read_open_filename(pkg, archivefile, ARCHIVE_READ_BLOCK_SIZE) != ARCHIVE_OK) {
    fprintf(stderr, "Error opening archive file: %s\n", archivefile);
    abort = -1;
  } else {
    struct archive_entry* pkgentry;
    char* pathname;
    char* fullpath;
    FILE* dst;
    size_t basepathlen = strlen(basepath);
    uint64_t currentsize = 0;
    while (!abort && archive_read_next_header(pkg, &pkgentry) == ARCHIVE_OK) {
      if ((pathname = strdup_slashed(archive_entry_pathname(pkgentry))) != NULL) {
        if ((fullpath = (char*)malloc(basepathlen + strlen(pathname) + 2)) == NULL) {
          fprintf(stderr, "Memory allocation error\n");
        } else {
          //determine destination name
          memcpy(fullpath, basepath, basepathlen);
          fullpath[basepathlen] = PATH_SEPARATOR;
          strcpy(fullpath + basepathlen + 1, pathname);
          if (archive_entry_filetype(pkgentry) & AE_IFDIR) {
            //create folder
            //printf("Creating folder: %s\n", pathname);
            recursive_mkdir(fullpath);
          } else if (archive_entry_filetype(pkgentry) & AE_IFREG) {
            //show progress
            //printf("Extracting: %s\n", pathname);
            //printf("\r%3i%%", (int)(currentsize * 100 / metadata->totalsize));
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
                  abort = 2;
                  break;
                }
                currentsize += len;
              }
              fclose(dst);
            }
          }
          free(fullpath);
        }
        free(pathname);
      }
    }
#if ARCHIVE_VERSION_NUMBER < 3000000
    archive_write_finish(pkg);
#else
    archive_write_free(pkg);
#endif
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////

int main (int argc, char** argv, char *envp[])
{
  struct downloader* dl;
  int showhelp = 0;
  int verbose = 0;
  const char* packageinfopath = NULL;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",            NULL,      miniargv_cb_increment_int, &showhelp,        "show command line help"},
    {'s', "source-path",     "PATH",    miniargv_cb_set_const_str, &packageinfopath, "build recipe path\noverrides environment variable BUILDSCRIPTS"},
    {'v', "verbose",         NULL,      miniargv_cb_increment_int, &verbose,         "verbose mode, can be specified multiple times to increase verbosity"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "BUILDSCRIPTS",    NULL,      miniargv_cb_set_const_str, &packageinfopath, "build recipe path"},
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

  //create downloader
  if ((dl = downloader_create(PROGRAM_USER_AGENT, NULL, NULL)) == NULL) {
    fprintf(stderr, "Error in downloader_create()\b");
    return 10;
  }

  //collect data
  struct itemlist_struct* itemlist;
  struct itemlist_struct** itemlistend;
  struct itemlist_struct* item;
  char* msyspath = NULL;
  while (!msyspath) {
    if (msyspath)
      free(msyspath);
    printf("Enter MSYS2 path or leave empty to download and install MSYS2.\n");
    msyspath = prompt_value("", "MSYS2 path");
    if (msyspath) {
      if (!folder_exists(msyspath)) {
        fprintf(stderr, "Path not found: %s\n", msyspath);
        free(msyspath);
        msyspath = NULL;
      } else {
        struct memory_buffer* path;
        path = memory_buffer_create_from_string(msyspath);
        memory_buffer_append(path, MSYS2_CHECK_FILE);
        if (!file_exists(memory_buffer_get(path))) {
          fprintf(stderr, "File does not exist: %s\n", memory_buffer_get(path));
          free(msyspath);
          msyspath = NULL;
        }
        memory_buffer_free(path);
      }
    } else {
      struct memory_buffer* url;
      {
        itemlist = NULL;
        itemlistend = &itemlist;
#ifdef __MINGW64__
        itemlistend = itemlist_append(itemlistend, "x86_64", "Windows 64-bit (x86_64)");
#endif
        itemlistend = itemlist_append(itemlistend, "i686", "Windows 32-bit (i686)");
        item = itemlist_choice(itemlist, 1, "MSYS2 platform to download and install");
        url = memory_buffer_create_from_string(MSYS2_REPO_URL);
        memory_buffer_append(url, item->key);
        memory_buffer_append(url, "/");
        itemlist_free(&itemlist);
      }
      printf("Checking available downloads on %s\n", memory_buffer_get(url));
      {
        unsigned int i;
        const char* s;
        struct download_info_struct urlinfo;
        struct search_html_for_versions_struct searchhtmldata;
        char* mimetype;
        char* htmldata;
        //determine download
        mimetype = NULL;
        urlinfo.status = NULL;
        urlinfo.actualurl = NULL;
        urlinfo.mimetype = &mimetype;
        htmldata = downloader_get_file(dl, memory_buffer_get(url), &urlinfo);
        if (!htmldata) {
          fprintf(stderr, "Error loading URL: %s\n", memory_buffer_get(url));
          return 12;
        }
        searchhtmldata.versions = sorted_unique_list_create(versioncmp, free);
        //if (mimetype_is_html(mimetype)) {
          search_html_for_versions(htmldata, &searchhtmldata);
        //}
        free(htmldata);
        i = 0;
        itemlist = NULL;
        itemlistend = &itemlist;
        while ((s = sorted_unique_list_get(searchhtmldata.versions, i)) != NULL) {
          itemlistend = itemlist_append(itemlistend, s, s);
          i++;
        }
        sorted_unique_list_free(searchhtmldata.versions);
        item = itemlist_choice(itemlist, -1, "MSYS2 version to install");
        printf("%s\n", item->key);
        //get install location
        while (!msyspath) {
          printf("Path where to download and extract MSYS2\n");
          msyspath = prompt_value("", "MSYS2 path");
          if (!msyspath || !*msyspath) {
            fprintf(stderr, "No path specified\n");
          } else if (!folder_exists(msyspath)) {
            fprintf(stderr, "Path not found: %s\n", msyspath);
            free(msyspath);
            msyspath = NULL;
          }
        }
        //download archive
        struct memory_buffer* archivepath;
        archivepath = memory_buffer_create_from_string(msyspath);
        memory_buffer_append(archivepath, "\\");
        memory_buffer_append(archivepath, item->key);
        memory_buffer_append(url, "/");
        memory_buffer_append(url, item->key);
        itemlist_free(&itemlist);
        printf("Downloading...\n");
        downloader_save_file(dl, memory_buffer_get(url), memory_buffer_get(archivepath));
        if (!file_exists(memory_buffer_get(archivepath))) {
          fprintf(stderr, "Unable to download %s to file %s\n", memory_buffer_get(url), memory_buffer_get(archivepath));
        } else {
          printf("Download complete: %s\nExtracting...\n", memory_buffer_get(archivepath));
          if (extract_archive(memory_buffer_get(archivepath), msyspath) != 0) {
            fprintf(stderr, "Unable to extract %s to file %s\n", memory_buffer_get(archivepath), msyspath);
          } else {
            unlink(memory_buffer_get(archivepath));
          }
        }
        memory_buffer_free(archivepath);
      }
      memory_buffer_free(url);
    }
  }

  //clean up
  downloader_free(dl);

#if 0
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
#endif // 0000000000000000000000000000000000000
  return 0;
}
