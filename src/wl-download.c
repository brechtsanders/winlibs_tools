#include "winlibs_common.h"
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/types.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif
#if defined(STATIC) && !defined(CURL_STATICLIB)
#define CURL_STATICLIB
#endif
#include <curl/curl.h>
#include <curl/easy.h>
#include <miniargv.h>
#include "exclusive_lock_file.h"
#include "handle_interrupts.h"

#define PROGRAM_NAME            "wl-download"
#define PROGRAM_DESC            "Command line utility to download files"
#define PROGRAM_USER_AGENT      PROGRAM_NAME "/" WINLIBS_VERSION_STRING

#define DEFAULT_CONNECT_TIMEOUT 240             //abort connection if not connected in 4 minutes
#define DEFAULT_DOWNLOAD_TIMEOUT 10800          //abort download if total download time exceeds 3 hours
#define PROGRESS_UPDATE_INTERVAL 1 / 3          //how many seconds to wait in between download status updates (undefined for no limit)
//#define NO_SCALEDNUM                            //don't use scalednum library to show download size in human readable format

#ifndef NO_SCALEDNUM
#include <scalednum.h>
#endif

#define STRINGIZE_(value) #value
#define STRINGIZE(value) STRINGIZE_(value)

static int is_directory (const char* path)
{
  struct stat statbuf;
  if (stat(path, &statbuf) < 0)
    return 0;
  return S_ISDIR(statbuf.st_mode);
}

exclusive_lock_file lock = NULL;
char* dstpath = NULL;
int dsthandle = -1;

typedef int (*download_progress_callback_fn)(long long pos, long long len, void* callbackdata);

struct process_download_data_struct {
  void* showprogress_callbackdata;
  CURL* curl_handle;
  curl_off_t len;
  curl_off_t pos;
  curl_off_t lastpos;
  int dst;
  download_progress_callback_fn showprogress;
#ifndef NO_SCALEDNUM
  scalednum scale;
#endif
#ifdef PROGRESS_UPDATE_INTERVAL
  clock_t nextstatusupdate;
#endif
};

int show_progress (long long pos, long long len, void* callbackdata)
{
#ifndef NO_SCALEDNUM
  char scalednumbuf[16];
#endif
  if (len > 0) {
#ifndef NO_SCALEDNUM
    scalednum_to_buffer(((struct process_download_data_struct*)callbackdata)->scale, (double)pos, scalednumbuf, sizeof(scalednumbuf));
    printf("\r%s (%u%%) ", scalednumbuf, (unsigned)(100L * pos / len));
#else
    printf("\r%lu bytes (%u%%)", (unsigned long)pos, (unsigned)(100L * pos / len));
    //printf("\r%lu/%lu bytes (%u%%)", (unsigned long)pos, (unsigned long)len, (unsigned)(100L * pos / len));
#endif
  } else {
#ifndef NO_SCALEDNUM
    scalednum_to_buffer(((struct process_download_data_struct*)callbackdata)->scale, (double)pos, scalednumbuf, sizeof(scalednumbuf));
    printf("\r%s ", scalednumbuf);
#else
    printf("\r%lu bytes", (unsigned long)pos);
#endif
  }
  if (pos == len)
    printf("\n");
  fflush(stdout);
  return 0;
}

