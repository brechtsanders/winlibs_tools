#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <miniargv.h>
#include <versioncmp.h>
#include "winlibs_common.h"
#include "sorted_unique_list.h"
#include "filesystem.h"
#include "pkgfile.h"

#define PROGRAM_NAME    "wl-listall"
#define PROGRAM_DESC    "Command line utility to list available package recipes"

int packageinfo_callback (const char* basename, void* callbackdata)
{
  sorted_unique_list_add((sorted_unique_list*)callbackdata, basename);
  return 0;
}

int main (int argc, char *argv[], char *envp[])
{
  size_t i;
  size_t n;
  sorted_unique_list* packagelist;
  struct package_metadata_struct* pkginfo;
  const char* basename;
  size_t totalbuilding = 0;
  size_t totalproblems = 0;
  int showhelp = 0;
  int verbose = 1;
  const char* packageinfopath = NULL;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",          NULL,   miniargv_cb_increment_int,   &showhelp,        "show command line help", NULL},
    {'s', "source-path",   "PATH", miniargv_cb_set_const_str,   &packageinfopath, "build recipe path\noverrides environment variable BUILDSCRIPTS\ncan be multiple paths separated by \"" WINLIBS_CHR2STR(PATHLIST_SEPARATOR) "\"", NULL},
    //{'v', "verbose",       NULL,   miniargv_cb_increment_int,   &verbose,         "verbose mode", NULL},
    {'q', "quiet",         NULL,   miniargv_cb_set_int_to_zero, &verbose,         "quiet mode", NULL},
    MINIARGV_DEFINITION_END
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "BUILDSCRIPTS",  NULL,   miniargv_cb_set_const_str,   &packageinfopath, "build recipe path", NULL},
    MINIARGV_DEFINITION_END
  };
  //parse environment and command line flags
  if (miniargv_process(argv, envp, argdef, envdef, NULL, NULL) != 0)
    return 1;
  //show help if requested
  if (showhelp) {
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
  if (!packageinfopath || !*packageinfopath) {
    fprintf(stderr, "Missing -s parameter or BUILDSCRIPTS environment variable\n");
    return 2;
  }
  if (!check_packageinfo_paths(packageinfopath)) {
    fprintf(stderr, "Invalid path(s) specified with -s parameter or BUILDSCRIPTS environment variable: %s\n", packageinfopath);
    return 3;
  }
  //read all package info
  if ((packagelist = sorted_unique_list_create(versioncmp, free)) == NULL) {
    fprintf(stderr, "Memory allocation error in sorted_unique_list_create()\n");
    return 1;
  }
  iterate_packages(packageinfopath, packageinfo_callback, packagelist);
  //show all package info
  n = sorted_unique_list_size(packagelist);
  for (i = 0; i < n; i++) {
    basename = sorted_unique_list_get(packagelist, i);
    if ((pkginfo = read_packageinfo(packageinfopath, basename)) == NULL) {
      fprintf(stderr, "Error reading package information for: %s\n", basename);
      totalproblems++;
    } else {
      if (!pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME] || strcmp(basename, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME]) != 0) {
        fprintf(stderr, "Name mismatch for package %s (name in file: %s)\n", basename, (pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME] ? pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME] : "NULL"));
        totalproblems++;
      }
      if (verbose)
        printf("%s %s%s\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], pkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION], (pkginfo->buildok ? "" : " (not building)"));
      if (pkginfo->buildok)
        totalbuilding++;
      package_metadata_free(pkginfo);
    }
  }
  //show summary
  printf("Total packages: %lu\n", (unsigned long)n);
  printf("Total packages than can be built: %lu\n", (unsigned long)totalbuilding);
  printf("Total packages with package information problems: %lu\n", (unsigned long)totalproblems);
  //clean up
  sorted_unique_list_free(packagelist);
  return 0;
}
