#include "version_check_db.h"
#include <time.h>
#include <sqlite3.h>

#define DB_SQL_CREATE_TABLES \
  "CREATE TABLE package (" \
  " id INTEGER PRIMARY KEY AUTOINCREMENT," \
  " basename TEXT NOT NULL," \
  " responsecode INTEGER," \
  " status TEXT," \
  " currentversion TEXT," \
  " downloadurl TEXT," \
  " updated INTEGER," \
  " created INTEGER" \
  ");" \
  "CREATE UNIQUE INDEX idx_package_basename ON package(basename);" \
  "CREATE TABLE package_version (" \
  " id INTEGER PRIMARY KEY AUTOINCREMENT," \
  " basename TEXT NOT NULL," \
  " version TEXT NOT NULL," \
  " updated INTEGER," \
  " created INTEGER" \
  ");" \
  "CREATE UNIQUE INDEX idx_package_version ON package_version(basename, version);"

#define DB_SQL_PACKAGE_UPD "INSERT INTO package (basename, responsecode, status, currentversion, downloadurl, updated, created) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?6) ON CONFLICT(basename) DO UPDATE SET responsecode = ?2, status = ?3, currentversion = ?4, downloadurl = ?5, updated = ?6"
#define DB_SQL_PACKAGE_GET "SELECT updated FROM package WHERE basename = ?1"
//#define DB_SQL_PACKAGE_DEL "DELETE FROM package WHERE basename = ?1"

#define DB_SQL_PACKAGEVERSION_UPD "INSERT INTO package_version (basename, version, updated, created) VALUES (?1, ?2, ?3, ?3) ON CONFLICT(basename, version) DO UPDATE SET updated = ?3"
//#define DB_SQL_PACKAGEVERSION_UPD "UPDATE package_version SET updated = ?3 WHERE basename = ?1 AND version = ?2"
//#define DB_SQL_PACKAGEVERSION_GET "SELECT updated FROM package_version WHERE basename = ?1 AND version = ?2"
//#define DB_SQL_PACKAGEVERSION_DEL "DELETE FROM updated WHERE basename = ?1 AND version = ?2"

#define DB_SQL_PACKAGEVERSION_GET_SINCE "SELECT version FROM package_version WHERE basename = ?1 AND created >= ?2 ORDER BY id DESC"

/*
-- list package versions added in the last 24 hours
SELECT basename, version FROM package_version WHERE created >= strftime('%s') - 3600 * 24 ORDER BY created DESC, version DESC;

-- list packages with no versions found in the last 30 days
SELECT package.*, COUNT(package_version.version) AS versions_found, datetime(MAX(package_version.updated), 'unixepoch') AS version_last_updated FROM package
LEFT JOIN package_version
ON package.basename = package_version.basename
WHERE package.updated < strftime('%s') - 3600 * 24 * 30
GROUP BY(package.basename)
ORDER BY updated DESC;

-- clean up packages with no versions found in the last 30 days
DELETE FROM package_version WHERE basename IN (SELECT basename FROM package WHERE updated < strftime('%s') - 3600 * 24 * 30);
DELETE FROM package WHERE basename IN (SELECT basename FROM package WHERE updated < strftime('%s') - 3600 * 24 * 30);


-- list packages with no versions updated in the last 120 days
SELECT package.*, COUNT(package_version.version) AS versions_found, datetime(MAX(package_version.updated), 'unixepoch') AS version_last_updated FROM package
LEFT JOIN package_version
ON package.basename = package_version.basename
GROUP BY(package.basename)
HAVING version_last_updated < strftime('%s') - 3600 * 24 * 120
ORDER BY updated DESC;
*/

//!data structure for version check master database handle
struct versioncheckmasterdb {
  sqlite3* db;
};

struct versioncheckmasterdb* versioncheckmasterdb_open (const char* dbpath)
{
  struct versioncheckmasterdb* handle;
  if (!sqlite3_threadsafe())
    return NULL;
  if ((handle = (struct versioncheckmasterdb*)malloc(sizeof(struct versioncheckmasterdb))) == NULL)
    return NULL;
  //create database
  if (sqlite3_open_v2(dbpath, &handle->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX /*| SQLITE_OPEN_SHAREDCACHE*/, NULL) != SQLITE_OK) {
    free(handle);
    return NULL;
  }
  //enable WAL mode (see also: https://www.sqlite.org/wal.html)
  //  sqlite3_exec(handle->db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
  //create tables
  if (sqlite3_exec(handle->db, DB_SQL_CREATE_TABLES, NULL, NULL, NULL) != SQLITE_DONE) {
    //ignore error, assuming database and tables already exist
  }
  return handle;
}

void versioncheckmasterdb_close (struct versioncheckmasterdb* handle)
{
  if (handle) {
    if (handle->db) {
      sqlite3_close(handle->db);
    }
    free(handle);
  }
}

////////////////////////////////////////////////////////////////////////

struct versioncheckdb {
  sqlite3* db;
  sqlite3_stmt* db_pkg_upd;
#if 0
  sqlite3_stmt* db_pkg_get;
#endif
  sqlite3_stmt* db_pkgver_upd;
  sqlite3_stmt* db_pkgver_getsince;
};

