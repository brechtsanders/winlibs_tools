#include "winlibs_common.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <miniargv.h>
#include "memory_buffer.h"
#include "filesystem.h"
#include "pkgdb.h"

#define PROGRAM_NAME    "wl-uninstall"
#define PROGRAM_DESC    "Command line utility to uninstall packages"

#if defined(_WIN32) && !defined(__MINGW64_VERSION_MAJOR)
#define strcasecmp _stricmp
#endif

void delete_file_verbose (const char* filepath, int verbose)
{
  if (verbose >= 2)
    printf("Deleting file: %s\n", filepath);
  if (unlink(filepath) != 0 && verbose) {
    fprintf(stderr, "Error deleting file: %s\n", filepath);
  }
}

int package_file_iteration_delete (pkgdb_handle handle, const char* path, void* callbackdata)
{
  struct memory_buffer* filepath = memory_buffer_create();
  delete_file_verbose(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%c%s", pkgdb_get_rootpath(handle), PATH_SEPARATOR, path)), *(int*)callbackdata);
  memory_buffer_free(filepath);
  return 0;
}

int main (int argc, char** argv, char *envp[])
{
  int i;
  pkgdb_handle db;
  int status;
  int showhelp = 0;
  int verbose = 0;
  const char* basepath = NULL;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",         NULL,      miniargv_cb_increment_int, &showhelp,        "show command line help", NULL},
    {'i', "install-path", "PATH",    miniargv_cb_set_const_str, &basepath,        "package installation path\noverrides environment variable MINGWPREFIX", NULL},
    {'v', "verbose",      NULL,      miniargv_cb_increment_int, &verbose,         "verbose mode", NULL},
    {0,   NULL,           "PACKAGE", miniargv_cb_error,         NULL,             "package(s) to uninstall", NULL},
    MINIARGV_DEFINITION_END
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "MINGWPREFIX",  NULL,      miniargv_cb_set_const_str, &basepath,        "package installation path", NULL},
    MINIARGV_DEFINITION_END
  };
  //parse environment and command line flags
  if (miniargv_process_env(envp, envdef, NULL) != 0)
    return 1;
  if (miniargv_process_arg_flags(argv, argdef, NULL, NULL) != 0)
    return 1;
  //show help if requested or if no command line arguments were given
  if (showhelp || argc <= 1) {
    printf(
      PROGRAM_NAME " - Version " WINLIBS_VERSION_STRING " - " WINLIBS_LICENSE " - " WINLIBS_CREDITS "\n"
      PROGRAM_DESC "\n"
      "Usage: " PROGRAM_NAME " "
    );
    miniargv_arg_list(argdef, 1);
    printf("\n");
    miniargv_help(argdef, envdef, 24, 0);
#ifdef PORTCOLCON_VERSION
    printf(WINLIBS_HELP_COLOR);
#endif
    return 0;
  }
  //check parameters
  if (!basepath || !*basepath) {
    fprintf(stderr, "Missing -i parameter or MINGWPREFIX environment variable\n");
    return 2;
  }
  if (!folder_exists(basepath)) {
    fprintf(stderr, "Path does not exist: %s\n", basepath);
    return 3;
  }
  if (miniargv_get_next_arg_param(0, argv, argdef, NULL) <= 0) {
    fprintf(stderr, "Missing package name(s)\n");
    return 4;
  }
/*
#ifdef __MINGW32__
  {
    char s[_MAX_PATH];
    if (!_fullpath(s, basepath,_MAX_PATH)) {
      fprintf(stderr, "Unable to get real path for package destination directory\n");
      return 3;
    }
    basepath = strdup(s);
  }
#endif
*/
  if ((db = pkgdb_open(basepath)) == NULL) {
    fprintf(stderr, "No valid package database found for: %s\n", basepath);
    return 3;
  }
  //process command line argument values
  i = 0;
  while ((i = miniargv_get_next_arg_param(i, argv, argdef, NULL)) > 0) {
    struct package_metadata_struct* installedpkginfo;
    struct memory_buffer* pkginfopath = memory_buffer_create();
    struct memory_buffer* filepath = memory_buffer_create();
    //get information about already installed package
    if ((installedpkginfo = pkgdb_read_package(db, argv[i])) == NULL) {
      fprintf(stderr, "Package not found: %s\n", argv[i]);
      continue;
    }

    //show information
    printf("Uninstalling %s version %s\n", installedpkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], installedpkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION]);
    if (verbose) {
      printf("Destination: %s\n", basepath);
    }

    //determine package information path
    memory_buffer_set_printf(pkginfopath, "%s%s%c%s%c", basepath, PACKAGE_INFO_PATH, PATH_SEPARATOR, installedpkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], PATH_SEPARATOR);
    //delete version file
    delete_file_verbose(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_VERSION_FILE)), verbose);
    //delete file list
    delete_file_verbose(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_CONTENT_FILE)), verbose);
    //delete folder list
    delete_file_verbose(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_FOLDERS_FILE)), verbose);
    //delete dependency files
    delete_file_verbose(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_DEPENDENCIES_FILE)), verbose);
    delete_file_verbose(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_OPTIONALDEPENDENCIES_FILE)), verbose);
    delete_file_verbose(memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_BUILDDEPENDENCIES_FILE)), verbose);
    //delete package information folder
    rmdir(memory_buffer_get(pkginfopath));

    //delete files
    pkgdb_interate_package_files(db, installedpkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], package_file_iteration_delete, &verbose);
    //delete from package database
    if ((status = pkgdb_uninstall_package(db, installedpkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME])) != 0) {
      fprintf(stderr, "Error removing package %s from package database in: %s\n", installedpkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], basepath);
    }

    //show information
    printf("Finished uninstalling %s %s\n", installedpkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], installedpkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION]);

    //clean up
    memory_buffer_free(pkginfopath);
    memory_buffer_free(filepath);
    package_metadata_free(installedpkginfo);
  }
  //clean up
  pkgdb_close(db);
  return 0;
}
/////TO DO: delete folders not used by any other package (what if they are not empty?)
