/*
  header file for functions for the database used to check for new source releases
*/

#ifndef INCLUDED_VERSION_CHECK_DB_H
#define INCLUDED_VERSION_CHECK_DB_H

#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

//!data structure for version check master database handle
struct versioncheckmasterdb;

//!open version check database (only use in main thread)
/*!
  \return version check master database handle or NULL on error
*/
struct versioncheckmasterdb* versioncheckmasterdb_open (const char* dbpath);

//!close version check master database (only use in main thread)
/*!
  \param  handle                version check database handle
*/
void versioncheckmasterdb_close (struct versioncheckmasterdb* handle);



//!data structure for version check database handle
struct versioncheckdb;

//!open version check database
/*!
  \return version check database handle or NULL on error
*/
struct versioncheckdb* versioncheckdb_open (struct versioncheckmasterdb* masterdb);

//!close version check database
/*!
  \param  handle                version check database handle
*/
void versioncheckdb_close (struct versioncheckdb* handle);

//!update package information in version check database
/*!
  \param  handle                version check database handle
  \param  packagename           name of package
  \param  status                information about current status (set to NULL for no issues)
  \param  currentversion        current version
  \param  downloadurl           URL of page with available versions
  \return zero on success or non-zero on error
*/
int versioncheckdb_update_package (struct versioncheckdb* handle, const char* packagename, long responsecode, const char* status, const char* currentversion, const char* downloadurl);

#if 0
//!update package information from version check database
/*!
  \param  handle                version check database handle
  \param  packagename           name of package
  \param  updated               pointer that will receive latest timestamp (if not set to NULL)
  \return zero if not found, positive on success, negative on error
*/
int versioncheckdb_get_package (struct versioncheckdb* handle, const char* packagename, time_t* updated);
#endif

//!update package version information in version check database
/*!
  \param  handle                version check database handle
  \param  packagename           name of package
  \param  version               version
  \return zero on success or non-zero on error
*/
int versioncheckdb_update_package_version (struct versioncheckdb* handle, const char* packagename, const char* version);

//!group the next database actions together in one transaction
/*!
  \param  handle                version check database handle
*/
void versioncheckdb_group_start (struct versioncheckdb* handle);

//!end transaction group
/*!
  \param  handle                version check database handle
*/
int versioncheckdb_group_end (struct versioncheckdb* handle);

//!callback function used by versioncheckdb_list_new_package_versions (can be used to show progress)
/*!
  \param  version               version
  \param  callbackdata          callback data passed to versioncheckdb_list_new_package_versions
  \return zero to continue processing, non-zero to abort
*/
typedef int (*versioncheckdb_list_new_package_versions_callback_fn)(const char* version, void* callbackdata);

//!list new package versions
/*!
  \param  handle                version check database handle
  \param  packagename           name of package
  \param  since                 time stamp since when
*/
void versioncheckdb_list_new_package_versions (struct versioncheckdb* handle, const char* packagename, time_t since, versioncheckdb_list_new_package_versions_callback_fn callback, void* callbackdata);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_VERSION_CHECK_DB_H