static size_t process_download_data (void* data, size_t size, size_t nitems, void* userdata)
{
  ssize_t len;
#ifdef PROGRESS_UPDATE_INTERVAL
  clock_t currentclock;
#endif
  struct process_download_data_struct* dldata = (struct process_download_data_struct*)userdata;
  //in the beginning get download size
  if (dldata->pos == 0 && dldata->len == -2) {
    curl_easy_getinfo(dldata->curl_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &dldata->len);
  }
  //update download size
  dldata->pos += size * nitems;
  //update status if needed
#ifdef PROGRESS_UPDATE_INTERVAL
  if ((currentclock = clock()) > dldata->nextstatusupdate) {
    dldata->nextstatusupdate = currentclock + CLOCKS_PER_SEC * PROGRESS_UPDATE_INTERVAL;
#else
  {
#endif
    if ((*dldata->showprogress)(dldata->pos, dldata->len, dldata) != 0)
      return 0;
    dldata->lastpos = dldata->pos;
  }
  //write data
  if (!size || !nitems)
    len = 0;
  else
    len = write(dldata->dst, data, size * nitems);
  return (len < 0 ? 0 : len);
}

static size_t process_download_data_silent (void* data, size_t size, size_t nitems, void* userdata)
{
  ssize_t len;
  struct process_download_data_struct* dldata = (struct process_download_data_struct*)userdata;
  //write data
  if (!size || !nitems)
    len = 0;
  else
    len = write(dldata->dst, data, size * nitems);
  return (len < 0 ? 0 : len);
}

long download_to_file (CURL* curl_handle, const char* url, const char* dstpath, int force, download_progress_callback_fn progresscallback, void* callbackdata)
{
  CURLcode curlstatus;
  long responsecode;
  struct process_download_data_struct dldata;
  dldata.showprogress_callbackdata = callbackdata;
  dldata.curl_handle = curl_handle;
  dldata.len = -2;
  dldata.pos = 0;
  dldata.lastpos = -1;
  dldata.showprogress = progresscallback;
#ifndef NO_SCALEDNUM
  dldata.scale = scalednum_create(3, SCALEDNUM_KILO1024 | SCALEDNUM_SHORTPREFIX, "B", NULL);
#endif
#ifdef PROGRESS_UPDATE_INTERVAL
  dldata.nextstatusupdate = 0;
#endif
  //open destination file
  if ((dsthandle = open(dstpath, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY | (force ? 0 : O_EXCL), S_IWUSR | S_IWGRP | S_IWOTH)) < 0) {
    if (errno == EEXIST)
      fprintf(stderr, "File already exists: %s\n", dstpath);
    else
      fprintf(stderr, "Error creating file: %s\n", dstpath);
    return -2;
  }
  //download contents
  dldata.dst = dsthandle;
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, (progresscallback ? process_download_data : process_download_data_silent));
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &dldata);
  curlstatus = curl_easy_perform(curl_handle);
  if (progresscallback && dldata.lastpos < dldata.len)
    (*progresscallback)(dldata.pos, (dldata.len >= 0 ? dldata.len : dldata.pos), &dldata);
#ifndef NO_SCALEDNUM
  scalednum_free(dldata.scale);
#endif
  //close destination file
  close(dsthandle);
  dsthandle = -1;
  //check download status
  if (curlstatus != CURLE_OK) {
    fprintf(stderr, "libcurl error %i: %s\n", (int)curlstatus, curl_easy_strerror(curlstatus));
    return curlstatus;
  }
  responsecode = -1;
  curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &responsecode);
  //delete downloaded file in case of error
  if (responsecode == -1 || !(responsecode >= 200 && responsecode < 300)) {
    if (responsecode != -1) {
      printf("HTTP response code: %li\n", responsecode);
    }
    unlink(dstpath);
    return responsecode;
  }
  /////TO DO: handle other response codes (retry?)
  return 0;
}

DEFINE_INTERRUPT_HANDLER_BEGIN(handle_break_signal)
{
  //close interrupted download file
  if (dsthandle >= 0) {
    close(dsthandle);
    dsthandle = -1;
  }
  //clean up interrupted download file
  if (dstpath) {
    if (unlink(dstpath) == 0) {
      free(dstpath);
      dstpath = NULL;
    } else {
      fprintf(stderr, "Error deleting file: %s\n", dstpath);
    }
  }
  //clean up lock file
  exclusive_lock_file_destroy(lock);
  lock = NULL;
  //exit program
  exit(0);
}
DEFINE_INTERRUPT_HANDLER_END(handle_break_signal)

int get_lock_progress (exclusive_lock_file handle, void* callbackdata)
{
  if ((*(int*)callbackdata)++ == 0) {
    printf("Waiting for other process to finish download...\n");
  }
  return 0;
}

struct url_list {
  const char* url;
  struct url_list* next;
};

