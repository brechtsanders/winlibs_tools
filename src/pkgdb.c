#include "pkgdb.h"
#include "filesystem.h"
#include "memory_buffer.h"
#include "winlibs_common.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////

#define PKGDB_VERSION 2

const char* pkgdb_sql_create[] = {
  //database version 1
  "CREATE TABLE dbinfo (" \
  " version INT NOT NULL," \
  " created INT NOT NULL" \
  ");" \
  "CREATE TABLE package (" \
  " basename TEXT PRIMARY KEY NOT NULL," \
  " version TEXT," \
  " name TEXT," \
  " description TEXT," \
  " url TEXT," \
  " downloadurl TEXT," \
  " downloadsourceurl TEXT," \
  " category TEXT," \
  " type TEXT," \
  " versiondate TEXT," \
  " licensefile TEXT," \
  " licensetype TEXT," \
  " status TEXT," \
  " installed INT NOT NULL" \
  ");" \
  "CREATE TABLE package_dependency (" \
  " package TEXT NOT NULL," \
  " type INT NOT NULL," \
  " name TEXT NOT NULL," \
  " FOREIGN KEY(package) REFERENCES package(basename)" \
  ");" \
  "CREATE INDEX idx_package_dependency_package ON package_dependency (package);" \
  "CREATE INDEX idx_package_dependency_package_type ON package_dependency (package, type);" \
  "CREATE INDEX idx_package_dependency_name ON package_dependency (name);" \
  "CREATE INDEX idx_package_dependency_type_name ON package_dependency (type, name);" \
  "CREATE UNIQUE INDEX idx_package_dependency_package_type_name_type ON package_dependency (package, type, name);" \
  "CREATE TABLE package_path (" \
  " package TEXT NOT NULL," \
  " type INT NOT NULL," \
  " path TEXT NOT NULL," \
  " FOREIGN KEY(package) REFERENCES package(basename)" \
  ");" \
  "CREATE INDEX idx_package_path_package ON package_path (package);" \
  "CREATE INDEX idx_package_path_path ON package_path (path);" \
  "CREATE UNIQUE INDEX idx_package_path_package_type_path ON package_path (package, type, path);",
  //database version 2
  "CREATE TABLE package_category (" \
  " package TEXT NOT NULL," \
  " category TEXT NOT NULL," \
  " created INT NOT NULL," \
  " FOREIGN KEY(package) REFERENCES package(basename)"
  ");" \
  "CREATE INDEX idx_package_category_package ON package_category (package);" \
  "CREATE INDEX idx_package_category_category ON package_category (category);" \
  "CREATE UNIQUE INDEX idx_package_category_package_category ON package_category (package, category);"
};

#define SQL_BEGIN_TRANSACTION "BEGIN TRANSACTION;"
#define SQL_END_TRANSACTION "COMMIT TRANSACTION;"
#define SQL_ABORT_TRANSACTION "ROLLBACK TRANSACTION;"
#define SQL_GET_DBVERSION "SELECT MAX(version) FROM dbinfo"
#define SQL_SET_DBVERSION "INSERT INTO dbinfo (version, created) VALUES (?, strftime('%s','now'));"
#define SQL_GET_PACKAGE_VERSION "SELECT version FROM package WHERE basename=?"
#define SQL_DEL_PACKAGE "DELETE FROM package WHERE basename=?"
#define SQL_DEL_PACKAGE_PATHS "DELETE FROM package_path WHERE package=?"
#define SQL_DEL_PACKAGE_DEPENDENCIES "DELETE FROM package_dependency WHERE package=?"
#define SQL_ADD_PACKAGE "INSERT INTO package (basename, version, name, description, url, downloadurl, downloadsourceurl, category, type, versiondate, licensefile, licensetype, status, installed) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s','now'))"
#define SQL_ADD_PACKAGE_PATH "INSERT INTO package_path (package, type, path) VALUES (?, ?, ?)"
#define SQL_ADD_PACKAGE_DEPENDENCY "INSERT INTO package_dependency (package, type, name) VALUES (?, ?, ?)"
#define SQL_SELECT_PACKAGE "SELECT basename, version, name, description, url, downloadurl, downloadsourceurl, category, type, versiondate, licensefile, licensetype, status, installed FROM package"
#define SQL_GET_PACKAGE SQL_SELECT_PACKAGE " WHERE basename=?"
#define SQL_GET_PACKAGE_DEPENDENCIES "SELECT type, name FROM package_dependency WHERE package=?"
#define SQL_GET_PACKAGE_PATHS "SELECT type, path FROM package_path WHERE package=?"
#define SQL_GET_PACKAGE_FILES_OR_FOLDERS "SELECT path FROM package_path WHERE package=? AND type=?"
#define SQL_SET_PACKAGE_CATEGORY "INSERT INTO package_category (package, category, created) VALUES (?, ?, strftime('%s','now'))"
#define SQL_DEL_PACKAGE_CATEGORIES "DELETE FROM package_category WHERE package=?"

