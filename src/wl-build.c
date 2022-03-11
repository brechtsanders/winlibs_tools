#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#else
#ifndef O_BINARY
#define O_BINARY 0
#endif
#endif
#include <miniargv.h>
#include <versioncmp.h>
#include "pkgfile.h"
#include "pkgdb.h"
#include "filesystem.h"
#include "handle_interrupts.h"
#include "build-order.h"
#include "build-package.h"
#include "winlibs_common.h"
/*
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif
#include <portcolcon.h>
*/

#define PROGRAM_NAME    "wl-build"
#define PROGRAM_DESC    ""

//#define DEFAULT_SHELL_COMMAND "sh.exe --login -i"
#define DEFAULT_SHELL_COMMAND "sh.exe --noprofile --norc --noediting -i -v"
#define LOG_FILE_EXTENSION ".log"
#define LOG_FILE_EXTENSION_LEN (sizeof(LOG_FILE_EXTENSION) - 1)
#define ABORT_WAIT_SECONDS 3

DEFINE_INTERRUPT_HANDLER_BEGIN(handle_break_signal)
{
  interrupted++;
}
DEFINE_INTERRUPT_HANDLER_END(handle_break_signal)

////////////////////////////////////////////////////////////////////////

int pkgdb_is_package_installed_callback (const char* basename, void* callbackdata)
{
  struct package_metadata_struct* pkginfo;
  if ((pkginfo = pkgdb_read_package((pkgdb_handle)callbackdata, basename)) == NULL)
    return -1;
  package_metadata_free(pkginfo);
  return 0;
}

////////////////////////////////////////////////////////////////////////

void show_help ()
{
  printf(PROGRAM_NAME " - Version " WINLIBS_VERSION_STRING " - " WINLIBS_LICENSE " - " WINLIBS_CREDITS "\n"
    "Command line utility to build a package from source\n"
    "Usage:  " PROGRAM_NAME " [-s path] [-x command] package[...]\n"
    "        " PROGRAM_NAME " -h\n"
    "Parameters:\n"
/*
    "  -u path       path where to look for build recipes,\n"
*/
    "  -s path       path where to look for build recipes,\n"
    "                overrides environment variable BUILDSCRIPTS\n"
    "  -x command    shell command to execute, defaults to:\n"
    "                  \"" DEFAULT_SHELL_COMMAND "\"\n"
    "  -b path       path temporary build folder will be created\n"
    "  -l path       path where output logs will be saved\n"
    "  -i path       installation path (defaults to $MINGWPREFIX)\n"
    "  -r            remove output log when build was successful\n"
    "  package       package(s) to build, or:\n"
    "                  all         = all packages that can be built\n"
    "                  all-changed = all packages for which the recipe changed\n"
    "  -h            show command line help and exit\n"
    "Environment variables:\n"
    "  BUILDSCRIPTS  path where to look for build recipes\n"
    //"  MINGWPKGINFODIR  path where to look for build recipes\n"
    "  MINGWPREFIX   path where to install package\n"
    "Remarks:\n"
/*
    "  - Either -u or environment variable BUILDSCRIPTS must be set.\n"
*/
    "  - Either -s or environment variable BUILDSCRIPTS must be set.\n"
  );
}

