#include "downloader.h"
#include "memory_buffer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef STATIC
#define CURL_STATICLIB 1
#endif
#include <curl/curl.h>

#define HEADER_CONTENTTYPE      "Content-Type:"
#define HEADER_CONTENTTYPE_LEN  13

////////////////////////////////////////////////////////////////////////

struct process_download_header_struct {
  size_t linenumber;
  char* firstline;
  char* mimetype;
};

static size_t process_download_header (void* data, size_t size, size_t nitems, void* userdata)
{
  struct process_download_header_struct* info = (struct process_download_header_struct*)userdata;
  if (info->linenumber++ == 0) {
    size_t endpos = nitems;
    while (endpos > 0 && (((char*)data)[endpos - 1] == '\r' || ((char*)data)[endpos - 1] == '\n'))
      endpos--;
    if ((info->firstline = (char*)malloc(endpos + 1)) != NULL) {
      memcpy(info->firstline, data, endpos);
      info->firstline[endpos] = 0;
    }
  } else if (!info->mimetype && strncasecmp(data, HEADER_CONTENTTYPE, HEADER_CONTENTTYPE_LEN) == 0) {
    //keep MIME type
    size_t beginpos;
    size_t endpos;
    //determine MIME type
    beginpos = HEADER_CONTENTTYPE_LEN;
    while (((char*)data)[beginpos] && isspace(((char*)data)[beginpos]) && beginpos < nitems)
      beginpos++;
    endpos = beginpos;
    size_t fullmimetypelen;
    endpos = endpos;
    while (((char*)data)[endpos] && ((char*)data)[endpos] != '\r' && ((char*)data)[endpos] != '\n')
      endpos++;
    fullmimetypelen = endpos - beginpos;
    if (info->mimetype)
      free (info->mimetype);
    if ((info->mimetype = (char*)malloc(fullmimetypelen + 1)) != NULL) {
      memcpy(info->mimetype, (char*)data + beginpos, fullmimetypelen);
      info->mimetype[fullmimetypelen] = 0;
    }
  }
  return nitems;
}

static size_t process_download_data (void* data, size_t size, size_t nitems, void* userdata)
{
  return memory_buffer_append_buf((struct memory_buffer*)userdata, (char*)data, size * nitems);
}

////////////////////////////////////////////////////////////////////////

struct downloader {
  struct downloadcache* cache;
  struct commonoutput_stuct* output;
  CURL* curl_handle;
};

void downloader_global_init ()
{
  curl_global_init(CURL_GLOBAL_ALL);
}

void downloader_global_cleanup ()
{
  curl_global_cleanup();
}

struct downloader* downloader_create (const char* useragent, struct downloadcachedb* cachedb, struct commonoutput_stuct* output)
{
  struct downloader* handle;
  if ((handle = (struct downloader*)malloc(sizeof(struct downloader))) == NULL)
    return NULL;
  handle->cache = downloadcache_create(cachedb);
  handle->output = output;
  //inialize cURL handle
  handle->curl_handle = curl_easy_init();
  curl_easy_setopt(handle->curl_handle, CURLOPT_VERBOSE, 0L);                               //hide output
  curl_easy_setopt(handle->curl_handle, CURLOPT_NOPROGRESS, 1L);                            //hide progress indicator
  curl_easy_setopt(handle->curl_handle, CURLOPT_FOLLOWLOCATION, 1L);                        //follow redirections
  curl_easy_setopt(handle->curl_handle, CURLOPT_MAXREDIRS, 20L);                            //maximum number of redirections to follow
  curl_easy_setopt(handle->curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);                        //don't verify the SSL peer
  curl_easy_setopt(handle->curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);                        //don't verify the SSL host
  //curl_easy_setopt(handle->curl_handle, CURLOPT_SSLVERSION, CURL_SSLVERSION_SSLv3);         //avoid GnuTLS errors
  curl_easy_setopt(handle->curl_handle, CURLOPT_CONNECTTIMEOUT, 240L);                      //connection timeout in seconds
  curl_easy_setopt(handle->curl_handle, CURLOPT_TIMEOUT, 120L);                             //download timeout in seconds
  curl_easy_setopt(handle->curl_handle, CURLOPT_TCP_KEEPALIVE, 1L);                         //enable keepalive probes
  curl_easy_setopt(handle->curl_handle, CURLOPT_HEADERFUNCTION, process_download_header);   //set header processing callback function
  curl_easy_setopt(handle->curl_handle, CURLOPT_WRITEFUNCTION, process_download_data);      //set incoming data callback function
  curl_easy_setopt(handle->curl_handle, CURLOPT_DIRLISTONLY, 1L);                           //only list filenames in FTP directory listings
  if (useragent)
    curl_easy_setopt(handle->curl_handle, CURLOPT_USERAGENT, useragent);                    //set the user agent
  return handle;
};