#define PACKAGE_DEPENDENCY_TYPE_OPTIONAL         0
#define PACKAGE_DEPENDENCY_TYPE_MANDATORY        1
//#define PACKAGE_DEPENDENCY_TYPE_BUILD_OPTIONAL   2
#define PACKAGE_DEPENDENCY_TYPE_BUILD            3

#define PACKAGE_PATH_TYPE_FILE              0
#define PACKAGE_PATH_TYPE_FOLDER            1

/*
-- to list sqlite3 tables:
SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%';

-- get all files and folders for package expat
SELECT path, type FROM package_path WHERE package = 'expat' ORDER BY type;

-- list all files provided by multiple packages
SELECT path, COUNT(path) AS count FROM package_path WHERE type = 0 GROUP BY path HAVING count > 1 ORDER BY count DESC, path;

-- get files provided by multiple packages
SELECT p1.path, p1.package, p2.package
FROM package_path AS p1
JOIN package_path AS p2
ON p1.path = p2.path
AND p1.package < p2.package
AND p1.type = 0 AND p2.type = 0
ORDER BY p1.path, p1.package, p2.package;

-- get files provided by multiple packages
SELECT p1.path, p2.package
FROM (SELECT path, COUNT(path) AS count FROM package_path WHERE type = 0 GROUP BY path HAVING count > 1 ORDER BY path) AS p1
LEFT JOIN package_path AS p2
ON p1.path = p2.path
WHERE p2.type = 0
ORDER BY p1.path, p2.package;

sqlite3 $MINGWPREFIX/var/lib/winlibs/wl-pkg.db "SELECT p1.path, p2.package FROM (SELECT path, COUNT(path) AS count FROM package_path WHERE type = 0 GROUP BY path HAVING count > 1 ORDER BY path) AS p1 LEFT JOIN package_path AS p2 ON p1.path = p2.path WHERE p2.type = 0 ORDER BY p1.path, p2.package"

*/

////////////////////////////////////////////////////////////////////////

const char* package_metadata_field_name[] = {
  "Name",
  "Version",
  "Title",
  "Description",
  "Website URL",
  "Downloads URL",
  "Source code URL",
  "Category",
  "Type",
  "Version date",
  "License file",
  "License type",
  "Status",
  NULL
};

struct package_metadata_struct* package_metadata_create ()
{
  int i;
  struct package_metadata_struct* metadata;
  if ((metadata = (struct package_metadata_struct*)malloc(sizeof(struct package_metadata_struct))) == NULL)
    return NULL;
  for (i = 0; i < PACKAGE_METADATA_TOTAL_FIELDS; i++)
    metadata->datafield[i] = NULL;
  metadata->fileexclusions = sorted_unique_list_create(strcmp, free);
  metadata->folderexclusions = sorted_unique_list_create(strcmp, free);
  metadata->filelist = sorted_unique_list_create(strcmp, free);
  metadata->folderlist = sorted_unique_list_create(strcmp, free);
  metadata->dependencies = sorted_unique_list_create(strcmp, free);
  metadata->optionaldependencies = sorted_unique_list_create(strcmp, free);
  metadata->builddependencies = sorted_unique_list_create(strcmp, free);
  metadata->totalsize = 0;
  return metadata;
}

