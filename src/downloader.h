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

//!download file contents to memory
/*!
  \param  handle                downloader handle
  \param  url                   URL
  \param  info                  structure that will receive status information about the downloaded file
  \return contents of HTML page or NULL on error
*/
char* downloader_get_file (struct downloader* handle, const char* url, struct download_info_struct* info);

//!download file
/*!
  \param  handle                downloader handle
  \param  url                   URL
  \param  outputfile            file where to save the downloaded data
  \return HTTP status code
*/
long downloader_save_file (struct downloader* handle, const char* url, const char* outputfile);



//!decode URL encoded string
/*!
  \param  data                  data to URL decode
  \param  datalen               length data to URL decode
  \return newly allocated string with URL decoded data (caller must call free() on the result) or NULL on error
*/
char* url_decode (const char* data, size_t datalen);

//!return a pointer to the character right after "<scheme>://" or NULL if not found
/*!
  \param  url                   URL
  \return pointer after "<scheme>://" in the URL or NULL on error
*/
const char* url_skip_scheme (const char* url);

//!resolve URL (absolute URLs are returned without change, relative URLs will be resolved)
/*!
  \param  base_url              base URL
  \param  url                   URL to resolve
  \return newly allocated string with resolved URL (caller must call free() on the result) or NULL on error
*/
char* resolve_url (const char* base_url, const char* url);

//!returns filename part of a URL, or name of deepest folder if URL ends with a slash
/*!
  \param  url                   URL to get filename part from
  \param  filelen               pointer to variable that will receive length of filename (may be NULL)
  \return pointer to filename part of URL or NULL on error
*/
const char* get_url_file_part (const char* url, size_t* filelen);

//!returns download filename part of a URL
/*!
  \param  url                   URL to get download filename part from
  \param  filelen               pointer to variable that will receive length of download filename (may be NULL)
  \return pointer to download filename part of URL or NULL on error
*/
const char* get_url_download_file_part (const char* url, size_t* filelen);



//!compare full mime type string with base mime type
/*!
  \param  fullmimetype          full mime type to check
  \param  basemimetype          base mime type to compare with
  \return zero on match, otherwise non-zero
*/
int mimetype_cmp (const char* fullmimetype, const char* basemimetype);

//!check if mime type specifies HTML
/*!
  \param  fullmimetype          full mime type to check
  \return zero if HTML, otherwise non-zero
*/
int mimetype_is_html (const char* fullmimetype);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_DOWNLOADER_H
