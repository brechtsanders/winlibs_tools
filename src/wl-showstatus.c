#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif
#include <miniargv.h>
#include <portcolcon.h>
#include "winlibs_common.h"

#define PROGRAM_NAME    "wl-showstatus"
#define PROGRAM_DESC    "Command line utility to show status of the winlibs package build process"

void allocstr_append (char** data, char separator, const char* text)
{
  size_t datalen;
  char* p;
  int dataisnull = (*data == NULL);
  if (!text)
    return;
  datalen = (dataisnull ? 0 : strlen(*data));
  if ((*data = (char*)realloc(*data, datalen + (!dataisnull && separator ? 1 : 0) + strlen(text) + 1)) == NULL)
    return;
  p = *data + datalen;
  if (!dataisnull && separator)
    *(p++) = separator;
  strcpy(p, text);
}

int process_arg_append_str (const miniargv_definition* argdef, const char* value, void* callbackdata)
{
  allocstr_append((char**)argdef->userdata, ' ', value);
  return 0;
}

int main (int argc, char *argv[], char *envp[])
{
  char* s;
  int showhelp = 0;
  int showpackageversion = 0;
  const char* basename = NULL;
  const char* version = NULL;
  char* title = NULL;
  char* message = NULL;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",            NULL,   miniargv_cb_increment_int, &showhelp,           "show command line help"},
    {'v', "package-version", NULL,   miniargv_cb_increment_int, &showpackageversion, "show package name and version (from environment variables)"},
    {0,   NULL,              "TEXT", process_arg_append_str,    &message,            "text to display"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "BASENAME",        NULL,   miniargv_cb_set_const_str, &basename,           "package name\nif present will be displayed as part of the window title"},
    {0,   "VERSION",         NULL,   miniargv_cb_set_const_str, &version,            "package version\nif present will be displayed as part of the window title"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //parse environment and command line flags
  if (miniargv_process(argv, envp, argdef, envdef, NULL, NULL) != 0)
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
    miniargv_help(argdef, envdef, 20, 0);
#ifdef PORTCOLCON_VERSION
    printf(WINLIBS_HELP_COLOR);
#endif
    return 0;
  }
  //determine which text to show
  if (showpackageversion) {
    //show package name and version number
    if (message) {
      fprintf(stderr, "No other command line parameters allowed when showing package version.\n");
      return 2;
    }
    if (basename)
      allocstr_append(&message, ' ', basename);
    if (version)
      allocstr_append(&message, ' ', version);
    //use message as basis for window title
    if (message)
      title = strdup(message);
  } else {
    //use message as basis for window title
    if (message)
      title = strdup(message);
    //add package name and version to window title
    if (basename)
      allocstr_append(&title, ' ', basename);
    if (version)
      allocstr_append(&title, ' ', version);
  }
  //add current path to window title
  if ((s = portcolcon_getcurdir()) != NULL) {
    allocstr_append(&title, ' ', s);
    portcolcon_free_string(s);
  }
  //output
  portcolconhandle con = portcolcon_initialize();
  portcolcon_write_in_color(con, "-=[", PORTCOLCON_COLOR_DARK_GRAY, PORTCOLCON_COLOR_BLACK);
  portcolcon_printf_in_color(con, PORTCOLCON_COLOR_YELLOW, PORTCOLCON_COLOR_BLACK, " %s ", message);
  portcolcon_write_in_color(con, "]=-", PORTCOLCON_COLOR_DARK_GRAY, PORTCOLCON_COLOR_BLACK);
  portcolcon_write(con, "\n");
  portcolcon_set_title(con, (title ? title : ""));
  portcolcon_cleanup(con);
  //clean up
  free(message);
  free(title);
  return 0;
}

