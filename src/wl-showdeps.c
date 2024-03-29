#include "winlibs_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <miniargv.h>
#include "pkgfile.h"
#include "sorted_unique_list.h"
#include "filesystem.h"

#define PROGRAM_NAME    "wl-showdeps"
#define PROGRAM_DESC    "Command line utility to display package dependency tree"
#define DEPENDANCY_BULLET_MANDATORY     '+'
#define DEPENDANCY_BULLET_OPTIONAL      '-'
#define DEPENDANCY_BULLET_BUILD         '*'

void show_package_item (const char* packagename, int level, char bullet)
{
  printf("%*s%c %s\n", level * 2, "", bullet, packagename);
}

struct package_list_struct {
  const char* packagename;
  struct package_list_struct* next;
};

void show_package_dependencies (const char* packagename, const char* basepath, const char* packageinfopath, int recursive, int level, char bullet, struct package_list_struct* parent)
{
  struct package_metadata_struct* pkginfo;
  struct package_list_struct* listitem;
  //show information
  show_package_item(packagename, level, bullet);
  //check for circular dependency
  listitem = parent;
  while (listitem) {
    if (strcmp(packagename, listitem->packagename) == 0) {
      printf("%*s^ (circular dependency)\n", (level + 1) * 2, "");
      return;
    }
    listitem = listitem->next;
  }
  //get package details and show details
  if ((pkginfo = read_packageinfo(packageinfopath, packagename)) == NULL) {
    fprintf(stderr, "Error reading package information for: %s\n", packagename);
  } else {
    unsigned int i;
    unsigned int n;
    struct package_list_struct current = {packagename, parent};
    //process depenancies
    n = sorted_unique_list_size(pkginfo->dependencies);
    for (i = 0; i < n; i++) {
      if (!recursive) {
        show_package_item(sorted_unique_list_get(pkginfo->dependencies, i), level + 1, DEPENDANCY_BULLET_MANDATORY);
      } else {
        show_package_dependencies(sorted_unique_list_get(pkginfo->dependencies, i), basepath, packageinfopath, recursive, level + 1, DEPENDANCY_BULLET_MANDATORY, &current);
      }
    }
    //process optional depenancies
    n = sorted_unique_list_size(pkginfo->optionaldependencies);
    for (i = 0; i < n; i++) {
      if (!recursive) {
        show_package_item(sorted_unique_list_get(pkginfo->optionaldependencies, i), level + 1, DEPENDANCY_BULLET_OPTIONAL);
      } else {
        show_package_dependencies(sorted_unique_list_get(pkginfo->optionaldependencies, i), basepath, packageinfopath, recursive, level + 1, DEPENDANCY_BULLET_OPTIONAL, &current);
      }
    }
    //process build depenancies
    n = sorted_unique_list_size(pkginfo->builddependencies);
    for (i = 0; i < n; i++) {
      if (!recursive) {
        show_package_item(sorted_unique_list_get(pkginfo->builddependencies, i), level + 1, DEPENDANCY_BULLET_BUILD);
      } else {
        show_package_dependencies(sorted_unique_list_get(pkginfo->builddependencies, i), basepath, packageinfopath, recursive, level + 1, DEPENDANCY_BULLET_BUILD, &current);
      }
    }
    //clean up
    package_metadata_free(pkginfo);
  }
}

int main (int argc, char *argv[], char *envp[])
{
  int i;
  int showversion = 0;
  int showhelp = 0;
  const char* basepath = NULL;
  const char* packageinfopath = NULL;
  int recursive = 0;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",         NULL,      miniargv_cb_increment_int, &showhelp,        "show command line help", NULL},
    {0,   "version",         NULL,   miniargv_cb_increment_int, &showversion,     "show version information", NULL},
    //{'m', "install-path", "PATH",    miniargv_cb_set_const_str, &basepath,        "path where to look for already installed packages\noverrides environment variable MINGWPREFIX", NULL},
    {'i', "install-path", "PATH",    miniargv_cb_set_const_str, &basepath,        "path where packages are installed\noverrides environment variable MINGWPREFIX", NULL},
    {'s', "source-path",  "PATH",    miniargv_cb_set_const_str, &packageinfopath, "path containing build recipes\noverrides environment variable BUILDSCRIPTS\ncan be multiple paths separated by \"" WINLIBS_CHR2STR(PATHLIST_SEPARATOR) "\"", NULL},
    {'r', "recursive",    NULL,      miniargv_cb_increment_int, &recursive,       "recursive (list dependencies of dependences and so on)", NULL},
    {0,   NULL,           "PACKAGE", miniargv_cb_error,         NULL,              "name(s) of package(s) to list dependencies for", NULL},
    MINIARGV_DEFINITION_END
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "MINGWPREFIX",  NULL,      miniargv_cb_set_const_str, &basepath,        "path where packages are installed", NULL},
    {0,   "BUILDSCRIPTS", NULL,      miniargv_cb_set_const_str, &packageinfopath, "path where to look for build recipes", NULL},
    MINIARGV_DEFINITION_END
  };
  //parse environment and command line flags
  if (miniargv_process_env(envp, envdef, NULL, NULL) != 0)
    return 1;
  if (miniargv_process_arg_flags(argv, argdef, NULL, NULL) != 0)
    return 1;
  //show help if requested or if no command line arguments were given
  if (showhelp) {
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
    show_package_dependencies(argv[i], basepath, packageinfopath, recursive, 0, DEPENDANCY_BULLET_MANDATORY, NULL);
  }
  return 0;
}
