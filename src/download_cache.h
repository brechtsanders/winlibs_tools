/*
  header file for download caching functions
*/

#ifndef INCLUDED_DOWNLOAD_CACHE_H
#define INCLUDED_DOWNLOAD_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

//!data structure for receiving download information
struct download_info_struct {
  int cached;                   //!< cached (non-zero if content was cached, otherwise zero)
  long responsecode;            //!< HTTP response code
  char** status;                //!< pointer that will (if not NULL) receive status information or error message
  char** actualurl;             //!< pointer that will (if not NULL) receive the actual URL (can be different, e.g. because of redirections)
  char** mimetype;              //!< pointer that will (if not NULL) receive the MIME type
};



//!data structure for download cache database handle
struct downloadcachedb;

//!create download cache database handle
/*!
  \param  filename              cache database path or set to NULL for temporary database
  \param  expiration            number of seconds after which cache entries expire
  \return download cache database handle
*/
struct downloadcachedb* downloadcachedb_create (const char* filename, unsigned long expiration);

//!clean up download cache database handle
/*!
  \param  handle                download cache handle
*/
void downloadcachedb_free (struct downloadcachedb* handle);

//!check if cache database was newly created
/*!
  \param  handle                download cache handle
  \return non-zero if cache database was newly created, otherwise zero
*/
int downloadcachedb_is_new (struct downloadcachedb* handle);

//!purge cache (remove all expired entries)
/*!
  \param  handle                download cache handle
  \return number of entries deleted or negative on error
*/
int downloadcachedb_purge (struct downloadcachedb* handle);



//!data structure for download cache handle
struct downloadcache;

//!create download cache handle
/*!
  \param  filename              cache database path or set to NULL for temporary database
  \param  expiration            number of seconds after which cache entries expire
  \return download cache handle
*/
struct downloadcache* downloadcache_create (struct downloadcachedb* masterdb);

//!clean up download cache handle
/*!
  \param  handle                download cache handle
*/
void downloadcache_free (struct downloadcache* handle);

//!create cache entry
/*!
  \param  handle                download cache handle
  \param  url                   URL used to fetch the data
  \param  data                  web page data
  \param  info                  structure with status information about the downloaded file
  \param  responsecode          HTTP responce code
  \param  status                status information or error message
  \param  actualurl             actual URL reported by page (can be different, e.g. because of redirections)
  \param  mimetype              MIME type
  \return zero on success or non-zero on error
*/
int downloadcache_add (struct downloadcache* handle, const char* url, const char* data, struct download_info_struct* info);

//!get cache entry
/*!
  \param  handle                download cache handle
  \param  url                   URL
  \param  info                  structure that will receive status information about the downloaded file
  \return web page data or NULL if unable to get page
*/
char* downloadcache_get (struct downloadcache* handle, const char* url, struct download_info_struct* info);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_DOWNLOAD_CACHE_H
