#include "install_db.h"
#include "memory_buffer.h"
#include "filesystem.h"
#include "winlibs_common.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

int install_db_check_install_path (const char* installpath)
{
  return (folder_exists(installpath) ? 0 : -1);
}

struct install_db_get_package_info_struct* install_db_get_package_info (const char* installpath, const char* packagename)
{
  FILE* src;
  struct memory_buffer* pkginfopath = memory_buffer_create();
  struct memory_buffer* filepath = memory_buffer_create();
  struct install_db_get_package_info_struct* packageinfo;
  //allocate structure for result
  if ((packageinfo = (struct install_db_get_package_info_struct*)malloc(sizeof(struct install_db_get_package_info_struct))) == NULL)
    return NULL;
  packageinfo->installpath = strdup(installpath);
  packageinfo->packagename = strdup(packagename);
  packageinfo->version = NULL;
  //determine package information path
  memory_buffer_set_printf(pkginfopath, "%s%s%c%s%c", installpath, PACKAGE_INFO_PATH, PATH_SEPARATOR, packagename, PATH_SEPARATOR);
  //load version installed package
  if ((src = fopen(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_VERSION_FILE)), "rb")) != NULL) {
    packageinfo->version = readline_from_file(src);
    fclose(src);
  }
  //clean up
  memory_buffer_free(pkginfopath);
  memory_buffer_free(filepath);
  //abort if version information was not found
  if (!packageinfo->version) {
    free(packageinfo);
    return NULL;
  }
  return packageinfo;
}

int install_db_get_package_info_folders (struct install_db_get_package_info_struct* packageinfo, install_db_get_package_info_callback_fn foldercallback, void* foldercallbackdata)
{
  FILE* src;
  char* line;
  int abort;
  struct memory_buffer* pkginfopath;
  struct memory_buffer* filepath;
  if (!packageinfo || !packageinfo->installpath)
    return -1;
  //iterate folders
  abort = 0;
  if (foldercallback) {
    pkginfopath = memory_buffer_create();
    filepath = memory_buffer_create();
    //determine package information path
    memory_buffer_set_printf(pkginfopath, "%s%s%c%s%c", packageinfo->installpath, PACKAGE_INFO_PATH, PATH_SEPARATOR, packageinfo->packagename, PATH_SEPARATOR);
    //read and process list of folders
    if ((src = fopen(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_FOLDERS_FILE)), "rb")) != NULL) {
      while (!abort && (line = readline_from_file(src)) != NULL)
        abort = (*foldercallback)(packageinfo, line, foldercallbackdata);
      fclose(src);
    }
    //clean up
    memory_buffer_free(pkginfopath);
    memory_buffer_free(filepath);
  }
  return abort;
}

int install_db_get_package_info_files (struct install_db_get_package_info_struct* packageinfo, install_db_get_package_info_callback_fn filecallback, void* filecallbackdata)
{
  FILE* src;
  char* line;
  int abort;
  struct memory_buffer* pkginfopath;
  struct memory_buffer* filepath;
  if (!packageinfo || !packageinfo->installpath)
    return -1;
  //iterate files
  abort = 0;
  if (filecallback) {
    pkginfopath = memory_buffer_create();
    filepath = memory_buffer_create();
    //determine package information path
    memory_buffer_set_printf(pkginfopath, "%s%s%c%s%c", packageinfo->installpath, PACKAGE_INFO_PATH, PATH_SEPARATOR, packageinfo->packagename, PATH_SEPARATOR);
    //read and process list of files
    if ((src = fopen(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_CONTENT_FILE)), "rb")) != NULL) {
      while (!abort && (line = readline_from_file(src)) != NULL)
        abort = (*filecallback)(packageinfo, line, filecallbackdata);
      fclose(src);
    }
    //clean up
    memory_buffer_free(pkginfopath);
    memory_buffer_free(filepath);
  }
  return abort;
}

void install_db_get_package_info_free (struct install_db_get_package_info_struct* packageinfo)
{
  if (!packageinfo)
    return;
  free(packageinfo->packagename);
  free(packageinfo->version);
  free(packageinfo);
}