void downloader_free (struct downloader* handle)
{
  if (!handle)
    return;
  downloadcache_free(handle->cache);
  //clean up cURL handle
  curl_easy_cleanup(handle->curl_handle);
  free(handle);
};

const char* custom_mimetype_ftp_listing = "FTP simple list";

char* downloader_get_file (struct downloader* handle, const char* url, struct download_info_struct* info)
{
  char* data;
  struct process_download_header_struct headerinfo = {0, NULL, NULL};
  //initialize/clean up data
  if (info->status) {
    free(*info->status);
    *info->status = NULL;
  }
  if (info->actualurl) {
    free(*info->actualurl);
    *info->actualurl = NULL;
  }
  if (info->mimetype) {
    free(*info->mimetype);
    *info->mimetype = NULL;
  }
  info->cached = 1;
  info->responsecode = -1;
  //try to get from cache
  if ((data = downloadcache_get(handle->cache, url, info)) == NULL && info->responsecode == -1) {
    //not found in cache, download
    CURLcode curlstatus;
    struct memory_buffer* buf;
    info->cached = 0;
    headerinfo.linenumber = 0;
    if ((buf = memory_buffer_create()) == NULL)
      return NULL;
    //protocol specific settings
    if (info->mimetype) {
      struct Curl_URL* urlhandle;
      char* urlscheme = NULL;
      urlhandle = curl_url();
      curl_url_set(urlhandle, CURLUPART_URL, url, 0);
      curl_url_get(urlhandle, CURLUPART_SCHEME, &urlscheme, 0);
      if (urlscheme) {
        if (strcmp(urlscheme, "ftp") == 0 || strcmp(urlscheme, "ftps") == 0 || strcmp(urlscheme, "sftp") == 0)
          headerinfo.mimetype = strdup(custom_mimetype_ftp_listing);
      }
      curl_free(urlscheme);
      curl_url_cleanup(urlhandle);
    }
    //download
    curl_easy_setopt(handle->curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(handle->curl_handle, CURLOPT_HEADERDATA, &headerinfo);
    curl_easy_setopt(handle->curl_handle, CURLOPT_WRITEDATA, buf);
    if ((curlstatus = curl_easy_perform(handle->curl_handle)) != CURLE_OK) {
      //commonoutput_printf(handle->output, 0, "Error: %s - URL: %s\n", curl_easy_strerror(curlstatus), url);
      //memory_buffer_free(buf);
      memory_buffer_append_printf(buf, "libcurl error %i: %s", (int)curlstatus, curl_easy_strerror(curlstatus));
      free(headerinfo.firstline);
      headerinfo.firstline = memory_buffer_free_to_allocated_string(buf);
      data = NULL;
    } else {
      if (info->actualurl) {
        data = NULL;
        if (curl_easy_getinfo(handle->curl_handle, CURLINFO_EFFECTIVE_URL, &data) == CURLE_OK)
          *info->actualurl = (data ? strdup(data) : NULL);
      }
      curl_easy_getinfo(handle->curl_handle, CURLINFO_RESPONSE_CODE, &info->responsecode);
      data = memory_buffer_free_to_allocated_string(buf);
    }
    //store results
    if (info->status)
      *info->status = headerinfo.firstline;
    else
      free(headerinfo.firstline);
    if (info->mimetype)
      *info->mimetype = headerinfo.mimetype;
    else
      free(headerinfo.mimetype);
    //add to cache
    downloadcache_add(handle->cache, url, data, info);
  }
  //return requested information
  if (info->responsecode >= 400) {
    //don't return result in case of HTTP error
    free(data);
    data = NULL;
  } else if (info->responsecode == 200) {
    //set status field to NULL in case of HTTP success code 200
    if (info->status) {
      free(*info->status);
      *info->status = NULL;
    }
  }
  return data;
}

