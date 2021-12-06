#include "download_cache.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

#define DB_SQL_CACHE_CREATE \
  "CREATE TABLE cache (" \
  " url TEXT NOT NULL," \
  " responsecode INTEGER," \
  " status TEXT," \
  " data TEXT," \
  " actualurl TEXT," \
  " mimetype TEXT," \
  " created INTEGER," \
  " PRIMARY KEY(url)" \
  ");"
#define DB_SQL_CACHE_ADD "INSERT INTO cache (url, actualurl, data, mimetype, responsecode, status, created) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)"
#define DB_SQL_CACHE_GET "SELECT responsecode, status, data, actualurl, mimetype, created FROM cache WHERE url = ?"
#define DB_SQL_CACHE_DEL "DELETE FROM cache WHERE url = ?"
#define DB_SQL_CACHE_PURGE "DELETE FROM cache WHERE created <= ?"

struct downloadcachedb {
  sqlite3* db;
  int isnew;
  unsigned long expiration;
};

struct downloadcachedb* downloadcachedb_create (const char* filename, unsigned long expiration)
{
  struct downloadcachedb* handle;
  if (!sqlite3_threadsafe())
    return NULL;
  if ((handle = (struct downloadcachedb*)malloc(sizeof(struct downloadcachedb))) == NULL)
    return NULL;
  //create temporary database
  if (sqlite3_open_v2((!filename ? "" : filename), &handle->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX /*| SQLITE_OPEN_SHAREDCACHE*/, NULL) != SQLITE_OK) {
    free(handle);
    return NULL;
  }
  //enable WAL mode (see also: https://www.sqlite.org/wal.html)
  sqlite3_exec(handle->db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
  //create tables
  handle->isnew = 0;
  if (sqlite3_exec(handle->db, DB_SQL_CACHE_CREATE, NULL, NULL, NULL) != SQLITE_OK) {
    //only consider errors if cache database is a temporary database and not a specified file
    if (!filename || !*filename) {
      free(handle);
      return NULL;
    }
  } else {
    handle->isnew = 1;
  }
  handle->expiration = expiration;
  return handle;
}

void downloadcachedb_free (struct downloadcachedb* handle)
{
  if (handle) {
    if (handle->db) {
      sqlite3_close(handle->db);
    }
    free(handle);
  }
}

int downloadcachedb_is_new (struct downloadcachedb* handle)
{
  if (!handle)
    return 0;
  return handle->isnew;
}

int downloadcachedb_purge (struct downloadcachedb* handle)
{
  sqlite3_stmt* qry;
  int result = 0;
  if (!handle)
    return -3;
  if (handle->expiration == 0)
    return 0;
  if (sqlite3_prepare_v3(handle->db, DB_SQL_CACHE_PURGE, -1, 0, &qry, NULL) != SQLITE_OK)
    return -2;
  sqlite3_bind_int64(qry, 1, time(NULL) - handle->expiration);
  if (sqlite3_step(qry) != SQLITE_DONE)
    result = -1;
  else
    result = sqlite3_changes(handle->db);
  //sqlite3_clear_bindings(qry);
  //sqlite3_reset(qry);
  sqlite3_finalize(qry);
  return result;
}

////////////////////////////////////////////////////////////////////////

struct downloadcache {
  struct downloadcachedb* masterdb;
  sqlite3* db;
  sqlite3_stmt* db_add;
  sqlite3_stmt* db_get;
  sqlite3_stmt* db_del;
};

#define PREP_SQL(var,sql) \
  if (sqlite3_prepare_v3(handle->db, sql, -1, SQLITE_PREPARE_PERSISTENT, &handle->var, NULL) != SQLITE_OK) { \
    free(handle); \
    return NULL; \
  }

struct downloadcache* downloadcache_create (struct downloadcachedb* masterdb)
{
  struct downloadcache* handle;
  if (!masterdb)
    return NULL;
  if ((handle = (struct downloadcache*)malloc(sizeof(struct downloadcache))) == NULL)
    return NULL;
  handle->masterdb = masterdb;
  handle->db = masterdb->db;
  //prepare SQL statements
  PREP_SQL(db_add, DB_SQL_CACHE_ADD)
  PREP_SQL(db_get, DB_SQL_CACHE_GET)
  PREP_SQL(db_del, DB_SQL_CACHE_DEL)
  return handle;
}

#undef PREP_SQL

void downloadcache_free (struct downloadcache* handle)
{
  if (handle) {
    if (handle->db) {
      sqlite3_finalize(handle->db_add);
      sqlite3_finalize(handle->db_get);
      sqlite3_finalize(handle->db_del);
    }
    free(handle);
  }
}

int downloadcache_add (struct downloadcache* handle, const char* url, const char* data, struct download_info_struct* info)
{
  int result = 0;
  if (!handle)
    return -1;
  sqlite3_bind_text(handle->db_add, 1, url, -1, SQLITE_STATIC);
  sqlite3_bind_text(handle->db_add, 2, (info && info->actualurl ? *info->actualurl : NULL), -1, SQLITE_STATIC);
  sqlite3_bind_text(handle->db_add, 3, data, -1, SQLITE_STATIC);
  sqlite3_bind_text(handle->db_add, 4, (info && info->mimetype ? *info->mimetype : NULL), -1, SQLITE_STATIC);
  sqlite3_bind_int64(handle->db_add, 5, (info ? info->responsecode : -1));
  sqlite3_bind_text(handle->db_add, 6, (info && info->status ? *info->status : NULL), -1, SQLITE_STATIC);
  sqlite3_bind_int64(handle->db_add, 7, time(NULL));
  sqlite3_step(handle->db_add);
  if (sqlite3_step(handle->db_add) != SQLITE_DONE) {
    //creation of database record failed, assuming the reason is another thread just created the entry
    result = 1;
  }
  sqlite3_clear_bindings(handle->db_add);
  sqlite3_reset(handle->db_add);
  return result;
}

char* downloadcache_get (struct downloadcache* handle, const char* url, struct download_info_struct* info)
{
  const char* dbdata;
  time_t cache_time = 0;
  int cache_expired = 0;
  char* result = NULL;
  if (!handle)
    return NULL;
  if (info) {
    info->cached = 0;
    info->responsecode = -1;
  }
  sqlite3_bind_text(handle->db_get, 1, url, -1, SQLITE_STATIC);
  if (sqlite3_step(handle->db_get) == SQLITE_ROW) {
    if (handle->masterdb->expiration && (cache_time = sqlite3_column_int64(handle->db_get, 5)) > 0 && cache_time > 0 && cache_time <= time(NULL) - handle->masterdb->expiration) {
      cache_expired = 1;
    } else if ((dbdata = (char*)sqlite3_column_text(handle->db_get, 2)) != NULL) {
      result = strdup(dbdata);
      if (info) {
        info->cached = 1;
        info->responsecode = sqlite3_column_int64(handle->db_get, 0);
        if (info->status) {
          if (*info->status)
            free(*info->status);
          dbdata = (char*)sqlite3_column_text(handle->db_get, 1);
          *info->status = (dbdata ? strdup(dbdata) : NULL);
        }
        if (info->actualurl) {
          if (*info->actualurl)
            free(*info->actualurl);
          dbdata = (char*)sqlite3_column_text(handle->db_get, 3);
          *info->actualurl = (dbdata ? strdup(dbdata) : NULL);
        }
        if (info->mimetype) {
          if (*info->mimetype)
            free(*info->mimetype);
          dbdata = (char*)sqlite3_column_text(handle->db_get, 4);
          *info->mimetype = (dbdata ? strdup(dbdata) : NULL);
        }
      }
    }
  }
  sqlite3_clear_bindings(handle->db_get);
  sqlite3_reset(handle->db_get);
  if (cache_expired) {
    sqlite3_bind_text(handle->db_del, 1, url, -1, SQLITE_STATIC);
    if (sqlite3_step(handle->db_del) != SQLITE_DONE) {
      //deletion of database record failed, assuming the reason is another thread just deleted the entry
    }
    sqlite3_clear_bindings(handle->db_del);
    sqlite3_reset(handle->db_del);
  }
  return result;
}
