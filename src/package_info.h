/*
  header file for package information functions
*/

#ifndef INCLUDED_PACKAGE_INFO_H
#define INCLUDED_PACKAGE_INFO_H

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct packageinfo_file_struct* packageinfo_file;
packageinfo_file open_packageinfo_file (const char* infopath, const char* basename);
void close_packageinfo_file (packageinfo_file pkgfile);
char* packageinfo_file_readline (packageinfo_file pkgfile);

//!check path(s) of build package information files
/*!
  \param  infopath              full path(s) of directory containing build information files
  \return 0 if no valid paths were found, < 0 on success, > 0 specifying the number of invalid folders in the list
*/
int check_packageinfo_paths (const char* infopath);

//!data structure for package information
struct package_info_struct {
  char* name;
  char* status;
  char* url;
  char* basename;
  char* description;
  char* category;
  char* type;
  char* version;
  size_t version_linenumber;
  char* versiondate;
  char* dependencies;
  char* optionaldependencies;
  char* builddependencies;
  char* licensefile;
  char* licensetype;
  char* downloadurldata;
  char* downloadsourceurl;
  int buildok;
  time_t lastchanged;
  void* extradata;
  void (*extradata_free_fn)(void*);
};

//!free package information
/*!
  \param  packageinfo           package information
*/
void free_packageinfo (struct package_info_struct* packageinfo);

//!get build package information from file
/*!
  \param  infopath              full path(s) of directory containing build information files
  \param  basename              name of package
  \return package information (or NULL on error), the caller must clean up with free_packageinfo()
*/
struct package_info_struct* read_packageinfo (const char* infopath, const char* basename);

//!get download information from package information
/*!
  \param  packageinfo           package information
  \param  url                   pointer that will receive the URL where the package sources can be downloaded
  \param  filematchprefix       pointer that will receive the source file matching prefix (or NULL if none specified)
  \param  filematchsuffix       pointer that will receive the source file matching suffix (or NULL if none specified)
*/
void get_package_downloadurl_info (struct package_info_struct* packageinfo, char** url, char** filematchprefix, char** filematchsuffix);

//!callback function used by read_packageinfolist (can be used to show progress)
/*!
  \param  basename              name of package
  \param  callbackdata          callback data passed to read_packageinfolist
  \return zero to continue processing, non-zero to abort
*/
typedef int (*package_callback_fn)(const char* basename, void* callbackdata);

//!iterate through build package information
/*!
  \param  infopath              full path(s) of directory containing build information files
  \param  callback              callback function to be called for each package
  \param  callbackdata          callback data passed to be passed to callback function
  \return number of packages processed
*/
size_t iterate_packages (const char* infopath, package_callback_fn callback, void* callbackdata);

//!iterate through build package information
/*!
  \param  list                  comma separated list of package names
  \param  callback              callback function to be called for each package name
  \param  callbackdata          callback data passed to be passed to callback function
  \return last result from callback (should be zero if all entries were processed)
*/
int iterate_packages_in_comma_separated_list (const char* list, package_callback_fn callback, void* callbackdata);

//!data structure for package information list
struct package_info_list_struct {
  struct package_info_struct* info;
  struct package_info_list_struct* next;
};

#if 0
//!get list of build package information
/*!
  \param  infopath              full path(s) of directory containing build information files
  \param  callback              callback function to be called for each package
  \param  callbackdata          callback data passed to be passed to callback function
  \return package information list (or NULL on error or if list is empty)
*/
struct package_info_list_struct* read_packageinfolist (const char* infopath, package_callback_fn callback, void* callbackdata);
#endif

//!free package information list
/*!
  \param  packagelist           package information list
  \param  basename              name of package
*/
void free_packageinfolist (struct package_info_list_struct* packagelist);

//!search package in package information list
/*!
  \param  packagelist           package information list
  \param  basename              name of package
  \return pointer to package information or NULL if not found
*/
struct package_info_list_struct* search_packageinfolist_by_basename (struct package_info_list_struct* packagelist, const char* basename);

//!add package and its dependencies to package information list
/*!
  \param  packagelist           package information list
  \param  infopath              full path(s) of directory containing build information files
  \param  basename              name of package
  \return zero on success or non-zero when the package was not added
*/
int insert_package_by_dependency (struct package_info_list_struct** packagelist, const char* infopath, const char* basename);

//!check if package is installed
/*!
  \param  installpath           path where packages are installed
  \param  basename              name of package
  \return non-zero if package is installed
*/
int package_is_installed (const char* installpath, const char* basename);

//!check if packages are installed
/*!
  \param  installpath           path where packages are installed
  \param  packagelist           comma separated list of package names
  \return non-zero if all packages are installed
*/
int packages_are_installed (const char* installpath, const char* packagelist);

//!check which version of package is installed
/*!
  \param  installpath           path where packages are installed
  \param  basename              name of package
  \return version (caller must free() the result) or NULL if not installed
*/
char* installed_package_version (const char* installpath, const char* basename);

//!check when the package was installed
/*!
  \param  installpath           path where packages are installed
  \param  basename              name of package
  \return timestamp of last installation or update or 0 if not installed
*/
time_t installed_package_lastchanged (const char* installpath, const char* basename);

//!remove missing packages from comma separated list
/*!
  \param  installpath           path where packages are installed
  \param  packagelist           comma separated list of package names (will be modified)
  \return number of entries removed from list
*/
int packages_remove_missing_from_list (const char* installpath, char* packagelist);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_PACKAGE_INFO_H
