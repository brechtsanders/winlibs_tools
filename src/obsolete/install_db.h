/*
  header file for functions related to the database of installed packages
*/

#ifndef INCLUDED_INSTALL_DB_H
#define INCLUDED_INSTALL_DB_H

#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

//!check if installation path exists
/*!
  \param  installpath           installation path
  \return zero on success or non-zero on error
*/
int install_db_check_install_path (const char* installpath);

//!structure used for returning package information
struct install_db_get_package_info_struct {
  char* installpath;
  char* packagename;
  char* version;
};

//!callback function used to iterate folders or files of an installed package
/*!
  \param  packageinfo           package information
  \param  path                  path of folder or file
  \param  callbackdata          user data
  \return zero to continue or non-zero to abort the current listing
*/
typedef int (*install_db_get_package_info_callback_fn)(struct install_db_get_package_info_struct* packageinfo, const char* path, void* callbackdata);

//!get information about installed package
/*!
  \param  installpath           installation path
  \param  packagename           package name
  \param  foldercallback        callback function to call for each folder in the installed package
  \param  foldercallbackdata    user data to pass to the folder callback function
  \param  filecallback          callback function to call for each file in the installed package
  \param  filecallbackdata      user data to pass to the file callback function
  \return installed version or NULL if package was not found
*/
struct install_db_get_package_info_struct* install_db_get_package_info (const char* installpath, const char* packagename);

//!iterate through all folders in installed package
/*!
  \param  packageinfo           package information
  \param  foldercallback        callback function to call for each folder in the installed package
  \param  foldercallbackdata    user data to pass to the folder callback function
  \return 0 if all folders were iterated, -1 on error or exit code of callback function
*/
int install_db_get_package_info_folders (struct install_db_get_package_info_struct* packageinfo, install_db_get_package_info_callback_fn foldercallback, void* foldercallbackdata);

//!iterate through all files in installed package
/*!
  \param  packageinfo           package information
  \param  filecallback          callback function to call for each file in the installed package
  \param  filecallbackdata      user data to pass to the file callback function
  \return 0 if all files were iterated, -1 on error or exit code of callback function
*/
int install_db_get_package_info_files (struct install_db_get_package_info_struct* packageinfo, install_db_get_package_info_callback_fn filecallback, void* filecallbackdata);

//!clean up structure used for returning package information
/*!
  \param  packageinfo           package information
*/
void install_db_get_package_info_free (struct install_db_get_package_info_struct* packageinfo);



/*
  struct install_db_install_package_struct {};
  struct install_db_install_package_struct* install_db_install_package (const char* installpath, const char* packagename);
  void install_db_install_package_close (struct install_db_install_package_struct* packageinfo);
  int install_db_install_package_folder (struct install_db_install_package_struct* packageinfo, const char* path);
  FILE* install_db_install_package_file_create (struct install_db_install_package_struct* packageinfo, const char* path);
  int install_db_install_package_file_close (struct install_db_install_package_struct* packageinfo, FILE* filehandle);
*/

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_INSTALL_DB_H
