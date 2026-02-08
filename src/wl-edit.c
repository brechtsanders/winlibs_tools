#include "winlibs_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <limits.h>
#endif
#include <miniargv.h>
#ifdef WITH_LIBXDIFF
#include "generatediff.h"
#endif
#include "filesystem.h"

#define PROGRAM_NAME    "wl-edit"
#define PROGRAM_DESC    "Command line utility to edit winlibs build recipes"
#ifdef _WIN32
#define DEFAULT_EDITOR  "Notepad++"
#else
#define DEFAULT_EDITOR  "nano"
#endif
#define DEFAULT_BACKUP_EXTENSION ".bak"
#define DEFAULT_DIFF_CONTEXT 1
#define FILE_COPY_BUFFER_SIZE 4096

#define STRINGIZE_(n) #n
#define STRINGIZE(n) STRINGIZE_(n)

#ifdef _WIN32
const char* strcasestr (const char* s1, const char* s2)
{
  if (!s1 || !s2)
    return 0;
  size_t s2len = strlen(s2);
  while (*s1)
    if (!strnicmp(s1++, s2, s2len))
      return (s1 - 1);
  return 0;
}

//quote string for Windows (surround with quotes and double quotes in the text)

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

/*
//quote string for Windows (surround with quotes and double quotes in the text)
char* strndup_quoted (const char* s, size_t len)
{
  char* result;
  char* p;
  size_t i;
  int quotes;
  //abort if NULL
  if (!s)
    return NULL;
  //count quotes
  quotes = 0;
  for (i = 0; i < len; i++) {
    if (s[i] == '"')
      quotes++;
  }
  //allocate memory
  if ((result = (char*)malloc(len + quotes + 3)) == NULL)
    return NULL;
  //copy quoted string
  i = 0;
  p = result;
  *p++ = '"';
  while (i < len) {
    if (s[i] == '"')
      *p++ = '"';
    *p++ = s[i++];

  }
  *p++ = '"';
  *p = 0;
  return result;
}
*/
#endif

//split the argument in file path (quoted for Windows if needed) and line number
char* get_file_ad_line_from_arg (const char* arg, const char** line)
{
  //start with no line number
  if (line)
    *line = NULL;
  //abort if arg is NULL
  if (!arg)
    return NULL;
  size_t i;
  i = strlen(arg);
  while (i > 0 && arg[i - 1] != ':') {
    if (!isdigit(arg[--i])) {
      i = 0;
      break;
    }
  }
  if (i > 0 && arg[i - 1] == ':') {
    if (line)
      *line = arg + i;
#ifdef _WIN32
    char* result;
    if ((result = (char*)malloc(i)) != NULL) {
      memcpy(result, arg, i - 1);
      result[i - 1] = 0;
    }
    return result;
#else
    return strndup(arg, i - 1);
#endif
  }
  return strdup(arg);
}

struct filearg_struct {
  char* filepath;
  const char* linenumber;
};

struct filearglist_struct {
  struct filearg_struct* list;
  size_t listlen;
};

int filearglist_add (struct filearglist_struct* fileargs, const char* fileinfo)
{
  //grow list
  if ((fileargs->list = (struct filearg_struct*)realloc(fileargs->list, (fileargs->listlen + 1) * sizeof(struct filearg_struct))) == NULL)
    return 1;
  //determine file path and line number
  if ((fileargs->list[fileargs->listlen].filepath = get_file_ad_line_from_arg(fileinfo, &fileargs->list[fileargs->listlen].linenumber)) == NULL)
    return 2;
  fileargs->listlen++;
  return 0;
}

int filearglist_removelast (struct filearglist_struct* fileargs)
{
  if (fileargs->listlen <= 0)
    return -1;
  fileargs->listlen--;
  return 0;
}

void filearglist_cleanup (struct filearglist_struct* fileargs)
{
  int i;
  for (i = 0; i < fileargs->listlen; i++)
    free(fileargs->list[i].filepath);
  free(fileargs->list);
  fileargs->list = NULL;
  fileargs->listlen = 0;
}

