#include "build-package.h"
#include "memory_buffer.h"
#include "filesystem.h"
#include "pkgfile.h"
#include "winlibs_common.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <crossrun.h>
#include <dirtrav.h>

unsigned int interrupted = 0;           //variable set by signal handler


////////////////////////////////////////////////////////////////////////

struct strip_ansi_data_struct {
  int status;
  FILE* dst;
};

#define ANSI_STATUS_DATA             0
#define ANSI_STATUS_CSI1             1
#define ANSI_STATUS_CSI_DONE         2
#define ANSI_STATUS_CSI_ALT_DONE     3
#define ANSI_STATUS_CSI_ALT_DONE_ESC 4

static inline void strip_ansi_next_character (char c, struct strip_ansi_data_struct* data)
{
  //see also: http://en.wikipedia.org/wiki/ANSI_escape_code
  switch (data->status) {
    case ANSI_STATUS_DATA:
      if (c == 27) {
        data->status = ANSI_STATUS_CSI1;
      } else if ((unsigned char)c == 0x9B) {
        data->status = ANSI_STATUS_CSI_DONE;
      } else if (c == 7) {
        //skip beep
      } else if (data->dst) {
        fputc(c, data->dst);
      }
      break;
    case ANSI_STATUS_CSI1:
      if (c == '[') {
        data->status = ANSI_STATUS_CSI_DONE;
      } else if (c == ']') {
        data->status = ANSI_STATUS_CSI_ALT_DONE;
      } else if (c >= 64 && c <= 95) {
        data->status = ANSI_STATUS_DATA;
      } else {
        //unknown ANSI/VT code
        fputc(27, data->dst);
        fputc(c, data->dst);
        data->status = ANSI_STATUS_DATA;
      }
      break;
    case ANSI_STATUS_CSI_DONE:
      if (c >= 64 && c <= 126)
        data->status = ANSI_STATUS_DATA;
      break;
    case ANSI_STATUS_CSI_ALT_DONE:
      if (c == 7)
        data->status = ANSI_STATUS_DATA;
      else if (c == 27)
        data->status = ANSI_STATUS_CSI_ALT_DONE_ESC;
      else if ((unsigned char)c == 0x9C)
        data->status = ANSI_STATUS_DATA;
      break;
    case ANSI_STATUS_CSI_ALT_DONE_ESC:
      if (c == '\\')
        data->status = ANSI_STATUS_DATA;
      else
        data->status = ANSI_STATUS_CSI_ALT_DONE;
      break;
  }
}

////////////////////////////////////////////////////////////////////////

//typedef void (*build_package_output_fn)(char* buf, size_t buflen, void* callbackdata);

static int detect_last_line (const char* line)
{
  const char* p = line;
  if (!line)
    return 0;
  while (*p && isspace(*p))
    p++;
  if (strncasecmp(p, "~/makeDevPak.sh", 15) == 0 || strncasecmp(p, "wl-makepackage", 14) == 0)
    return 1;
  return 0;
}

/*
char** split_to_args (const char* cmd)
{
  char** args = NULL;
  int n = 0;
  char quote = 0;
  char* p = cmd;
  char* q;
  if (!cmd)
    return NULL;
  while (*p) {
    q = p;
    while (*q && (*q != ' ' || quote)) {
      if (quote && *q == quote)
        quote = 0;
      else if (*q == '"')
        quote = *q;
      q++;
    }
    if ((args = (char**)realloc(args, (n + 2) * sizeof(char*))) == NULL)
      return NULL;
    if ((args[n] = (char*)malloc((q - p) + 1)) == NULL)
      return NULL;
    memcpy(args[n], p, q - p);
    args[n][q - p] = 0;
    n++;
    p = q;
    while (*p == ' ')
      p++;
  }
  if (n)
    args[n] = NULL;
  return args;
}

void free_args (char** args)
{
  if (args) {
    char** arg = args;
    while (*arg) {
      free(*arg);
      arg++;
    }
    free(args);
  }
}
*/

struct build_package_write_thread_struct {
  packageinfo_file pkgfile;
  crossrun proc;
  char* buildpath;
};

void* build_package_write_thread (void* data)
{
  char* line;
  int status = 0;
  int non_export_lines_seen = 0;
  struct build_package_write_thread_struct* write_thread_data = (struct build_package_write_thread_struct*)data;
  //change to build folder if requested
  if (write_thread_data->buildpath && write_thread_data->buildpath[0]) {
    crossrun_write(write_thread_data->proc, "cd '");
    crossrun_write(write_thread_data->proc, write_thread_data->buildpath);
    crossrun_write(write_thread_data->proc, "'\n");
  }
  //process build instruction lines
  while (!interrupted && (line = packageinfo_file_readline(write_thread_data->pkgfile)) != NULL) {
    if (status == 0 && (line[0] != 0 && line[0] != '#')) {
      status++;
    } else if (!non_export_lines_seen) {
      if (line[0] == '#')
        status = 0;
      else if (line[0] != 0 && strncmp(line, "export ", 7) != 0)
        non_export_lines_seen++;
    }
    if (status > 0) {
      crossrun_write(write_thread_data->proc, line);
      crossrun_write(write_thread_data->proc, "\n");
    }
    if (detect_last_line(line)) {
      free(line);
      break;
    }
    free(line);
  }
  close_packageinfo_file(write_thread_data->pkgfile);
  //write exit command to shell
  crossrun_write(write_thread_data->proc, "\nexit $?\n");
  return NULL;
}

