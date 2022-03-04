/*
  header file for functions related to writing to the database of installed packages
*/

#ifndef INCLUDED_PKGDB_H
#define INCLUDED_PKGDB_H

#include "pkg.h"
#include <stdint.h>
#include <stdlib.h>
#include <sqlite3.h>

#ifdef __cplusplus
extern "C" {
#endif

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
  \return package information or NULL on error, the caller must free the result with package_metadata_free()
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

//!check if packages are installed
/*!
  \param  handle                database handle
  \param  packagelist           comma separated list of package names
  \return non-zero if all packages are installed, zero if at least package is missing
*/
size_t pkgdb_packages_are_installed (pkgdb_handle handle, const char* packagelist);


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



//!callback function used by iterate_items_in_list()
/*!
  \param  item                  item data
  \param  callbackdata          callback data passed to read_packageinfolist
  \return zero to continue processing, non-zero to abort
*/
typedef size_t (*iterate_items_in_list_callback_fn)(const char* item, void* callbackdata);

//!iterate through list of items
/*!
  \param  itemlist              list of package names
  \param  separator             separator
  \param  callback              callback function to be called for each package name
  \param  callbackdata          callback data passed to be passed to callback function
  \return last result from callback (should be zero if all entries were processed)
*/
size_t iterate_items_in_list (const char* itemlist, char separator, iterate_items_in_list_callback_fn callback, void* callbackdata);



#ifdef __cplusplus
}
#endif

#endif //INCLUDED_PKGDB_H