void package_metadata_free (struct package_metadata_struct* metadata)
{
  int i;
  if (!metadata)
    return;
  for (i = 0; i < PACKAGE_METADATA_TOTAL_FIELDS; i++)
    if (metadata->datafield[i])
      free(metadata->datafield[i]);
  sorted_unique_list_free(metadata->fileexclusions);
  sorted_unique_list_free(metadata->folderexclusions);
  sorted_unique_list_free(metadata->filelist);
  sorted_unique_list_free(metadata->folderlist);
  sorted_unique_list_free(metadata->dependencies);
  sorted_unique_list_free(metadata->optionaldependencies);
  sorted_unique_list_free(metadata->builddependencies);
  free(metadata);
}

////////////////////////////////////////////////////////////////////////

typedef int (*comma_separated_list_callback_fn)(const char* data, size_t datalen, void* callbackdata);

int iterate_comma_separated_list (const char* list, comma_separated_list_callback_fn callback, void* callbackdata)
{
  const char* p;
  const char* q;
  int result;
  p = list;
  while (p && *p) {
    q = p;
    while (*q && *q != ',')
      q++;
    if (q > p) {
      if ((result = (*callback)(p, q - p, callbackdata)) != 0)
        return result;
      p = q;
    }
    if (*p)
      p++;
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////

#define RETRY_ATTEMPTS 12
#define RETRY_WAIT_TIME 250
#ifdef _WIN32
#include <windows.h>
#define WAIT_BEFORE_RETRY(ms) Sleep(ms);
#else
#define WAIT_BEFORE_RETRY(ms) usleep(ms * 1000);
#endif

sqlite3_stmt* execute_sql_query (sqlite3* sqliteconn, const char* sql, int* status, const char** nextsql)
{
  sqlite3_stmt* sqlresult;
  size_t n = 0;
  if ((*status = sqlite3_prepare_v2(sqliteconn, sql, -1, &sqlresult, nextsql)) == SQLITE_OK) {
    while (n++ < RETRY_ATTEMPTS && ((*status = sqlite3_step(sqlresult)) == SQLITE_BUSY || *status == SQLITE_LOCKED)) {
      WAIT_BEFORE_RETRY(RETRY_WAIT_TIME)
    }
    if (*status == SQLITE_ROW || *status == SQLITE_DONE)
      return sqlresult;
  }
  return NULL;
}

int execute_sql_cmds (sqlite3* sqliteconn, const char* sql)
{
  sqlite3_stmt* sqlresult;
  const char* nextsql;
  int status = SQLITE_ERROR;
  while (sql && *sql) {
    if ((sqlresult = execute_sql_query(sqliteconn, sql, &status, &nextsql)) != NULL)
      sqlite3_finalize(sqlresult);
else { fprintf(stderr, "Error %i (%s) in SQL:\n%s\n", status, sqlite3_errstr(status), sql); break; }/////
    sql = nextsql;
  }
  return status;
}

int execute_sql_cmd (sqlite3* sqliteconn, const char* sql)
{
  sqlite3_stmt* sqlresult;
  int status = SQLITE_ERROR;
  if ((sqlresult = execute_sql_query(sqliteconn, sql, &status, NULL)) != NULL)
    sqlite3_finalize(sqlresult);
else fprintf(stderr, "Error %i (%s) in SQL:\n%s\n", status, sqlite3_errstr(status), sql);/////
  return status;
}

int64_t get_sql_int64 (sqlite3* sqliteconn, const char* sql, int64_t defaultvalue)
{
  int64_t result = defaultvalue;
  int status;
  sqlite3_stmt* sqlresult;
  if ((sqlresult = execute_sql_query(sqliteconn, sql, &status, NULL)) != NULL) {
    if (status == SQLITE_ROW)
      result = sqlite3_column_int64(sqlresult, 0);
    sqlite3_finalize(sqlresult);
  }
  return result;
}

char* get_sql_str (sqlite3* sqliteconn, const char* sql)
{
  char* result = NULL;
  int status;
  sqlite3_stmt* sqlresult;
  if ((sqlresult = execute_sql_query(sqliteconn, sql, &status, NULL)) != NULL) {
    if (status == SQLITE_ROW) {
      const char* s;
      if ((s = (char*)sqlite3_column_text(sqlresult, 0)) != NULL)
        result = strdup(s);
    }
    sqlite3_finalize(sqlresult);
  }
  return result;
}

sqlite3_stmt* execute_sql_query_param_int (sqlite3* sqliteconn, const char* sql, int* status, int64_t param1)
{
	sqlite3_stmt* sqlresult;
	if ((*status = sqlite3_prepare_v2(sqliteconn, sql, -1, &sqlresult, NULL)) == SQLITE_OK) {
    sqlite3_bind_int64(sqlresult, 1, param1);
    if ((*status = pkgdb_sql_query_next_row(sqlresult)) == SQLITE_ROW || *status == SQLITE_DONE)
      return sqlresult;
	}
	return NULL;
}

int execute_sql_cmd_param_int (sqlite3* sqliteconn, const char* sql, int64_t param1)
{
  sqlite3_stmt* sqlresult;
  int status = SQLITE_ERROR;
  if ((sqlresult = execute_sql_query_param_int(sqliteconn, sql, &status, param1)) != NULL)
    sqlite3_finalize(sqlresult);
else fprintf(stderr, "Error %i (%s) in SQL:\n%s\n", status, sqlite3_errstr(status), sql);/////
  return status;
}

sqlite3_stmt* execute_sql_query_param_str (sqlite3* sqliteconn, const char* sql, int* status, const char* param1)
{
	sqlite3_stmt* sqlresult;
	if ((*status = sqlite3_prepare_v2(sqliteconn, sql, -1, &sqlresult, NULL)) == SQLITE_OK) {
    sqlite3_bind_text(sqlresult, 1, param1, -1, NULL);
    if ((*status = pkgdb_sql_query_next_row(sqlresult)) == SQLITE_ROW || *status == SQLITE_DONE)
      return sqlresult;
	}
	return NULL;
}

int execute_sql_cmd_param_str (sqlite3* sqliteconn, const char* sql, const char* param1)
{
  sqlite3_stmt* sqlresult;
  int status = SQLITE_ERROR;
  if ((sqlresult = execute_sql_query_param_str(sqliteconn, sql, &status, param1)) != NULL)
    sqlite3_finalize(sqlresult);
else fprintf(stderr, "Error %i (%s) in SQL:\n%s\n", status, sqlite3_errstr(status), sql);/////
  return status;
}

char* get_sql_str_param_str (sqlite3* sqliteconn, const char* sql, const char* param1)
{
  int status;
  sqlite3_stmt* sqlresult;
  char* result = NULL;
  if ((sqlresult = execute_sql_query_param_str(sqliteconn, sql, &status, param1)) != NULL) {
    if (status == SQLITE_ROW) {
      const char* s;
      if ((s = (char*)sqlite3_column_text(sqlresult, 0)) != NULL)
        result = strdup(s);
    }
    sqlite3_finalize(sqlresult);
  }
  return result;
}

sqlite3_stmt* execute_sql_query_param_str_int (sqlite3* sqliteconn, const char* sql, int* status, const char* param1, int64_t param2)
{
	sqlite3_stmt* sqlresult;
	if ((*status = sqlite3_prepare_v2(sqliteconn, sql, -1, &sqlresult, NULL)) == SQLITE_OK) {
    sqlite3_bind_text(sqlresult, 1, param1, -1, NULL);
    sqlite3_bind_int64(sqlresult, 2, param2);
    if ((*status = pkgdb_sql_query_next_row(sqlresult)) == SQLITE_ROW || *status == SQLITE_DONE)
      return sqlresult;
	}
	return NULL;
}

////////////////////////////////////////////////////////////////////////

struct pkgdb_handle_struct {
  sqlite3* db;
  char* rootpath;
};

struct pkgdb_handle_struct* pkgdb_open (const char* rootpath)
{
  sqlite3* db;
  struct pkgdb_handle_struct* handle = NULL;
  struct memory_buffer* dbpath = memory_buffer_create();
  //abort if destination path is not specified or does not exist
  if (!rootpath || !*rootpath || !folder_exists(rootpath)) {
    return NULL;
  }
  //determine package information path
  memory_buffer_set_printf(dbpath, "%s%c%s", rootpath, PATH_SEPARATOR, PACKAGE_DATABASE_PATH);
  recursive_mkdir(memory_buffer_get(dbpath));
  memory_buffer_append_printf(dbpath, "%c%s", PATH_SEPARATOR, PACKAGE_DATABASE_FILE);
  //open database
  if (sqlite3_open(memory_buffer_get(dbpath), &db) == SQLITE_OK) {
    if ((handle = (struct pkgdb_handle_struct*)malloc(sizeof(struct pkgdb_handle_struct))) == NULL) {
      sqlite3_close(db);
    } else {
      int64_t dbversion;
      handle->db = db;
      //check if database exists
      dbversion = get_sql_int64(db, SQL_GET_DBVERSION, 0);
      if (dbversion < PKGDB_VERSION) {
        if (dbversion < 0)
          dbversion = 0;
        //create database if it doesn't exist or upgrade to current level if needed
        for (; dbversion < PKGDB_VERSION; dbversion++) {
          execute_sql_cmd(db, SQL_BEGIN_TRANSACTION);
          execute_sql_cmds(db, pkgdb_sql_create[dbversion]);
          execute_sql_cmd_param_int(db, SQL_SET_DBVERSION, dbversion + 1);
          execute_sql_cmd(db, SQL_END_TRANSACTION);
        }
      }
      //set other data
      handle->rootpath = strdup(rootpath);
    }
  }
  memory_buffer_free(dbpath);
  return handle;
}

void pkgdb_close (pkgdb_handle handle)
{
  if (handle) {
    sqlite3_close(handle->db);
    free(handle->rootpath);
    free(handle);
  }
}

const char* pkgdb_get_rootpath (pkgdb_handle handle)
{
  if (!handle)
    return NULL;
  return handle->rootpath;
}

size_t pkgdb_add_sorted_unique_list (pkgdb_handle handle, const char* sql, const char* package, sorted_unique_list* sortuniqlist, int64_t type)
{
  unsigned int i;
  unsigned int n;
  int status;
  const char* value;
  sqlite3_stmt* sqlresult;
  size_t count = 0;
  if ((n = sorted_unique_list_size(sortuniqlist)) == 0)
    return 0;
  if ((status = sqlite3_prepare_v2(handle->db, sql, -1, &sqlresult, NULL)) == SQLITE_OK) {
    sqlite3_bind_text(sqlresult, 1, package, -1, SQLITE_STATIC);
    sqlite3_bind_int64(sqlresult, 2, type);
    for (i = 0; i < n; i++) {
      if ((value = sorted_unique_list_get(sortuniqlist, i)) != NULL) {
        sqlite3_bind_text(sqlresult, 3, value, -1, SQLITE_STATIC);
        status = pkgdb_sql_query_next_row(sqlresult);
/*
        if (status != SQLITE_OK && status != SQLITE_DONE) {
          fprintf(stderr, "Error %i\n", (int)status);/////
        }
*/
        count++;
        sqlite3_reset(sqlresult);
      }
    }
    sqlite3_finalize(sqlresult);
  }
  return count;
}

struct set_package_category_callback_struct {
  pkgdb_handle handle;
  const char* package;
};

int set_package_category_callback (const char* data, size_t datalen, void* callbackdata)
{
  int status;
	sqlite3_stmt* sqlresult;
	if ((status = sqlite3_prepare_v2(((struct set_package_category_callback_struct*)callbackdata)->handle->db, SQL_SET_PACKAGE_CATEGORY, -1, &sqlresult, NULL)) == SQLITE_OK) {
    sqlite3_bind_text(sqlresult, 1, ((struct set_package_category_callback_struct*)callbackdata)->package, -1, NULL);
    sqlite3_bind_text(sqlresult, 2, data, datalen, NULL);
    status = pkgdb_sql_query_next_row(sqlresult);
    sqlite3_finalize(sqlresult);
	}
	return 0;
}

int pkgdb_install_package (pkgdb_handle handle, const struct package_metadata_struct* pkginfo)
{
  int i;
  size_t n;
  int status;
  struct set_package_category_callback_struct categorydata;
  sqlite3_stmt* sqlresult;
  char* detectedversion;
  if ((detectedversion = get_sql_str_param_str(handle->db, SQL_GET_PACKAGE_VERSION, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME])) != NULL) {
    /////printf("Version already installed: %s\n", detectedversion);
    free(detectedversion);
  }
  execute_sql_cmd(handle->db, SQL_BEGIN_TRANSACTION);
  n = 0;
  if ((status = sqlite3_prepare_v2(handle->db, SQL_ADD_PACKAGE, -1, &sqlresult, NULL)) == SQLITE_OK) {
    for (i = 0; i < PACKAGE_METADATA_TOTAL_FIELDS; i++)
      sqlite3_bind_text(sqlresult, i + 1, pkginfo->datafield[i], -1, SQLITE_STATIC);
    while (n++ < RETRY_ATTEMPTS && ((status = sqlite3_step(sqlresult)) == SQLITE_BUSY || status == SQLITE_LOCKED)) {
      WAIT_BEFORE_RETRY(RETRY_WAIT_TIME)
    }
    if (status == SQLITE_DONE) {
    }
    sqlite3_finalize(sqlresult);
    pkgdb_add_sorted_unique_list(handle, SQL_ADD_PACKAGE_DEPENDENCY, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], pkginfo->dependencies, PACKAGE_DEPENDENCY_TYPE_MANDATORY);
    pkgdb_add_sorted_unique_list(handle, SQL_ADD_PACKAGE_DEPENDENCY, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], pkginfo->optionaldependencies, PACKAGE_DEPENDENCY_TYPE_OPTIONAL);
    pkgdb_add_sorted_unique_list(handle, SQL_ADD_PACKAGE_DEPENDENCY, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], pkginfo->builddependencies, PACKAGE_DEPENDENCY_TYPE_BUILD);
    pkgdb_add_sorted_unique_list(handle, SQL_ADD_PACKAGE_PATH, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], pkginfo->filelist, PACKAGE_PATH_TYPE_FILE);
    pkgdb_add_sorted_unique_list(handle, SQL_ADD_PACKAGE_PATH, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], pkginfo->folderlist, PACKAGE_PATH_TYPE_FOLDER);
    execute_sql_cmd_param_str(handle->db, SQL_DEL_PACKAGE_CATEGORIES, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
    categorydata.handle = handle;
    categorydata.package = pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME];
    iterate_comma_separated_list(pkginfo->datafield[PACKAGE_METADATA_INDEX_CATEGORY], set_package_category_callback, &categorydata);
  }
  execute_sql_cmd(handle->db, SQL_END_TRANSACTION);
  return status;
}

