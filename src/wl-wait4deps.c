#include "winlibs_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <miniargv.h>
#include <portcolcon.h>
#include "filesystem.h"
#include "handle_interrupts.h"

#define PROGRAM_NAME    "wl-wait4deps"
#define PROGRAM_DESC    "Command line utility to wait for package dependencies to be installed"
#define DEFAULT_WAIT_SECONDS 3

#define PACKAGE_INFO_PATH_LEN (sizeof(PACKAGE_INFO_PATH) - 1)
#define PACKAGE_INFO_VERSION_FILE_LEN (sizeof(PACKAGE_INFO_VERSION_FILE) - 1)

#define STRINGIZE_(value) #value
#define STRINGIZE(value) STRINGIZE_(value)

int is_installed (const char* packagename, size_t packagenamelen, const char* basepath)
{
  char* path;
  size_t basepathlen = strlen(basepath);
  int result = 0;
  if ((path = (char*)malloc(basepathlen + PACKAGE_INFO_PATH_LEN + 1 + packagenamelen + 1 + PACKAGE_INFO_VERSION_FILE_LEN + 1)) != NULL) {
    memcpy(path, basepath, basepathlen);
    memcpy(path + basepathlen, PACKAGE_INFO_PATH, PACKAGE_INFO_PATH_LEN);
    path[basepathlen + PACKAGE_INFO_PATH_LEN] = PATH_SEPARATOR;
    memcpy(path + basepathlen + PACKAGE_INFO_PATH_LEN + 1, packagename, packagenamelen);
    path[basepathlen + PACKAGE_INFO_PATH_LEN + 1 + packagenamelen] = PATH_SEPARATOR;
    memcpy(path + basepathlen + PACKAGE_INFO_PATH_LEN + 1 + packagenamelen + 1, PACKAGE_INFO_VERSION_FILE, PACKAGE_INFO_VERSION_FILE_LEN);
    path[basepathlen + PACKAGE_INFO_PATH_LEN + 1 + packagenamelen + 1 + PACKAGE_INFO_VERSION_FILE_LEN] = 0;
    result = file_exists(path);
    free(path);
  }
  return result;
}

typedef int (*iterate_list_callback_fn) (const char* item, size_t itemlen, void* callbackdata);

size_t iterate_list (const char* list, iterate_list_callback_fn callbackfn, void* callbackdata)
{
  const char* p;
  const char* q;
  size_t count = 0;
  if (!list)
    return 0;
  p = list;
  while (*p) {
    //skip leading comma's
    while (*p == ',')
      p++;
    //find next comma
    q = p;
    while (*q && *q != ',')
      q++;
    //process item
    if (q != p) {
      count++;
      if ((*callbackfn)(p, q - p, callbackdata) != 0)
        break;
    }
    //move pointer after current item for next iteration
    p = q;
  }
  return count;
}

struct list_dependency_callbackdata_struct {
  portcolconhandle con;
  const char* basepath;
  size_t countmissing;
  char itemfoundbullet;
  char itemmissingbullet;
  int isoptional;
};

int list_dependency (const char* item, size_t itemlen, void* callbackdata)
{
  struct list_dependency_callbackdata_struct* data = (struct list_dependency_callbackdata_struct*)callbackdata;
  if (is_installed(item, itemlen, data->basepath)) {
    portcolcon_set_foreground(data->con, (!data->isoptional ? PORTCOLCON_COLOR_GREEN : PORTCOLCON_COLOR_CYAN));
    portcolcon_printf(data->con, " %c ", data->itemfoundbullet);
  } else {
    if (!data->isoptional)
      data->countmissing++;
    portcolcon_set_foreground(data->con, (!data->isoptional ? PORTCOLCON_COLOR_RED : PORTCOLCON_COLOR_BROWN));
    portcolcon_printf(data->con, " %c ", data->itemmissingbullet);
  }
  if (!data->isoptional) {
    portcolcon_reset_color(data->con);
    portcolcon_printf(data->con, "%.*s", (int)itemlen, item);
  } else {
    portcolcon_set_foreground(data->con, PORTCOLCON_COLOR_DARK_GRAY);
    portcolcon_printf(data->con, "(%.*s)", (int)itemlen, item);
    portcolcon_reset_color(data->con);
  }
  portcolcon_printf(data->con, "\n");
  return 0;
}

