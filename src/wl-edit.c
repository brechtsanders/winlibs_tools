#include "winlibs_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#endif
#include <miniargv.h>
#include "filesystem.h"

#define PROGRAM_NAME    "wl-edit"
#define PROGRAM_DESC    "Command line utility to edit winlibs build recipes"
#ifdef _WIN32
#define DEFAULT_EDITOR  "Notepad++"
#else
#define DEFAULT_EDITOR  "nano"
#endif

char* strdup_quoted (const char* s)
{
  char* result;
  char* p;
  int i;
  int quotes;
  //abort if NULL
  if (!s)
    return NULL;
  //count quotes
  quotes = 0;
  for (i = 0; s[i]; i++) {
    if (s[i] == '"')
      quotes++;
  }
  //allocate memory
  if ((result = (char*)malloc(i + quotes + 3)) == NULL)
    return NULL;
  //copy quoted string
  i = 0;
  p = result;
  *p++ = '"';
  while (s[i]) {
    if (s[i] == '"')
      *p++ = '"';
    *p++ = s[i++];

  }
  *p++ = '"';
  *p = 0;
  return result;
}

int main (int argc, char *argv[], char *envp[])
{
  int showversion = 0;
  int showhelp = 0;
  char* editor = NULL;
  int ignoremissing = 0;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",            NULL,   miniargv_cb_increment_int, &showhelp,           "show command line help", NULL},
    {0,   "version",         NULL,   miniargv_cb_increment_int, &showversion,        "show version information", NULL},
    {'e', "editor",          "PROG", miniargv_cb_strdup,        &editor,             "editor program to launch\noverrides EDITOR environment variable\ndefaults to " DEFAULT_EDITOR, NULL},
    {'i', "ignoremissing",   NULL,   miniargv_cb_set_boolean,   &ignoremissing,      "don't abort when any of the specified files are not found", NULL},
    {0,   NULL,              "FILE", miniargv_cb_noop,          NULL,                "file to open (multiple allowed)", NULL},
    MINIARGV_DEFINITION_END
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "EDITOR",          NULL,   miniargv_cb_strdup,        &editor,             "editor to launch\ndefaults to " DEFAULT_EDITOR, NULL},
    MINIARGV_DEFINITION_END
  };
  //parse environment and command line flags
  if (miniargv_process(argv, envp, argdef, envdef, NULL, NULL) != 0)
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
    miniargv_help(argdef, envdef, 20, 0);
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
  //detect editor if needed
  if (!editor) {
#ifdef _WIN32
    //detect Notepad++ in the default install locaction (path based on ProgramFiles environment variable)
    char* s;
    if ((s = getenv("ProgramFiles")) != NULL) {
      if ((editor = (char*)malloc(strlen(s) + 25)) != NULL) {
        strcpy(editor, s);
        strcat(editor, "\\Notepad++\\notepad++.exe");
      }
    }
    //otherwise detect Notepad++ in the default install locaction (fixed path)
    if (!editor || !file_exists(editor)) {
      if (editor)
        free(editor);
      editor = strdup("C:\\Program Files\\Notepad++\\notepad++.exe");
    }
#else
    editor = strdup("nano");
#endif
  }
  if (!editor) {
    fprintf(stderr, "Error determining editor to use\n");
    return 2;
  }
  //determine number of files specified
  int i;
  char** p;
  char** editorargv;
  int editorargc = 1;
  i = 0;
  while ((i = miniargv_get_next_arg_param(i, argv, argdef, NULL)) > 0)
    editorargc++;
  //launch editor
  if ((editorargv = (char**)malloc((editorargc + 1) * sizeof(char*))) == NULL) {
    fprintf(stderr, "Memory allocation error\n");
    return 4;
  }
  for (i = 0; i <= editorargc; i++)
    editorargv[i] = 0;
  p = editorargv;
#ifdef _WIN32
  *p++ = strdup_quoted(editor);
#else
  *p++ = editor;
#endif
  i = 0;
  while ((i = miniargv_get_next_arg_param(i, argv, argdef, NULL)) > 0) {
    if (!file_exists(argv[i])) {
      fprintf(stderr, "File not found: %s\n", argv[i]);
      if (ignoremissing)
        continue;
      return 5;
    }
#ifdef _WIN32
    *p++ = strdup_quoted(argv[i]);
#else
    *p++ = argv[i];
#endif
  }
  if (execvp(editor, editorargv) != 0) {
    fprintf(stderr, "Error launching editor: %s\n", editor);
    return 6;
  }
  //clean up
#ifdef _WIN32
  for (i = 0; i < editorargc; i++)
    free(editorargv[i]);
  free(editorargv);
#endif
  free(argv);
  free(editor);
  return 0;
}