int backup_file (const char* path, const char* ext, int showdiff, const char* packageversion)
{
  char* backuppath;
  FILE* src;
  FILE* dst;
  size_t len;
  if (!ext || !*ext)
    return -1;
  //determine path of backup file
  if ((backuppath = (char*)malloc((len = strlen(path)) + strlen(ext) + 1)) == NULL) {
    fprintf(stderr, "Memory allocation error\n");
    return 1;
  }
  memcpy(backuppath, path, len);
  strcpy(backuppath + len, ext);
  //create backup file if it doesn't already exist
  if (!file_exists(backuppath)) {
    char* buf;
    //open destination file
    if ((dst = fopen(backuppath,"wb")) == NULL) {
      fprintf(stderr, "Error opening backup destination file: %s\n", backuppath);
      free(backuppath);
      return 2;
    }
    //open source file
    if ((src = fopen(path,"rb")) == NULL) {
      fprintf(stderr, "Error opening backup source file: %s\n", path);
      fclose(dst);
      free(backuppath);
      return 3;
    }
    //allocate buffer
    if ((buf = (char*)malloc(FILE_COPY_BUFFER_SIZE)) == NULL) {
      fprintf(stderr, "Memory allocation error\n");
      fclose(dst);
      fclose(dst);
      free(backuppath);
      return 4;
    }
    //copy contents
    while ((len = fread(buf, 1, FILE_COPY_BUFFER_SIZE, src)) > 0) {
      if (fwrite(buf, 1, len, dst) != len) {
        fprintf(stderr, "Error writing to backup destination file: %s\n", backuppath);
        fclose(src);
        fclose(dst);
        unlink(backuppath);
        free(buf);
        free(backuppath);
        return 5;
      }
    }
    //close file handles
    fclose(src);
    fclose(dst);
    free(buf);
#ifdef WITH_LIBXDIFF
  } else if (showdiff) {
    //compare contents with existing backup file
    diff_handle diff;
    if ((diff = diff_create(backuppath, path)) != NULL) {
      printf("patch -ulbf %s", path);
      if (packageversion)
        printf(" (version >= %s)", packageversion);
      printf("\n");
      diff_generate(diff, 1, stdout);
      printf("EOF\n");
      diff_free(diff);
    }
#endif
  }
  //clean up
  free(backuppath);
  return 0;
}

int main (int argc, char *argv[], char *envp[])
{
  int showversion = 0;
  int showhelp = 0;
  char* editor = NULL;
  const char* linearg = NULL;
  int createbackup = 0;
  const char* backupext = DEFAULT_BACKUP_EXTENSION;
  int showdiff = 0;
  int diffcontext = DEFAULT_DIFF_CONTEXT;
  const char* packageversion = NULL;
  int ignoremissing = 0;
  int multiplecalls = 0;
#ifdef _WIN32
  int editorisnpp = 0;
#endif
  struct filearglist_struct fileargs = {NULL, 0};
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help",            NULL,      miniargv_cb_increment_int, &showhelp,       "show command line help", NULL},
    {0,   "version",         NULL,      miniargv_cb_increment_int, &showversion,    "show version information", NULL},
    {'e', "editor",          "PROG",    miniargv_cb_strdup,        &editor,         "editor program to launch\noverrides EDITOR environment variable\ndefaults to " DEFAULT_EDITOR, NULL},
    {'l', "linearg",         "ARG",     miniargv_cb_set_const_str, &linearg,        "line number argument to pass to editor program\ndefaults to"
#ifdef _WIN32
                                                                                    "\"-n\" when editor contains \"notepad++\" or else to "
#endif
                                                                                    "\"+\" (supported by nano and vim)"
                                                                                    , NULL},
    {'b', "backup",          NULL,      miniargv_cb_set_boolean,   &createbackup,   "create a backup file (if it doesn't already exist)", NULL},
    {'x', "backupextension", "EXT",     miniargv_cb_set_const_str, &backupext,      "extension to use for backup files (defaults to \"" DEFAULT_BACKUP_EXTENSION "\")", NULL},
#ifdef WITH_LIBXDIFF
    {'d', "diff",            NULL,      miniargv_cb_set_boolean,   &showdiff,       "calculate unified diff and show patch command", NULL},
    {'c', "context",         NULL,      miniargv_cb_set_int,       &diffcontext,    "unified diff context size (defaults to " STRINGIZE(DEFAULT_DIFF_CONTEXT) ")", NULL},
    {'n', "package-version", "VERSION", miniargv_cb_set_const_str, &packageversion, "package version number (defaults to $VERSION)", NULL},
#endif
    {'i', "ignoremissing",   NULL,      miniargv_cb_set_boolean,   &ignoremissing,  "don't abort when any of the specified files are not found", NULL},
    {'s', "separate",        NULL,      miniargv_cb_set_boolean,   &multiplecalls,  "spawn a separate editor for each file specified"
#ifdef _WIN32
                                                                                 "\nautomatically set when editor contains \"notepad++\""
#endif
                                                                                 , NULL},
    {0,   NULL,              "FILE[:N] ...", miniargv_cb_error,    NULL,            "file to open, optionally followed by a colon (\":\") and the line number where to place the cursor\nmultiple files may be specified", NULL},
    MINIARGV_DEFINITION_END
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0,   "EDITOR",          NULL,      miniargv_cb_strdup,        &editor,         "editor program to launch\ndefaults to " DEFAULT_EDITOR, NULL},
#ifdef WITH_LIBXDIFF
    {0,   "VERSION",         NULL,      miniargv_cb_set_const_str, &packageversion, "package version number (used in comments when showing patch command)", NULL},
#endif
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
#ifdef _WIN32
    if (strcasestr(editor, "notepad++") != NULL) {
      editorisnpp = 1;
      multiplecalls = 1;
    }