int main (int argc, char *argv[], char *envp[])
{
  int i;
  int n;
  const char* errmsg;
  size_t dstdirlen;
  CURL* curl_handle;
  int showversion = 0;
  int showhelp = 0;
  const char* dstdir = NULL;
  int force = 0;
  long connect_timeout = DEFAULT_CONNECT_TIMEOUT;
  long download_timeout = DEFAULT_DOWNLOAD_TIMEOUT;
  int verbose = 0;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",             NULL,      miniargv_cb_increment_int, &showhelp,    "show command line help", NULL},
    {0,   "version",          NULL,      miniargv_cb_increment_int, &showversion, "show version information", NULL},
    {'d', "destination",      "path",    miniargv_cb_set_const_str, &dstdir,      "path where downloads are stored", NULL},
    {'f', "force",            NULL,      miniargv_cb_increment_int, &force,       "force download even if destination file already exists", NULL},
    {'t', "connect-timeout",  "SECONDS", miniargv_cb_increment_int, &verbose,     "connection timeout in seconds (default: " STRINGIZE(DEFAULT_CONNECT_TIMEOUT) "s)", NULL},
    {0,   "download-timeout", "SECONDS", miniargv_cb_increment_int, &verbose,     "total download timeout in seconds (default: " STRINGIZE(DEFAULT_DOWNLOAD_TIMEOUT) "s)", NULL},
    {'v', "verbose",          NULL,      miniargv_cb_increment_int, &verbose,     "verbose mode", NULL},
    {0,   NULL,               "URL",     miniargv_cb_error,         NULL,         "URL of file to download", NULL},
    MINIARGV_DEFINITION_END
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "TARBALLDIR",       NULL,      miniargv_cb_set_const_str, &dstdir,   "path where downloads are stored", NULL},
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
    miniargv_help(argdef, envdef, 20, 0);
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
  //check parameters
  if (!dstdir || !*dstdir) {
    fprintf(stderr, "Download directory not specified\n");
    return 2;
  }
  if (!is_directory(dstdir)) {
    if (verbose)
      printf("Creating missing destination directory: %s\n", dstdir);
#ifdef _WIN32
    if (mkdir(dstdir) != 0) {
#else
    if (mkdir(dstdir, S_IRWXU | S_IRWXG | S_IRWXO) != 0) {
#endif
      fprintf(stderr, "Error creating directory: %s\n%s\n", dstdir, strerror(errno));
      return 3;
    }
  }
  dstdirlen = strlen(dstdir);
  if (miniargv_get_next_arg_param(0, argv, argdef, NULL) <= 0) {
    fprintf(stderr, "No URLs specified\n");
    return 4;
  }

  //global initialization for cURL
  curl_global_init(CURL_GLOBAL_ALL);

  //install signal handler
  lock = NULL;
  INSTALL_INTERRUPT_HANDLER(handle_break_signal)

  //try to create exclusive lock file
  n = 0;
  if ((lock = exclusive_lock_file_create(dstdir, SOURCE_DOWNLOAD_LOCK_FILE_BASE, &errmsg, get_lock_progress, &n)) == NULL) {
    fprintf(stderr, "%s\n", errmsg);
    return 2;
  }

  //inialize cURL handle
  curl_handle = curl_easy_init();
  curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 0L);                               //hide output
  curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 1L);                            //hide progress indicator
  curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);                        //follow redirections
  curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 20L);                            //maximum number of redirections to follow
  curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);                        //don't verify the SSL peer
  curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);                        //don't verify the SSL host
  //curl_easy_setopt(curl_handle, CURLOPT_SSLVERSION, CURL_SSLVERSION_SSLv3);         //avoid GnuTLS errors
  curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, connect_timeout);           //connection timeout in seconds
  curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, download_timeout);                 //download timeout in seconds
  curl_easy_setopt(curl_handle, CURLOPT_TCP_KEEPALIVE, 1L);                         //enable keepalive probes
  curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_LIMIT, 1L);                       //abort if download speed is slower than 1 byte/sec ...
  curl_easy_setopt(curl_handle, CURLOPT_LOW_SPEED_TIME, connect_timeout);           //... during the period specified as connection timeout
#ifdef PROGRAM_USER_AGENT
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, PROGRAM_USER_AGENT);             //set the user agent
#endif

  //process URLs
  i = 0;
  while ((i = miniargv_get_next_arg_param(i, argv, argdef, NULL)) > 0) {
    const char* filename;

    printf("%s\n", argv[i]);

    //get filename
    if ((filename = strrchr(argv[i], '/')) == NULL || *++filename == 0) {
      fprintf(stderr, "Unable to determine filename for URL: %s\n", argv[i]);
    } else {
      //determine download file path
      if ((dstpath = (char*)malloc(dstdirlen + strlen(filename) + 2)) == NULL) {
        fprintf(stderr, "Memory allocation error\n");
        return 3;
      }
      memcpy(dstpath, dstdir, dstdirlen);
      dstpath[dstdirlen] = PATH_SEPARATOR;
      strcpy(dstpath + dstdirlen + 1, filename);
      //check filename for unsupported characters in filename
      {
        char* p = dstpath + dstdirlen + 1;
        while (*p) {
          switch (*p) {
            case ':':
            case '?':
            case '*':
            case '<':
            case '>':
              *p = '_';
              break;
            case '|':
              *p = '-';
              break;
          }
          p++;
        }
      }
      //download file
      download_to_file(curl_handle, argv[i], dstpath, force, (verbose > 0 ? show_progress : NULL), &verbose);
      free(dstpath);
      dstpath = NULL;
    }
  }
  //clean up
  curl_easy_cleanup(curl_handle);
  curl_global_cleanup();
  exclusive_lock_file_destroy(lock);
  return 0;
}

/////TO DO: resume failed download, see: https://curl.se/libcurl/c/CURLOPT_RESUME_FROM_LARGE.html
/////TO DO: add color