int pkgdb_uninstall_package (pkgdb_handle handle, const char* package)
{
  int status;
  int abort = 0;
  execute_sql_cmd(handle->db, SQL_BEGIN_TRANSACTION);
  if (!abort && (status = execute_sql_cmd_param_str(handle->db, SQL_DEL_PACKAGE_PATHS, package)) != SQLITE_OK && status != SQLITE_DONE)
    abort = 1;
  if (!abort && (status = execute_sql_cmd_param_str(handle->db, SQL_DEL_PACKAGE_DEPENDENCIES, package)) != SQLITE_OK && status != SQLITE_DONE)
    abort = 2;
  if (!abort && (status = execute_sql_cmd_param_str(handle->db, SQL_DEL_PACKAGE_CATEGORIES, package)) != SQLITE_OK && status != SQLITE_DONE)
    abort = 3;
  if (!abort && (status = execute_sql_cmd_param_str(handle->db, SQL_DEL_PACKAGE, package)) != SQLITE_OK && status != SQLITE_DONE)
    abort = 4;
  if (abort) {
    execute_sql_cmd(handle->db, SQL_ABORT_TRANSACTION);
    return status;
  }
  execute_sql_cmd(handle->db, SQL_END_TRANSACTION);
  return 0;
}

struct package_metadata_struct* pkgdb_read_package (pkgdb_handle handle, const char* package)
{
  int i;
  int status;
  uint64_t type;
  const char* s;
  sqlite3_stmt* sqlresult;
  struct package_metadata_struct* pkginfo = NULL;
  //allocate structure to hold package information
  if ((pkginfo = package_metadata_create()) == NULL)
    return NULL;
  //get package information
  i = 0;
  if ((sqlresult = execute_sql_query_param_str(handle->db, SQL_GET_PACKAGE, &status, package)) != NULL) {
    if (status == SQLITE_ROW) {
      for (i = 0; i < PACKAGE_METADATA_TOTAL_FIELDS; i++) {
        s = (char*)sqlite3_column_text(sqlresult, i);
        if (pkginfo->datafield[i])
          free(pkginfo->datafield[i]);
        pkginfo->datafield[i] = (s ? strdup(s) : NULL);
      }
    }
    sqlite3_finalize(sqlresult);
  }
  if (i == 0) {
    package_metadata_free(pkginfo);
    return NULL;
  }
  //get package dependencies
  if ((sqlresult = execute_sql_query_param_str(handle->db, SQL_GET_PACKAGE_DEPENDENCIES, &status, package)) != NULL) {
    while (status == SQLITE_ROW) {
      type = sqlite3_column_int64(sqlresult, 0);
      s = (char*)sqlite3_column_text(sqlresult, 1);
      switch (type) {
        case PACKAGE_DEPENDENCY_TYPE_MANDATORY:
          sorted_unique_list_add(pkginfo->dependencies, s);
          break;
        case PACKAGE_DEPENDENCY_TYPE_OPTIONAL:
          sorted_unique_list_add(pkginfo->optionaldependencies, s);
          break;
        case PACKAGE_DEPENDENCY_TYPE_BUILD:
          sorted_unique_list_add(pkginfo->builddependencies, s);
          break;
      }
      status = pkgdb_sql_query_next_row(sqlresult);
    }
    sqlite3_finalize(sqlresult);
  }
  //get package file and folders
  if ((sqlresult = execute_sql_query_param_str(handle->db, SQL_GET_PACKAGE_PATHS, &status, package)) != NULL) {
    while (status == SQLITE_ROW) {
      type = sqlite3_column_int64(sqlresult, 0);
      s = (char*)sqlite3_column_text(sqlresult, 1);
      switch (type) {
        case PACKAGE_PATH_TYPE_FILE:
          sorted_unique_list_add(pkginfo->filelist, s);
          break;
        case PACKAGE_PATH_TYPE_FOLDER:
          sorted_unique_list_add(pkginfo->folderlist, s);
          break;
      }
      status = pkgdb_sql_query_next_row(sqlresult);
    }
    sqlite3_finalize(sqlresult);
  }
  return pkginfo;
}

