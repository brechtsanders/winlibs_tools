/*
  header file for download functions
*/

#ifndef INCLUDED_DOWNLOADER_H
#define INCLUDED_DOWNLOADER_H

#include "download_cache.h"
#include "common_output.h"
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

//!data structure for downloader handle
struct downloader;

//!global initialization, call once in the application before using any other downloader functions
void downloader_global_init ();

//!global cleanup, call once in the application, don't use any other downloader functions after this
void downloader_global_cleanup ();

//!create downloader handle
/*!
  \param  useragent             user agent for web client (or NULL to set to default)
  \param  cachedb               cache database (or NULL to not use caching)
  \param  output                output to send status information to
  \return downloader handle
*/
struct downloader* downloader_create (const char* useragent, struct downloadcachedb* cachedb, struct commonoutput_stuct* output);

//!clean up downloader handle
/*!
  \param  handle                downloader handle
*/
void downloader_free (struct downloader* handle);

//!custom MIME type used for FTP listings
extern const char* custom_mimetype_ftp_listing;

//!get HTML page
/*!
  \param  handle                downloader handle
  \param  url                   URL
  \param  info                  structure that will receive status information about the downloaded file
  \param  responsecode          pointer that will receive HTTP response code (in case there was a HTTP error)
  \param  status                pointer that will receive status information or error message
  \param  actualurl             pointer that will receive actual URL reported by page (can be different, e.g. because of redirections)
  \return contents of HTML page or NULL on error
*/
char* downloader_get_file (struct downloader* handle, const char* url, struct download_info_struct* info);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_DOWNLOADER_H