#endif
  //detect argument to use for specifying line number
  if (!linearg) {
#ifdef _WIN32
    if (editorisnpp) {
      linearg = "-n";
    } else
#endif
    {
      linearg = "+";
    }
  }
  if (createbackup && (!backupext || !*backupext)) {
    fprintf(stderr, "The extension for backup files must not be empty\n");
    return 3;
  }
  //process specified file information
  int i;
  i = 0;
  while ((i = miniargv_get_next_arg_param(i, argv, argdef, NULL)) > 0) {
    //add file information
    if (filearglist_add(&fileargs, argv[i]) != 0) {
      fprintf(stderr, "Error parsing command line argument: %s\n", argv[i]);
      return 3;
    }
    //check if file exists
    if (!file_exists(fileargs.list[fileargs.listlen - 1].filepath)) {
      //show error if file doesn't exist
      fprintf(stderr, "File not found: %s\n", fileargs.list[fileargs.listlen - 1].filepath);
      //abort if we're not ignoring the error
      if (!ignoremissing)
        return 4;
      //skip non-existent file
      filearglist_removelast(&fileargs);
      continue;
    }
  }
#ifdef WITH_LIBXDIFF
  if (diff_initialize() != 0) {
    fprintf(stderr, "Error initializing libxdiff\n");
    return 5;
  }
#endif
  //call editor
  if (multiplecalls) {
    //spawn separate editor for each file specified
    int i;
    char** p;
    char** q;
    char** editorargv;
    //prepare arguments
    if ((editorargv = (char**)malloc((1 * 2 + 2) * sizeof(char*))) == NULL) {
      fprintf(stderr, "Memory allocation error\n");
      return 6;
    }
    p = editorargv;
#ifdef _WIN32
    *p++ = strdup_quoted(editor);
#else
    *p++ = strdup(editor);
#endif
    *p = NULL;
    q = p;
    for (i = 0; i < fileargs.listlen; i++) {
      p = q;
      //add line number argument (if any)
      if (linearg && fileargs.list[i].linenumber && fileargs.list[i].linenumber[0]) {
        if ((*p = (char*)malloc(strlen(linearg) + strlen(fileargs.list[i].linenumber) + 1)) == NULL) {
          fprintf(stderr, "Memory allocation error\n");
          return 7;
        }
        strcpy(*p, linearg);
        strcat(*p++, fileargs.list[i].linenumber);
      }
      //add file path argument
#ifdef _WIN32
      *p++ = strdup_quoted(fileargs.list[i].filepath);
#else
      *p++ = strdup(fileargs.list[i].filepath);
#endif
      *p = NULL;
      //create backup file if requested
      if (createbackup) {
        if (backup_file(fileargs.list[i].filepath, backupext, showdiff, packageversion) != 0) {
          fprintf(stderr, "Error creating backup (with extension \"%s\") of file: %s\n", backupext, fileargs.list[i].filepath);
          return 8;
        }
      }
      //execute editor
#ifdef _WIN32
      if (_spawnvp(_P_WAIT, editor, (const char *const *)editorargv) == -1) {
#else
      //if (spawnvp(P_WAIT, editor, editorargv) == -1) {
      if (execvp(editor, editorargv) == -1) {
#endif
        fprintf(stderr, "Error launching editor: %s\n", editor);
        return 9;
      }
      //clean up arguments used for this call
      p = q;
      while (*p)
        free(*p++);
      *q = NULL;
    }
    //clean up common arguments
    p = editorargv;
    while (*p)
      free(*p++);
  } else {
    //execute editor once to open all files
    int i;
    char** p;
    char** editorargv;
    //prepare arguments
    if ((editorargv = (char**)malloc((fileargs.listlen * 2 + 2) * sizeof(char*))) == NULL) {
      fprintf(stderr, "Memory allocation error\n");
      return 6;
    }
    p = editorargv;
#ifdef _WIN32
    *p++ = strdup_quoted(editor);
#else
    *p++ = strdup(editor);
#endif
    *p = NULL;
    for (i = 0; i < fileargs.listlen; i++) {
      //add line number argument (if any)
      if (linearg && fileargs.list[i].linenumber && fileargs.list[i].linenumber[0]) {
        if ((*p = (char*)malloc(strlen(linearg) + strlen(fileargs.list[i].linenumber) + 1)) == NULL) {
          fprintf(stderr, "Memory allocation error\n");
          return 7;
        }
        strcpy(*p, linearg);
        strcat(*p++, fileargs.list[i].linenumber);
      }
      //add file path argument
#ifdef _WIN32
      *p++ = strdup_quoted(fileargs.list[i].filepath);
#else
      *p++ = strdup(fileargs.list[i].filepath);
#endif
    }
    *p = NULL;
    //create backup file if requested
    if (createbackup) {
      if (backup_file(fileargs.list[i].filepath, backupext, showdiff, packageversion) != 0) {
        fprintf(stderr, "Error creating backup (with extension \"%s\") of file: %s\n", backupext, fileargs.list[i].filepath);
        return 8;
      }
    }
    //execute editor
    //if (spawnvp(_P_WAIT, editor, editorargv) == -1) {
    if (execvp(editor, editorargv) != 0) {
      fprintf(stderr, "Error launching editor: %s\n", editor);
      return 10;
    }
    //clean up arguments
    p = editorargv;
    while (*p)
      free(*p++);
  }
  //clean up
  filearglist_cleanup(&fileargs);
  free(editor);
  return 0;
}