int pkgdb_interate_package_files_or_folders (pkgdb_handle handle, const char* package, int64_t type, pkgdb_file_folder_callback_fn callback, void* callbackdata)
{
  int status;
  sqlite3_stmt* sqlresult;
  const char* s;
  int abort = 0;
  //get list of files/folders
  if ((sqlresult = execute_sql_query_param_str_int(handle->db, SQL_GET_PACKAGE_FILES_OR_FOLDERS, &status, package, type)) == NULL)
    return -1;
  while (!abort && status == SQLITE_ROW) {
    if ((s = (char*)sqlite3_column_text(sqlresult, 0)) != NULL)
      abort = (*callback)(handle, s, callbackdata);
    status = pkgdb_sql_query_next_row(sqlresult);
  }
  sqlite3_finalize(sqlresult);
  return abort;
}

int pkgdb_interate_package_files (pkgdb_handle handle, const char* package, pkgdb_file_folder_callback_fn callback, void* callbackdata)
{
  return pkgdb_interate_package_files_or_folders(handle, package, PACKAGE_PATH_TYPE_FILE, callback, callbackdata);
}

int pkgdb_interate_package_folders (pkgdb_handle handle, const char* package, pkgdb_file_folder_callback_fn callback, void* callbackdata)
{
  return pkgdb_interate_package_files_or_folders(handle, package, PACKAGE_PATH_TYPE_FOLDER, callback, callbackdata);
}