int list_missing_dependency (const char* item, size_t itemlen, void* callbackdata)
{
  struct list_dependency_callbackdata_struct* data = (struct list_dependency_callbackdata_struct*)callbackdata;
  if (!is_installed(item, itemlen, data->basepath)) {
    if (data->countmissing++)
      portcolcon_write(data->con, ",");
    portcolcon_set_foreground(data->con, PORTCOLCON_COLOR_WHITE);
    portcolcon_printf(data->con, "%.*s", (int)itemlen, item);
    portcolcon_reset_color(data->con);
  }
  return 0;
}

int count_missing_dependency (const char* item, size_t itemlen, void* callbackdata)
{
  struct list_dependency_callbackdata_struct* data = (struct list_dependency_callbackdata_struct*)callbackdata;
  if (!data->isoptional) {
    if (!is_installed(item, itemlen, data->basepath)) {
      data->countmissing++;
    }
  }
  return 0;
}

int interrupted = 0;

DEFINE_INTERRUPT_HANDLER_BEGIN(handle_break_signal)
{
  interrupted++;
}
DEFINE_INTERRUPT_HANDLER_END(handle_break_signal)

int main (int argc, char *argv[], char *envp[])
{
  int i;
  portcolconhandle con;
  struct list_dependency_callbackdata_struct list_dependency_data;
  const char* basename = NULL;
  const char* basepath = NULL;
  const char* dependencies = NULL;
  const char* optionaldependencies = NULL;
  const char* builddependencies = NULL;
  const char* optionalbuilddependencies = NULL;
  int waitdelay = DEFAULT_WAIT_SECONDS;
  int showversion = 0;
  int showhelp = 0;
  int verbose = 0;
  size_t countmissing = 0;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",                       NULL,      miniargv_cb_increment_int, &showhelp,                  "show command line help", NULL},
    {0,   "version",                     NULL,     miniargv_cb_increment_int, &showversion,               "show version information", NULL},
    {'n', "name",                       "NAME",    miniargv_cb_set_const_str, &basename,                  "name of project to check dependencies for (optional)\noverrides environment variable BASENAME", NULL},
    //{'m', "install-path",               "PATH",    miniargv_cb_set_const_str, &basepath,                  "path where to look for already installed packages\noverrides environment variable MINGWPREFIX", NULL},
    {'i', "install-path",               "PATH",    miniargv_cb_set_const_str, &basepath,                  "path where packages are installed\noverrides environment variable MINGWPREFIX", NULL},
    {'d', "dependencies",               "LIST",    miniargv_cb_set_const_str, &dependencies,              "comma separated list of target dependencies\noverrides environment variable DEPENDANCIES", NULL},
    {'o', "optional-dependencies",      "LIST",    miniargv_cb_set_const_str, &optionaldependencies,      "comma separated list of optional target dependencies\noverrides environment variable OPTIONALDEPENDANCIES", NULL},
    {'b', "build-dependencies",         "LIST",    miniargv_cb_set_const_str, &builddependencies,         "comma separated list of build dependencies\noverrides environment variable BUILDDEPENDANCIES", NULL},
    {'w', "wait",                       "SECONDS", miniargv_cb_set_int,       &waitdelay,                 "seconds to wait between checks (default: " STRINGIZE(DEFAULT_WAIT_SECONDS) "s)", NULL},
    {'q', NULL,                         NULL,      miniargv_cb_set_int_to_minus_one, &verbose,            "quiet mode (minimal output)", NULL},
    {'v', "verbose",                    NULL,      miniargv_cb_increment_int,        &verbose,            "verbose mode", NULL},
    MINIARGV_DEFINITION_END
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "MINGWPREFIX",               NULL,       miniargv_cb_set_const_str, &basepath,                  "path where packages are installed", NULL},
    {0,   "BASENAME",                  NULL,       miniargv_cb_set_const_str, &basename,                  "name of project to check dependencies for", NULL},
    //{0,   "DEPENDANCIES",              NULL,       miniargv_cb_set_const_str, &dependencies,              "comma separated list of target dependencies", NULL},
    {0,   "DEPENDENCIES",              NULL,       miniargv_cb_set_const_str, &dependencies,              "comma separated list of target dependencies", NULL},
    //{0,   "OPTIONALDEPENDANCIES",      NULL,       miniargv_cb_set_const_str, &optionaldependencies,      "comma separated list of optional target dependencies", NULL},
    {0,   "OPTIONALDEPENDENCIES",      NULL,       miniargv_cb_set_const_str, &optionaldependencies,      "comma separated list of optional target dependencies", NULL},
    //{0,   "BUILDDEPENDANCIES",         NULL,       miniargv_cb_set_const_str, &builddependencies,         "comma separated list of build dependencies", NULL},
    {0,   "BUILDDEPENDENCIES",         NULL,       miniargv_cb_set_const_str, &builddependencies,         "comma separated list of build dependencies", NULL},
    //{0,   "OPTIONALBUILDDEPENDANCIES", NULL,       miniargv_cb_set_const_str, &optionalbuilddependencies, "comma separated list of optional build dependencies", NULL},
    {0,   "OPTIONALBUILDDEPENDENCIES", NULL,       miniargv_cb_set_const_str, &optionalbuilddependencies, "comma separated list of optional build dependencies", NULL},
    MINIARGV_DEFINITION_END
  };
  //parse environment and command line flags
  if (miniargv_process(argv, envp, argdef, envdef, NULL, NULL) != 0)
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
  if (!basepath || !*basepath) {
    fprintf(stderr, "Missing -i parameter or MINGWPREFIX environment variable\n");
    return 2;
  }
  if (!folder_exists(basepath)) {
    fprintf(stderr, "Path does not exist: %s\n", basepath);
    return 3;
  }
  //install signal handler
  INSTALL_INTERRUPT_HANDLER(handle_break_signal)
  //initialize color console library
  con = portcolcon_initialize();
  //show information
  if (verbose > 0) {
    portcolcon_printf(con,
      PROGRAM_NAME " v" WINLIBS_VERSION_STRING "\n"
      "Project name:         %s\n"
      "Install path:         %s\n"
      "Dependancies:         %s\n"
      "Build dependencies:   %s\n"
      "Delay between checks: %is\n"
      , basename, basepath, dependencies, builddependencies, waitdelay);
  }
  //list dependencies
  portcolcon_set_title(con, "Checking dependencies");
  portcolcon_write(con, "Checking dependencies");
  if (basename && *basename) {
    portcolcon_write(con, " for ");
    portcolcon_set_foreground(con, PORTCOLCON_COLOR_YELLOW);
    portcolcon_write(con, basename);
    portcolcon_reset_color(con);
  }
  portcolcon_write(con, "\n");
  list_dependency_data.con = con;
  list_dependency_data.basepath = basepath;
  list_dependency_data.countmissing = 0;
  list_dependency_data.itemmissingbullet = '-';
  list_dependency_data.itemfoundbullet = '+';
  list_dependency_data.isoptional = 0;
  iterate_list(dependencies, (verbose >= 0 ? list_dependency : count_missing_dependency), &list_dependency_data);
  list_dependency_data.isoptional = 1;
  iterate_list(optionaldependencies, (verbose >= 0 ? list_dependency : count_missing_dependency), &list_dependency_data);
  list_dependency_data.itemmissingbullet = 'x';
  list_dependency_data.itemfoundbullet = '*';
  list_dependency_data.isoptional = 0;
  iterate_list(builddependencies, (verbose >= 0 ? list_dependency : count_missing_dependency), &list_dependency_data);
  portcolcon_reset_color(con);
  //keep checking for dependencies until there are no more missing dependencies
  while (!interrupted && list_dependency_data.countmissing) {
    if (list_dependency_data.countmissing != countmissing) {
      countmissing = list_dependency_data.countmissing;
      portcolcon_set_title(con, "Waiting for missing dependencies");
      portcolcon_printf(con, "Waiting for missing dependencies: ");
      list_dependency_data.countmissing = 0;
      iterate_list(dependencies, list_missing_dependency, &list_dependency_data);
      iterate_list(builddependencies, list_missing_dependency, &list_dependency_data);
      portcolcon_write(con, "\n");
    } else {
      list_dependency_data.countmissing = 0;
      iterate_list(dependencies, count_missing_dependency, &list_dependency_data);
      iterate_list(builddependencies, count_missing_dependency, &list_dependency_data);
    }
    fflush(stdout);
    //sleep
    if (verbose > 0) {
      for (i = waitdelay; i > 0; i--) {
        portcolcon_printf(con, "\r%i missing, waiting %is", (int)list_dependency_data.countmissing, i);
        fflush(stdout);
        SLEEP_SECONDS(1);
      }
      portcolcon_printf(con, "\r%-*s\r", 30, "");
    } else {
      SLEEP_SECONDS(waitdelay);
    }
  }
  portcolcon_set_title(con, "Dependancies found");
  //clean up color console library
  portcolcon_cleanup(con);
  return 0;
}

/////TO DO: hook into sqlite3 database to get notified of changes