#define PREP_SQL(var,sql) \
  if (sqlite3_prepare_v3(handle->db, sql, -1, SQLITE_PREPARE_PERSISTENT, &handle->var, NULL) != SQLITE_OK) { \
    free(handle); \
    return NULL; \
  }

struct versioncheckdb* versioncheckdb_open (struct versioncheckmasterdb* masterdb)
{
  if (!masterdb)
    return NULL;
  struct versioncheckdb* handle;
  if ((handle = (struct versioncheckdb*)malloc(sizeof(struct versioncheckdb))) == NULL)
    return NULL;
  handle->db = masterdb->db;
  //prepare SQL statements
  PREP_SQL(db_pkg_upd, DB_SQL_PACKAGE_UPD)
#if 0
  PREP_SQL(db_pkg_get, DB_SQL_PACKAGE_GET)
#endif
  PREP_SQL(db_pkgver_upd, DB_SQL_PACKAGEVERSION_UPD)
  PREP_SQL(db_pkgver_getsince, DB_SQL_PACKAGEVERSION_GET_SINCE)
  return handle;
}

#undef PREP_SQL

void versioncheckdb_close (struct versioncheckdb* handle)
{
  if (handle) {
    if (handle->db) {
      sqlite3_finalize(handle->db_pkg_upd);
#if 0
      sqlite3_finalize(handle->db_pkg_get);
#endif
      sqlite3_finalize(handle->db_pkgver_upd);
      sqlite3_finalize(handle->db_pkgver_getsince);
    }
    free(handle);
  }
}

int versioncheckdb_update_package (struct versioncheckdb* handle, const char* packagename, long responsecode, const char* status, const char* currentversion, const char* downloadurl)
{
  int result;
  if (!handle)
    return -1;
  sqlite3_bind_text(handle->db_pkg_upd, 1, packagename, -1, SQLITE_STATIC);
  sqlite3_bind_int64(handle->db_pkg_upd, 2, responsecode);
  sqlite3_bind_text(handle->db_pkg_upd, 3, status, -1, SQLITE_STATIC);
  sqlite3_bind_text(handle->db_pkg_upd, 4, currentversion, -1, SQLITE_STATIC);
  sqlite3_bind_text(handle->db_pkg_upd, 5, downloadurl, -1, SQLITE_STATIC);
  sqlite3_bind_int64(handle->db_pkg_upd, 6, time(NULL));
  if (sqlite3_step(handle->db_pkg_upd) != SQLITE_DONE)
    result = 1;
  sqlite3_clear_bindings(handle->db_pkg_upd);
  sqlite3_reset(handle->db_pkg_upd);
  return result;
}

#if 0
int versioncheckdb_get_package (struct versioncheckdb* handle, const char* packagename, time_t* updated)
{
  int result = 0;
  if (!handle)
    return -1;
  sqlite3_bind_text(handle->db_pkg_get, 1, packagename, -1, SQLITE_STATIC);
  if (sqlite3_step(handle->db_pkg_get) == SQLITE_ROW) {
    if (updated)
      *updated = sqlite3_column_int64(handle->db_pkg_get, 0);
    result++;
  }
  sqlite3_clear_bindings(handle->db_pkg_get);
  sqlite3_reset(handle->db_pkg_get);
  return result;
}
#endif

int versioncheckdb_update_package_version (struct versioncheckdb* handle, const char* packagename, const char* version)
{
  int result;
  if (!handle)
    return -1;
//printf("UPD_pkg_ver %s %s\n", packagename, version);/////
  sqlite3_bind_text(handle->db_pkgver_upd, 1, packagename, -1, SQLITE_STATIC);
  sqlite3_bind_text(handle->db_pkgver_upd, 2, version, -1, SQLITE_STATIC);
  sqlite3_bind_int64(handle->db_pkgver_upd, 3, time(NULL));
  if (sqlite3_step(handle->db_pkgver_upd) != SQLITE_DONE)
    result = 1;
  sqlite3_clear_bindings(handle->db_pkgver_upd);
  sqlite3_reset(handle->db_pkgver_upd);
  return result;
}

void versioncheckdb_group_start (struct versioncheckdb* handle)
{
  if (!handle)
    return;
  sqlite3_exec(handle->db, "BEGIN TRANSACTION", NULL, NULL, NULL);
}

int versioncheckdb_group_end (struct versioncheckdb* handle)
{
  if (!handle)
    return -1;
  return (sqlite3_exec(handle->db, "END TRANSACTION", NULL, NULL, NULL) == SQLITE_DONE ? 0 : 1);
}

void versioncheckdb_list_new_package_versions (struct versioncheckdb* handle, const char* packagename, time_t since, versioncheckdb_list_new_package_versions_callback_fn callback, void* callbackdata)
{
  sqlite3_bind_text(handle->db_pkgver_getsince, 1, packagename, -1, SQLITE_STATIC);
  sqlite3_bind_int64(handle->db_pkgver_getsince, 2, since);
  while (sqlite3_step(handle->db_pkgver_getsince) == SQLITE_ROW) {
    if ((*callback)((char*)sqlite3_column_text(handle->db_pkgver_getsince, 0), callbackdata) != 0)
      break;
  }
  sqlite3_clear_bindings(handle->db_pkgver_getsince);
  sqlite3_reset(handle->db_pkgver_getsince);
}