int main (int argc, char** argv, char *envp[])
{
  pkgdb_handle db;
  int showhelp = 0;
  const char* dstdir = NULL;
  const char* packageinfopath = NULL;
  const char* shellcmd = DEFAULT_SHELL_COMMAND;
  const char* builddir = NULL;
  const char* logdir = NULL;
  int removelog = 0;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",         NULL,      miniargv_cb_increment_int, &showhelp,        "show command line help"},
    {'i', "install-path", "PATH",    miniargv_cb_set_const_str, &dstdir,          "path where to install packages\noverrides environment variable MINGWPREFIX"},
    //{'u', "source-path",  "PATH",    miniargv_cb_set_const_str, &packageinfopath, "path containing build recipes\noverrides environment variable BUILDSCRIPTS"},
    {'s', "source-path",  "PATH",    miniargv_cb_set_const_str, &packageinfopath, "path containing build recipes\noverrides environment variable BUILDSCRIPTS"},
    {'x', "shell",        "CMD",     miniargv_cb_set_const_str, &shellcmd,        "shell command to execute, defaults to:\n\"" DEFAULT_SHELL_COMMAND "\""},
    {'b', "build-path",   "PATH",    miniargv_cb_set_const_str, &builddir,        "path temporary build folder will be created"},
    {'l', "logs",         "PATH",    miniargv_cb_set_const_str, &logdir,          "path where output logs will be saved"},
    {'r', "remove-log",   NULL,      miniargv_cb_increment_int, &removelog,       "remove output log when build was successful"},
    {0,   NULL,           "PACKAGE", miniargv_cb_error,         NULL,             "package(s) to build, or:\nall = all packages that can be built\nall-changed = all packages for which the recipe changed"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "MINGWPREFIX",  NULL,      miniargv_cb_set_const_str, &dstdir,          "path where to install packages"},
    {0,   "BUILDSCRIPTS", NULL,      miniargv_cb_set_const_str, &packageinfopath, "path where to look for build recipes"},
    //{0,   "MINGWPKGINFODIR", NULL,      miniargv_cb_set_const_str, &packageinfopath, "path where to look for build recipes"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //parse environment and command line flags
  if (miniargv_process_env(envp, envdef, NULL) != 0)
    return 1;
  if (miniargv_process_arg_flags(argv, argdef, NULL, NULL) != 0)
    return 1;
  //show help if requested or if no command line arguments were given
  if (showhelp) {
    printf(
      PROGRAM_NAME " - Version " WINLIBS_VERSION_STRING " - " WINLIBS_LICENSE " - " WINLIBS_CREDITS "\n"
      "Command line utility to build packages from source\n"
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
    return 1;
  }
  if (!check_packageinfo_paths(packageinfopath)) {
    fprintf(stderr, "Invalid path(s) specified with -s parameter or BUILDSCRIPTS environment variable: %s\n", packageinfopath);
    return 2;
  }
  if (!dstdir || !*dstdir) {
    fprintf(stderr, "Missing -i parameter or MINGWPREFIX environment variable\n");
    return 3;
  }
  if (!folder_exists(dstdir)) {
    fprintf(stderr, "Path does not exist: %s\n", dstdir);
    return 4;
  }
  if (builddir && *builddir) {
    if (!folder_exists(builddir)) {
      fprintf(stderr, "Build path does not exist: %s\n", builddir);
      return 5;
    }
  }
  //open package database
  if ((db = pkgdb_open(dstdir)) == NULL) {
    fprintf(stderr, "No valid package database found for: %s\n", dstdir);
    return 6;
  }

  //install signal handler
  INSTALL_INTERRUPT_HANDLER(handle_break_signal)

  //support ANSI/VT100 control codes in output
#if defined(_WIN32) && defined(ENABLE_VIRTUAL_TERMINAL_PROCESSING)
  SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN);
#endif

  //collect data for supplied packages
  sorted_unique_list* sortedpackagelist;
  struct add_package_and_dependencies_to_list_struct add_package_and_dependencies_to_list_data;
  sortedpackagelist = sorted_unique_list_create(packageinfo_cmp_basename, (sorted_unique_free_fn)package_metadata_free);
  add_package_and_dependencies_to_list_data.packagenamelist = sortedpackagelist;
  add_package_and_dependencies_to_list_data.packageinfopath = packageinfopath;
  {
    int i;
    const char* p;
    const char* q;
    char* s;
    i = 0;
    while (!interrupted && (i = miniargv_get_next_arg_param(i, argv, argdef, NULL)) > 0) {
      //go through each item in comma-separated list
      p = argv[i];
      while (p && *p) {
        //get separate item
        if ((q = strchr(p, ',')) == NULL) {
          s = strdup(p);
        } else {
          s = (char*)malloc(q - p + 1);
          memcpy(s, p, q - p);
          s[q - p] = 0;
        }
        //process item
        if (s && *s) {
          if (strcasecmp(s, "all") == 0) {
            add_package_and_dependencies_to_list_data.filtertype = filter_type_all;
            iterate_packages(packageinfopath, (package_callback_fn)add_package_and_dependencies_to_list, &add_package_and_dependencies_to_list_data);
          } else if (strcasecmp(s, "all-changed") == 0) {
            add_package_and_dependencies_to_list_data.filtertype = filter_type_changed;
            iterate_packages(packageinfopath, (package_callback_fn)add_package_and_dependencies_to_list, &add_package_and_dependencies_to_list_data);
          } else {
            add_package_and_dependencies_to_list_data.filtertype = filter_type_all;
            add_package_and_dependencies_to_list(s, &add_package_and_dependencies_to_list_data);
          }
        }
        //move to next item
        free(s);
        p = (q ? q + 1 : NULL);
      }
    }
  }

  printf("Packages to be built: %lu\n", (unsigned long)sorted_unique_list_size(sortedpackagelist));

  //determine build order
  struct package_info_list_struct* packagebuildlist;
  packagebuildlist = generate_build_list(sortedpackagelist);

  //process package build list
  if (packagebuildlist) {
    char* logfile;
    int skip;
    unsigned long exitcode;
    struct package_info_list_struct* current;
    struct package_metadata_struct* pkginfo;
    struct package_metadata_struct* dbpkginfo;
    while (!interrupted && (current = packagebuildlist) != NULL) {
      skip = 0;
      //check installed version
      dbpkginfo = pkgdb_read_package(db, current->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
      printf("--> %s %s (", current->info->datafield[PACKAGE_METADATA_INDEX_BASENAME], current->info->datafield[PACKAGE_METADATA_INDEX_VERSION]);
      if (!dbpkginfo)
        printf("currently not installed");
      else if (!dbpkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION])
        printf("installed without version information");
      else if (strcmp(dbpkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION], current->info->datafield[PACKAGE_METADATA_INDEX_VERSION]) == 0)
          printf("already installed");
      else
        printf("installed version: %s", dbpkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION]);
      printf(")\n");
      //check latest package information and determine if package should be skipped
      if ((pkginfo = read_packageinfo(packageinfopath, current->info->datafield[PACKAGE_METADATA_INDEX_BASENAME])) == NULL) {
        printf("package information for %s can no longer be found, skipping\n", current->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
        skip++;
      } else {
        //check if package still builds
        if (!pkginfo->buildok) {
          printf("%s is no longer marked as possible to build, skipping\n", current->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
          skip++;
        }
        //check if prerequisites are installed
        if (dstdir && (iterate_packages_in_list(pkginfo->dependencies, pkgdb_is_package_installed_callback, db) != 0 || iterate_packages_in_list(pkginfo->builddependencies, pkgdb_is_package_installed_callback, db) != 0)) {
          printf("missing dependencies, skipping\n");
          skip++;
        }
      }
      //determine if already installed package should be rebuilt
      if (!skip && dstdir && dbpkginfo) {
        if (PKG_XTRA(current->info)->cyclic_start_pkginfo) {
          //part of cyclic loop
          struct package_metadata_struct* cyclic_pkginfo;
          if ((cyclic_pkginfo = read_packageinfo(packageinfopath, PKG_XTRA(current->info)->cyclic_start_pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME])) != NULL) {
            if (!dependancies_listed_but_not_depended_on(dstdir, cyclic_pkginfo))
              skip++;
            package_metadata_free(cyclic_pkginfo);
          }
          if (!skip)
            printf("part of cyclic dependency (via %s), building anyway\n", PKG_XTRA(current->info)->cyclic_start_pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
          else
            skip++;
        } else {
          //otherwise skip rebuild
          skip++;
        }
      }
      //check if rebuild needed because recipe was changed
      if (skip && dstdir && pkginfo && pkginfo->lastchanged) {
        if (PKG_XTRA(current->info)->filtertype == filter_type_changed) {
          time_t install_lastchanged;
          if ((install_lastchanged = installed_package_lastchanged(dstdir, current->info->datafield[PACKAGE_METADATA_INDEX_BASENAME])) != 0 && install_lastchanged < pkginfo->lastchanged) {
            printf("build recipe for %s was changed, rebuilding (installed: %lu, package: %lu)\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], (unsigned long)install_lastchanged, (unsigned long)pkginfo->lastchanged);
            skip = 0;
          }
        }
      }
      //clean up
      if (dbpkginfo)
        package_metadata_free(dbpkginfo);
      if (pkginfo)
        package_metadata_free(pkginfo);
      //build package (unless it should be skipped)
      if (!skip) {
        //determine log file path
        logfile = NULL;
        if (logdir) {
          size_t logdirlen = strlen(logdir);
          size_t basenamelen = strlen(current->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
          if ((logfile = (char*)malloc(logdirlen + basenamelen + LOG_FILE_EXTENSION_LEN + 2)) != NULL) {
            memcpy(logfile, logdir, logdirlen);
            logfile[logdirlen] = PATH_SEPARATOR;
            memcpy(logfile + logdirlen + 1, current->info->datafield[PACKAGE_METADATA_INDEX_BASENAME], basenamelen);
            strcpy(logfile + logdirlen + 1 + basenamelen, LOG_FILE_EXTENSION);
          }
        }
        //build package
        exitcode = build_package(packageinfopath, current->info->datafield[PACKAGE_METADATA_INDEX_BASENAME], shellcmd, logfile, builddir);
        //clean up log file
        if (logfile) {
          if (removelog && exitcode == 0)
            unlink(logfile);
          free(logfile);
        }
      }
      //point to next package
      packagebuildlist = current->next;
      free(current);
      //if Ctrl-C is pressed once give opportunity for second Ctrl-C to abort program
      if (interrupted == 1) {
        printf("\nCtrl-C was pressed. Press Ctrl-C again in the next %i seconds to abort.\n", ABORT_WAIT_SECONDS);
        interrupted = 0;
        SLEEP_SECONDS(ABORT_WAIT_SECONDS);
      }
    }
  }

  //clean up
  pkgdb_close(db);
  sorted_unique_list_free(sortedpackagelist);
  return 0;
}

/////TO DO: option to rebuild touched recipes
/////TO DO: option to build in subfolder (e.g. based on process id) and clean up this folder on failure
/////TO DO: option to (not) abort on failed build
/////TO DO: check why package rhonabwy won't load -> space -> remove spaces from comma separated list

