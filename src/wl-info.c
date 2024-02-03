#include "winlibs_common.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <miniargv.h>
#include <versioncmp.h>
#include "pkgfile.h"
#include "sorted_unique_list.h"
#include "filesystem.h"

#define PROGRAM_NAME    "wl-info"
#define PROGRAM_DESC    "Command line utility to display package recipe information"

int packageinfo_show (const char* basename, void* callbackdata)
{
  struct package_metadata_struct* pkginfo;
  const char* packageinfopath = (const char*)callbackdata;
  if ((pkginfo = read_packageinfo(packageinfopath, basename)) == NULL) {
    fprintf(stderr, "Error reading package information for: %s\n", basename);
  } else {
    printf("[%s]\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
    if (!pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME] || strcmp(basename, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME]) != 0) {
      fprintf(stderr, "Name mismatch for package %s (name in file: %s)\n", basename, (pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME] ? pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME] : "NULL"));
    }
    printf("basename=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
    printf("version=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION]);
    printf("versiondate=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION]);
    printf("name=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_NAME]);
    printf("description=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_DESCRIPTION]);
    printf("url=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_URL]);
    printf("category=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_CATEGORY]);
    printf("type=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_CATEGORY]);
    printf("builds=%s\n", (pkginfo->buildok ? "yes" : "no"));
    printf("status=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_STATUS]);
    printf("dependencies=");
    sorted_unique_list_print(pkginfo->dependencies, ",");
    printf("\n");
    printf("optionaldependencies=");
    sorted_unique_list_print(pkginfo->optionaldependencies, ",");
    printf("\n");
    printf("builddependencies=");
    sorted_unique_list_print(pkginfo->builddependencies, ",");
    printf("\n");
    printf("licensefile=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_LICENSEFILE]);
    printf("licensetype=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_LICENSETYPE]);
    printf("downloadurldata=%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_DOWNLOADURL]);
    //printf("downloadsourceurl=%s\n", pkginfo->downloadsourceurl);
    //pkginfo->time_t lastchanged;
    printf("\n");
    package_metadata_free(pkginfo);
  }
  return 0;
}

int main (int argc, char *argv[], char *envp[])
{
  int i;
  int showversion = 0;
  int showhelp = 0;
  int verbose = 0;
  const char* packageinfopath = NULL;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",          NULL,      miniargv_cb_increment_int, &showhelp,        "show command line help", NULL},
    {0,   "version",       NULL,      miniargv_cb_increment_int, &showversion,     "show version information", NULL},
    {'s', "source-path",   "PATH",    miniargv_cb_set_const_str, &packageinfopath, "path containing build recipes\noverrides environment variable BUILDSCRIPTS\ncan be multiple paths separated by \"" WINLIBS_CHR2STR(PATHLIST_SEPARATOR) "\"", NULL},
    {'v', "verbose",       NULL,      miniargv_cb_increment_int, &verbose,         "verbose mode", NULL},
    //{0,   NULL,            "PACKAGE", process_arg_param,         NULL,             "package(s) to build (or \"all\" to list all packages)", NULL},
    {0,   NULL,            "PACKAGE", miniargv_cb_error,         NULL,             "package(s) to build (or \"all\" to list all packages)", NULL},
    MINIARGV_DEFINITION_END
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "BUILDSCRIPTS",  NULL,       miniargv_cb_set_const_str, &packageinfopath, "path where to look for build recipes", NULL},
    //{0,   "MINGWPKGINFODIR", NULL,     miniargv_cb_set_const_str, &packageinfopath, "path where to look for build recipes", NULL},
    MINIARGV_DEFINITION_END
  };
  //parse environment and command line flags
  if (miniargv_process_env(envp, envdef, NULL, NULL) != 0)
    return 1;
  if (miniargv_process_arg_flags(argv, argdef, NULL, NULL) != 0)
    return 1;
  //show help if requested or if no command line arguments were given
  if (showhelp || argc <= 1) {
    printf(
      PROGRAM_NAME " - version " WINLIBS_VERSION_STRING " - " WINLIBS_LICENSE " - " WINLIBS_CREDITS "\n"
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
  //show version information if requested
  if (showversion) {
    printf(PROGRAM_NAME " - version " WINLIBS_VERSION_STRING " - " WINLIBS_LICENSE " - " WINLIBS_CREDITS "\n");
    return 0;
  }
  //check parameters
  if (miniargv_get_next_arg_param(0, argv, argdef, NULL) <= 0) {
    fprintf(stderr, "No packages specified\n");
    return 2;
  }
  if (!packageinfopath || !*packageinfopath) {
    fprintf(stderr, "Missing -s parameter or BUILDSCRIPTS environment variable\n");
    return 2;
  }
  if (!check_packageinfo_paths(packageinfopath)) {
    fprintf(stderr, "Invalid path(s) specified with -s parameter or BUILDSCRIPTS environment variable: %s\n", packageinfopath);
    return 3;
  }
  //process command line argument values
  i = 0;
  while ((i = miniargv_get_next_arg_param(i, argv, argdef, NULL)) > 0) {
    if (strcasecmp(argv[i], "all") == 0) {
      iterate_packages(packageinfopath, (package_callback_fn)packageinfo_show, (void*)packageinfopath);
    } else {
      packageinfo_show(argv[i], (void*)packageinfopath);
    }
  }
  return 0;
}
