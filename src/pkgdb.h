/*
  header file for functions related to writing to the database of installed packages
*/

#ifndef INCLUDED_PKGDB_H
#define INCLUDED_PKGDB_H

#include "sorted_unique_list.h"
#include <stdint.h>
#include <stdlib.h>
#include <sqlite3.h>

#ifdef __cplusplus
extern "C" {
#endif

//!macros specifying package data field index
/*!
 * \name   PACKAGE_METADATA_INDEX_*
 * \{
 */
#define PACKAGE_METADATA_INDEX_BASENAME             0
#define PACKAGE_METADATA_INDEX_VERSION              1
#define PACKAGE_METADATA_INDEX_NAME                 2
#define PACKAGE_METADATA_INDEX_DESCRIPTION          3
#define PACKAGE_METADATA_INDEX_URL                  4
#define PACKAGE_METADATA_INDEX_DOWNLOADURL          5
#define PACKAGE_METADATA_INDEX_DOWNLOADSOURCEURL    6
#define PACKAGE_METADATA_INDEX_CATEGORY             7
#define PACKAGE_METADATA_INDEX_TYPE                 8
#define PACKAGE_METADATA_INDEX_VERSIONDATE          9
#define PACKAGE_METADATA_INDEX_LICENSEFILE          10
#define PACKAGE_METADATA_INDEX_LICENSETYPE          11
#define PACKAGE_METADATA_INDEX_STATUS               12
#define PACKAGE_METADATA_TOTAL_FIELDS               13
/*! @} */

//!names of package data fields
/*!
 * \sa     PACKAGE_METADATA_INDEX_*
 */
extern const char* package_metadata_field_name[];

//!data structure for package information
struct package_metadata_struct {
  char* datafield[PACKAGE_METADATA_TOTAL_FIELDS];
  sorted_unique_list* fileexclusions;
  sorted_unique_list* folderexclusions;
  sorted_unique_list* filelist;
  sorted_unique_list* folderlist;
  sorted_unique_list* dependencies;
  sorted_unique_list* optionaldependencies;
  sorted_unique_list* builddependencies;
  uint64_t totalsize;
};

//!create and initialize data structure for package information
struct package_metadata_struct* package_metadata_create ();

//!clean up data structure for package information
void package_metadata_free (struct package_metadata_struct* metadata);

//!handle type used for writing to package database
typedef struct pkgdb_handle_struct* pkgdb_handle;

//!open handle for writing to package database
/*!
  \param  rootpath              package install location
  \return a handle on success or NULL on error
*/
pkgdb_handle pkgdb_open (const char* rootpath);

//!close handle for writing to package database
/*!
  \param  handle                database handle
*/
void pkgdb_close (pkgdb_handle handle);

//!get installation root path for package database
/*!
  \param  handle                database handle
  \return root path or NULL on error (if handle is NULL)
*/
const char* pkgdb_get_rootpath (pkgdb_handle handle);

//!add package information to package database
/*!
  \param  handle                database handle
  \param  pkginfo               package information
*/
int pkgdb_install_package (pkgdb_handle handle, const struct package_metadata_struct* pkginfo);

//!remove package from package database
/*!
  \param  handle                database handle
  \param  package               package name
  \return 0 on success
*/
int pkgdb_uninstall_package (pkgdb_handle handle, const char* package);

//!read package information from package database
/*!
  \param  handle                database handle
  \param  package               package name
  \param  pkginfo               place where package information will be stored
  \return 0 on success
*/
struct package_metadata_struct* pkgdb_read_package (pkgdb_handle handle, const char* package);

//!callback function used to iterate folders or files of an installed package
/*!
  \param  handle                database handle
  \param  path                  path of folder or file (relative to install path)
  \param  callbackdata          user data
  \return zero to continue or non-zero to abort the current listing
*/
typedef int (*pkgdb_file_folder_callback_fn)(pkgdb_handle handle, const char* path, void* callbackdata);

//!iterate through all files in installed package
/*!
  \param  handle                database handle
  \param  package               package name
  \param  callback              callback function to call for each file in the installed package
  \param  callbackdata          user data to pass to the file callback function
  \return 0 if all files were iterated, -1 on error or exit code of callback function
*/
int pkgdb_interate_package_files (pkgdb_handle handle, const char* package, pkgdb_file_folder_callback_fn callback, void* callbackdata);

//!iterate through all folders in installed package
/*!
  \param  handle                database handle
  \param  package               package name
  \param  callback              callback function to call for each folder in the installed package
  \param  callbackdata          user data to pass to the file callback function
  \return 0 if all files were iterated, -1 on error or exit code of callback function
*/
int pkgdb_interate_package_folders (pkgdb_handle handle, const char* package, pkgdb_file_folder_callback_fn callback, void* callbackdata);

#if 0
//!callback function used to iterate packages
/*!
  \param  handle                database handle
  \param  pkginfo               package information (only \a datafield values are set)
  \param  callbackdata          user data
  \return zero to continue or non-zero to abort the current listing
*/
typedef int (*pkgdb_package_callback_fn)(pkgdb_handle handle, const struct package_metadata_struct* pkginfo, void* callbackdata);
#endif

//!get sqlite3 handleSQL query with 1 string parameter
/*!
  \param  handle                database handle
  \return sqlite3 handle or NULL on error
*/
sqlite3* pkgdb_get_sqlite3_handle (pkgdb_handle handle);

//!open an sqlite3 SQL query with 1 string parameter
/*!
  \param  handle                database handle
  \param  sql                   SQL query
  \param  status                pointer that will receive sqlite3 status code
  \param  param1                parameter
  \return sqlite3 prepared statement object or NULL on error
*/
sqlite3_stmt* pkgdb_sql_query_param_str (pkgdb_handle handle, const char* sql, int* status, const char* param1);

//!get next rowopen an sqlite3 SQL query with 1 string parameter
/*!
  \param  sqlresult             sqlite3 prepared statement object
  \return sqlite3 status code
*/
int pkgdb_sql_query_next_row (sqlite3_stmt* sqlresult);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_PKGDB_H
