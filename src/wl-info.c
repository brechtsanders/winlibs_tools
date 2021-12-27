#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <miniargv.h>
#include <versioncmp.h>
#include "winlibs_common.h"
#include "package_info.h"
#include "sorted_unique_list.h"
#include "filesystem.h"

#define PROGRAM_NAME    "wl-info"
#define PROGRAM_DESC    "Command line utility to display package recipe information"

int packageinfo_show (const char* basename, void* callbackdata)
{
  struct package_info_struct* pkginfo;
  const char* packageinfopath = (const char*)callbackdata;
  if ((pkginfo = read_packageinfo(packageinfopath, basename)) == NULL) {
    fprintf(stderr, "Error reading package information for: %s\n", basename);
  } else {
    printf("[%s]\n", pkginfo->basename);
    if (!pkginfo->basename || strcmp(basename, pkginfo->basename) != 0) {
      fprintf(stderr, "Name mismatch for package %s (name in file: %s)\n", basename, (pkginfo->basename ? pkginfo->basename : "NULL"));
    }
    printf("basename=%s\n", pkginfo->basename);
    printf("version=%s\n", pkginfo->version);
    printf("versiondate=%s\n", pkginfo->versiondate);
    printf("name=%s\n", pkginfo->name);
    printf("description=%s\n", pkginfo->description);
    printf("url=%s\n", pkginfo->url);
    printf("category=%s\n", pkginfo->category);
    printf("type=%s\n", pkginfo->type);
    printf("builds=%s\n", (pkginfo->buildok ? "yes" : "no"));
    printf("status=%s\n", pkginfo->status);
    printf("dependencies=%s\n", pkginfo->dependencies);
    printf("optionaldependencies=%s\n", pkginfo->optionaldependencies);
    printf("builddependencies=%s\n", pkginfo->builddependencies);
    printf("licensefile=%s\n", pkginfo->licensefile);
    printf("licensetype=%s\n", pkginfo->licensetype);
    printf("downloadurldata=%s\n", pkginfo->downloadurldata);
    //printf("downloadsourceurl=%s\n", pkginfo->downloadsourceurl);
    //pkginfo->time_t lastchanged;
    printf("\n");
    free_packageinfo(pkginfo);
  }
  return 0;
}

int main (int argc, char *argv[], char *envp[])
{
  int i;
  int showhelp = 0;
  int verbose = 0;
  const char* packageinfopath = NULL;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",          NULL,      miniargv_cb_increment_int, &showhelp,        "show command line help"},
    {'s', "source-path",   "PATH",    miniargv_cb_set_const_str, &packageinfopath, "path containing build recipes\noverrides environment variable BUILDSCRIPTS"},
    {'v', "verbose",       NULL,      miniargv_cb_increment_int, &verbose,         "verbose mode"},
    //{0,   NULL,            "PACKAGE", process_arg_param,         NULL,             "package(s) to build (or \"all\" to list all packages)"},
    {0,   NULL,            "PACKAGE", miniargv_cb_error,         NULL,             "package(s) to build (or \"all\" to list all packages)"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "BUILDSCRIPTS",  NULL,       miniargv_cb_set_const_str, &packageinfopath, "path where to look for build recipes"},
    //{0,   "MINGWPKGINFODIR", NULL,     miniargv_cb_set_const_str, &packageinfopath, "path where to look for build recipes"},
    {0, NULL, NULL, NULL, NULL, NULL}
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