unsigned long build_package (const char* infopath, const char* basename, const char* shell, const char* logfile, const char* buildpath)
{
  //open shell process
  pthread_t write_thread;
  packageinfo_file pkgfile;
  crossrunenv env;
  crossrun proc;
  struct strip_ansi_data_struct strip_ansi_data;
  unsigned long exitcode;
  char buf[128];
  int buflen;
  size_t i;
  //open build instructions
  if ((pkgfile = open_packageinfo_file(infopath, basename)) == NULL)
    return 0xFFFF;
  //open log file
  strip_ansi_data.status = ANSI_STATUS_DATA;
  strip_ansi_data.dst = NULL;
  if (logfile) {
    strip_ansi_data.dst = fopen(logfile, "wb");
  }
  //prepare environment and run shell process
  env = crossrunenv_create_from_system();
  crossrunenv_set(&env, "PS1", "$ ");
  crossrunenv_set(&env, "PS2", "> ");
  crossrunenv_set(&env, "PS4", "+ ");
  crossrunenv_set(&env, "TERM", "VT100");
  crossrunenv_set(&env, "FORCE_ANSI", "1");
#if CROSSRUN_VERSION_MAJOR == 0
  proc = crossrun_open(shell, env, CROSSRUN_PRIO_LOW);
#else
  proc = crossrun_open(shell, env, CROSSRUN_PRIO_LOW, NULL);
#endif
  crossrunenv_free(env);
  if (proc == NULL) {
    fprintf(stderr, "Error running command: %s\n", shell);
    return 0xFFFE;
  }
  //start thread for writing to process
  struct build_package_write_thread_struct write_thread_data;
  write_thread_data.pkgfile = pkgfile;
  write_thread_data.proc = proc;
  if (!buildpath) {
    write_thread_data.buildpath = NULL;
  } else {
    //create temporary build folder
    struct memory_buffer* s = memory_buffer_create();
    if (*buildpath)
      memory_buffer_set_printf(s, "%s%c", buildpath, PATH_SEPARATOR);
    memory_buffer_append_printf(s, "%lu.%s", crossrun_get_pid(proc), basename);
    write_thread_data.buildpath = memory_buffer_free_to_allocated_string(s);
    if (recursive_mkdir(write_thread_data.buildpath) != 0) {
      fprintf(stderr, "Error creating build folder: %s\n", write_thread_data.buildpath);
      crossrun_kill(proc);
      crossrun_close(proc);
      return 0xFFFD;
    }
  }
  if (pthread_create(&write_thread, NULL, build_package_write_thread, &write_thread_data) != 0) {
    fprintf(stderr, "Error starting thread\n");
    crossrun_kill(proc);
    crossrun_close(proc);
    return 0xFFFC;
  }
  //process shell output until the shell process finished
  while (!interrupted && (buflen = crossrun_read(proc, buf, sizeof(buf))) > 0) {
    printf("%.*s", buflen, buf);
    if (strip_ansi_data.dst) {
      for (i = 0; i < buflen; i++) {
        strip_ansi_next_character(buf[i], &strip_ansi_data);
      }
      //fflush(strip_ansi_data.dst);
    }
  }
  if (interrupted)
    crossrun_kill(proc);
  else
    crossrun_wait(proc);
  pthread_join(write_thread, NULL);
  exitcode = crossrun_get_exit_code(proc);
  //clean up
  crossrun_close(proc);
  if (strip_ansi_data.dst)
    fclose(strip_ansi_data.dst);
  if (interrupted)
    printf("\nAborted building %s\n", basename);
  else
    printf("Done building %s (shell exit code: %lu)\n", basename, exitcode);
  if (write_thread_data.buildpath) {
    //delete temporary build folder and all its contents
    int retries = 30;
    int status = -1;
    while (retries-- > 0) {
      if ((status = dirtrav_recursive_delete(write_thread_data.buildpath)) == 0)
        break;
      SLEEP_SECONDS(1);
    }
    if (status != 0)
      fprintf(stderr, "Error cleaning up working folder: %s\n", write_thread_data.buildpath);
    free(write_thread_data.buildpath);
  }
  return exitcode;
}

////////////////////////////////////////////////////////////////////////