sqlite3* pkgdb_get_sqlite3_handle (pkgdb_handle handle)
{
  return (handle ? handle->db : NULL);
}

sqlite3_stmt* pkgdb_sql_query_param_str (pkgdb_handle handle, const char* sql, int* status, const char* param1)
{
  return execute_sql_query_param_str(handle->db, sql, status, param1);
}

int pkgdb_sql_query_next_row (sqlite3_stmt* sqlresult)
{
  int status = SQLITE_ERROR;
  size_t attempt = 0;
  while (attempt++ < RETRY_ATTEMPTS && ((status = sqlite3_step(sqlresult)) == SQLITE_BUSY || status == SQLITE_LOCKED)) {
    WAIT_BEFORE_RETRY(RETRY_WAIT_TIME)
  }
  return status;
}



/////https://www.sqlite.org/inmemorydb.html#temp_db
/*
ATTACH DATABASE '' AS oldpkg;
--CREATE TABLE oldpkg.package AS SELECT * FROM package WHERE basename='pcre2';
--CREATE TABLE oldpkg.package_dependency AS SELECT * FROM package_dependency WHERE package='pcre2';
CREATE TABLE oldpkg.package_path AS SELECT * FROM package_path WHERE package='pcre2';
--SELECT * FROM oldpkg.package;
--SELECT name FROM oldpkg.sqlite_master WHERE type ='table' AND name NOT LIKE 'sqlite_%';

--CREATE INDEX oldpkg.idx_package_path_package ON package_path (package);
CREATE INDEX oldpkg.idx_package_path_path ON package_path (path);

--added files
SELECT new.type, new.path FROM package_path AS new
LEFT JOIN oldpkg.package_path AS old
ON new.package = old.package AND new.path = old.path
WHERE new.package = 'pcre2' AND old.path IS NULL
ORDER BY new.path;

--deleted files
SELECT old.type, old.path FROM oldpkg.package_path AS old
LEFT JOIN package_path AS new
ON old.package = new.package AND old.path = new.path
WHERE old.package = 'pcre2' AND new.path IS NULL
ORDER BY old.path;
*/



/////TO DO: update category table on package (un)install
