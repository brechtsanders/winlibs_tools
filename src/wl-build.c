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
#include <crossrun.h>
#include <dirtrav.h>
#include "package_info.h"
#include "pkgdb.h"
#include "sorted_unique_list.h"
#include "memory_buffer.h"
#include "filesystem.h"
#include "handle_interrupts.h"
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

#ifdef _WIN32
#define SLEEP_SECONDS(s) Sleep(s * 1000);
#else
#define SLEEP_SECONDS(s) sleep(s);
#endif

unsigned int interrupted = 0;           //variable set by signal handler

DEFINE_INTERRUPT_HANDLER_BEGIN(handle_break_signal)
{
  interrupted++;
}
DEFINE_INTERRUPT_HANDLER_END(handle_break_signal)

////////////////////////////////////////////////////////////////////////

enum package_filter_type_enum {
  filter_type_all,
  filter_type_changed
};

struct add_package_and_dependencies_to_list_struct {
  sorted_unique_list* packagenamelist;
  const char* packageinfopath;
  enum package_filter_type_enum filtertype;
};

struct package_info_extradata_struct {
  enum package_filter_type_enum filtertype;
  unsigned char visited;
  unsigned char checkingcyclic;
  struct package_info_struct* cyclic_start_pkginfo;
  struct package_info_struct* cyclic_next_pkginfo;
  size_t cyclic_size;
};

#define PKG_XTRA(pkginfo) ((struct package_info_extradata_struct*)pkginfo->extradata)

int packageinfo_cmp (const char* data1, const char* data2)
{
  return strcasecmp(((struct package_info_struct*)data1)->basename, ((struct package_info_struct*)data2)->basename);
}

int add_package_and_dependencies_to_list (const char* basename, struct add_package_and_dependencies_to_list_struct* data)
{
  struct package_info_struct* pkginfo;
  struct package_info_struct searchpkginfo;
  //check if package is already in list
  searchpkginfo.basename = (char*)basename;
  if (!sorted_unique_list_find(data->packagenamelist, (char*)&searchpkginfo)) {
    //read package information
    if ((pkginfo = read_packageinfo(data->packageinfopath, basename)) == NULL) {
      //fprintf(stderr, "Error reading package information for package %s from %s\n", basename, data->packageinfopath);
      return 0;
    }
    //skip package if it doesn't build
    if (!pkginfo->buildok) {
      free_packageinfo(pkginfo);
      return 0;
    }
    //add additional data
    if ((pkginfo->extradata = malloc(sizeof(struct package_info_extradata_struct))) == NULL) {
      return -1;
    }
    PKG_XTRA(pkginfo)->filtertype = data->filtertype;
    PKG_XTRA(pkginfo)->visited = 0;
    PKG_XTRA(pkginfo)->checkingcyclic = 0;
    PKG_XTRA(pkginfo)->cyclic_start_pkginfo = NULL;
    PKG_XTRA(pkginfo)->cyclic_next_pkginfo = NULL;
    PKG_XTRA(pkginfo)->cyclic_size = 0;
    pkginfo->extradata_free_fn = free;
    //add package information to list
    if (!interrupted && sorted_unique_list_add_allocated(data->packagenamelist, (char*)pkginfo) == 0) {
      //recurse for each dependency
      iterate_packages_in_comma_separated_list(pkginfo->dependencies, (package_callback_fn)add_package_and_dependencies_to_list, data);
      iterate_packages_in_comma_separated_list(pkginfo->builddependencies, (package_callback_fn)add_package_and_dependencies_to_list, data);
      iterate_packages_in_comma_separated_list(pkginfo->optionaldependencies, (package_callback_fn)add_package_and_dependencies_to_list, data);
    }
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////

typedef enum {
  dep_type_none = 0,
  dep_type_normal = 1,
  dep_type_build = 2,
  dep_type_optional = 3,
} package_dependency_type;

typedef void (*cyclic_graph_detected_fn)(struct package_info_struct* pkginfo, package_dependency_type dependency_type, struct package_info_struct* startpkginfo, void* callbackdata);

struct check_node_for_cyclic_graph_struct {
  sorted_unique_list* sortedpackagelist;
  size_t count_loops;
  package_dependency_type current_link_type;
  struct package_info_struct* parent_pkginfo;
  struct package_info_struct* cyclic_start_pkginfo;
};

int check_node_for_cyclic_graph_iterate_dependencies (const char* entryname, struct check_node_for_cyclic_graph_struct* data);

size_t check_node_for_cyclic_graph (struct package_info_struct* pkginfo, struct check_node_for_cyclic_graph_struct* data)
{
  size_t result;
  package_dependency_type prev_link_type;
  struct package_info_struct* prev_pkginfo;
  //mark package as being checked
  PKG_XTRA(pkginfo)->checkingcyclic = 1;
  //recursively process dependencies
  prev_link_type = data->current_link_type;
  data->current_link_type = dep_type_normal;
  prev_pkginfo = data->parent_pkginfo;
  data->parent_pkginfo = pkginfo;
  result = iterate_packages_in_comma_separated_list(pkginfo->dependencies, (package_callback_fn)check_node_for_cyclic_graph_iterate_dependencies, data);
  if (!result) {
    data->current_link_type = dep_type_build;
    result = iterate_packages_in_comma_separated_list(pkginfo->builddependencies, (package_callback_fn)check_node_for_cyclic_graph_iterate_dependencies, data);
  }
  if (!result) {
    data->current_link_type = dep_type_optional;
    result = iterate_packages_in_comma_separated_list(pkginfo->optionaldependencies, (package_callback_fn)check_node_for_cyclic_graph_iterate_dependencies, data);
  }
  data->current_link_type = prev_link_type;
  data->parent_pkginfo = prev_pkginfo;
  //check if circular loop has come full circle
  if (result && pkginfo == PKG_XTRA(pkginfo)->cyclic_start_pkginfo) {
/*
    if (data->cyclic_graph_detected_callback)
      (*data->cyclic_graph_detected_callback)(pkginfo, none, pkginfo, data->cyclic_graph_detected_callbackdata);
*/
    data->cyclic_start_pkginfo = NULL;
    result = 0;
  }
  //store circular loop information if part of circular loop
  if (data->cyclic_start_pkginfo) {
    PKG_XTRA(data->cyclic_start_pkginfo)->cyclic_size++;
    PKG_XTRA(pkginfo)->cyclic_start_pkginfo = data->cyclic_start_pkginfo;
    PKG_XTRA(pkginfo)->cyclic_next_pkginfo = data->parent_pkginfo;
  }
  //unmark package as being checked
  PKG_XTRA(pkginfo)->checkingcyclic = 0;
  //mark package as visited
  PKG_XTRA(pkginfo)->visited = 1;
  return result;
}

int check_node_for_cyclic_graph_iterate_dependencies (const char* entryname, struct check_node_for_cyclic_graph_struct* data)
{
  struct package_info_struct searchpkginfo;
  struct package_info_struct* pkginfo;
  int result = 0;
  //get entry from list
  searchpkginfo.basename = (char*)entryname;
  if ((pkginfo = (struct package_info_struct*)sorted_unique_list_search(data->sortedpackagelist, (const char*)&searchpkginfo)) != NULL) {
    //don't check entry if it was checked before
    if (!PKG_XTRA(pkginfo)->visited) {
      //cyclic graph detected if package already being checked
      if (PKG_XTRA(pkginfo)->checkingcyclic) {
        data->count_loops++;
        data->cyclic_start_pkginfo = pkginfo;
        PKG_XTRA(pkginfo)->cyclic_size = 1;
        PKG_XTRA(pkginfo)->cyclic_start_pkginfo = pkginfo;
        PKG_XTRA(pkginfo)->cyclic_next_pkginfo = data->parent_pkginfo;
/*
        if (data->cyclic_graph_detected_callback)
          (*data->cyclic_graph_detected_callback)(pkginfo, data->current_link_type, pkginfo, data->cyclic_graph_detected_callbackdata);
*/
        return 1;
      }
      //otherwise recurse
      if (check_node_for_cyclic_graph(pkginfo, data)) {
        //cyclic graph detected
/*
        if (data->cyclic_graph_detected_callback)
          (*data->cyclic_graph_detected_callback)(pkginfo, data->current_link_type, PKG_XTRA(pkginfo)->cyclic_start_pkginfo, data->cyclic_graph_detected_callbackdata);
*/
        result = 1;
      }
    }
  }
  return result;
}

/*
void cyclic_graph_detected (struct package_info_struct* pkginfo, package_dependency_type dependency_type, struct package_info_struct* startpkginfo, void* callbackdata)
{
  static const char* package_dependency_type_left_arrow[] = {"|", "<-", "<=", "<~"};
  if (pkginfo == startpkginfo && dependency_type != none) {
    printf("Cyclic dependency detected for %s:\n  ", pkginfo->basename);
  }
  printf("%s", pkginfo->basename);
  if (dependency_type != none) {
    printf(" %s ", package_dependency_type_left_arrow[dependency_type]);
  } else {
    printf("\n");
  }
}
*/

////////////////////////////////////////////////////////////////////////

//use topological sort algorithm for directed acyclic graph

struct topological_sort_depth_first_search_struct {
  sorted_unique_list* sortedpackagelist;
  size_t pkgorderpos;
  struct package_info_struct** pkgorder;
};

int topological_sort_depth_first_search_iterate_dependencies (const char* entryname, struct topological_sort_depth_first_search_struct* data);

void topological_sort_depth_first_search (struct package_info_struct* pkginfo, struct topological_sort_depth_first_search_struct* data)
{
  //mark package as visited
  PKG_XTRA(pkginfo)->visited = 1;
  //recursively process dependencies
  iterate_packages_in_comma_separated_list(pkginfo->dependencies, (package_callback_fn)topological_sort_depth_first_search_iterate_dependencies, data);
  iterate_packages_in_comma_separated_list(pkginfo->builddependencies, (package_callback_fn)topological_sort_depth_first_search_iterate_dependencies, data);
  if (!PKG_XTRA(pkginfo)->cyclic_start_pkginfo)
    iterate_packages_in_comma_separated_list(pkginfo->optionaldependencies, (package_callback_fn)topological_sort_depth_first_search_iterate_dependencies, data);
  ////else printf("Skipping optional dependencies for %s (%lu-step loop via: %s)\n", pkginfo->basename, (unsigned long)PKG_XTRA(PKG_XTRA(pkginfo)->cyclic_start_pkginfo)->cyclic_size, PKG_XTRA(pkginfo)->cyclic_start_pkginfo->basename);/////
  //store current package as next in line
  data->pkgorder[data->pkgorderpos++] = pkginfo;
}

int topological_sort_depth_first_search_iterate_dependencies (const char* entryname, struct topological_sort_depth_first_search_struct* data)
{
  struct package_info_struct searchpkginfo;
  struct package_info_struct* pkginfo;
  //get entry from list
  searchpkginfo.basename = (char*)entryname;
  if ((pkginfo = (struct package_info_struct*)sorted_unique_list_search(data->sortedpackagelist, (const char*)&searchpkginfo)) != NULL) {
    //recurse if not already visited
    if (!PKG_XTRA(pkginfo)->visited) {
      topological_sort_depth_first_search(pkginfo, data);
    }
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////

struct package_info_list_struct* generate_build_list (sorted_unique_list* sortedpackagelist)
{
  size_t i;
  size_t n;
  struct package_info_struct* pkginfo;
  struct check_node_for_cyclic_graph_struct cyclic_check_data;
  struct topological_sort_depth_first_search_struct topsort_data;
  struct package_info_list_struct* packagebuildlist = NULL;
  struct package_info_list_struct** packagebuildlistnext = &packagebuildlist;

  //check if directed graph is cyclic
  cyclic_check_data.sortedpackagelist = sortedpackagelist;
  cyclic_check_data.count_loops = 0;
  cyclic_check_data.parent_pkginfo = NULL;
  cyclic_check_data.cyclic_start_pkginfo = NULL;
  if ((n = sorted_unique_list_size(sortedpackagelist)) > 0) {
    //process all entries
    for (i = 0; i < n; i++) {
      pkginfo = (struct package_info_struct*)sorted_unique_list_get(sortedpackagelist, i);
      if (!PKG_XTRA(pkginfo)->visited) {
        check_node_for_cyclic_graph(pkginfo, &cyclic_check_data);
      }
    }
    //clear visited flags
    for (i = 0; i < n; i++) {
      pkginfo = (struct package_info_struct*)sorted_unique_list_get(sortedpackagelist, i);
      PKG_XTRA(pkginfo)->visited = 0;
    }
  }

  //determine build order based on dependencies using topological sort algorithm for directed acyclic graph
  if ((n = sorted_unique_list_size(sortedpackagelist)) > 0) {
    if ((topsort_data.pkgorder = (struct package_info_struct**)malloc(n * sizeof(struct package_info_struct*))) == NULL) {
      fprintf(stderr, "Memory allocation error\n");
      return NULL;
    }
    topsort_data.sortedpackagelist = sortedpackagelist;
    topsort_data.pkgorderpos = 0;
    for (i = 0; i < n; i++) {
      pkginfo = (struct package_info_struct*)sorted_unique_list_get(sortedpackagelist, i);
      if (!PKG_XTRA(pkginfo)->visited) {
        topological_sort_depth_first_search(pkginfo, &topsort_data);
      }
    }
  }

  //create build order list
  for (i = 0; i < n; i++) {
    //add package to list
    if ((*packagebuildlistnext = (struct package_info_list_struct*)malloc(sizeof(struct package_info_list_struct))) == NULL) {
      fprintf(stderr, "Memory allocation error\n");
      return NULL;
    }
    (*packagebuildlistnext)->info = topsort_data.pkgorder[i];
    (*packagebuildlistnext)->next = NULL;
    packagebuildlistnext = &(*packagebuildlistnext)->next;
    //when part of cyclic loop rebuild packages as needed
    if (PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo && PKG_XTRA(PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo)->cyclic_size > 0) {
      PKG_XTRA(PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo)->cyclic_size--;
      if (PKG_XTRA(PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo)->cyclic_size == 0) {
        pkginfo = PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo;
        struct package_info_struct* pkginfo;
        pkginfo = PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo;
        //skip to current package
        while (pkginfo && pkginfo != topsort_data.pkgorder[i])
          pkginfo = PKG_XTRA(pkginfo)->cyclic_next_pkginfo;
        while ((pkginfo = PKG_XTRA(pkginfo)->cyclic_next_pkginfo) != NULL) {
          //add package to list
          if ((*packagebuildlistnext = (struct package_info_list_struct*)malloc(sizeof(struct package_info_list_struct))) == NULL) {
            fprintf(stderr, "Memory allocation error\n");
            return NULL;
          }
          (*packagebuildlistnext)->info = pkginfo;
          (*packagebuildlistnext)->next = NULL;
          packagebuildlistnext = &(*packagebuildlistnext)->next;
          //abort when circular loop completed
          if (pkginfo == PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo)
            break;
        }
      }
    }
  }
  free(topsort_data.pkgorder);
  return packagebuildlist;
}

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
      } else if (c == 0x9B) {
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
      else if (c == 0x9C)
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

typedef void (*build_package_output_fn)(char* buf, size_t buflen, void* callbackdata);

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

struct dependancies_listed_but_not_depended_on_struct {
  size_t countmissing;
  sorted_unique_list* dependencies;
};

int dependancies_listed_but_not_depended_on_iteration (const char* basename, void* callbackdata)
{
  if (!sorted_unique_list_find(((struct dependancies_listed_but_not_depended_on_struct*)callbackdata)->dependencies, basename))
    ((struct dependancies_listed_but_not_depended_on_struct*)callbackdata)->countmissing++;
  return 0;
}

size_t dependancies_listed_but_not_depended_on (const char* dstdir, struct package_info_struct* pkginfo)
{
  struct memory_buffer* pkginfopath = memory_buffer_create();
  struct memory_buffer* filepath = memory_buffer_create();
  struct dependancies_listed_but_not_depended_on_struct data = {0, sorted_unique_list_create(strcmp, free)};
  //determine package information path
  memory_buffer_set_printf(pkginfopath, "%s%s%c%s%c", dstdir, PACKAGE_INFO_PATH, PATH_SEPARATOR, pkginfo->basename, PATH_SEPARATOR);
  //check dependancies
  sorted_unique_list_load_from_file(&data.dependencies, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_DEPENDENCIES_FILE)));
  iterate_packages_in_comma_separated_list(pkginfo->dependencies, dependancies_listed_but_not_depended_on_iteration, &data);
  sorted_unique_list_clear(data.dependencies);
  //check optional dependancies
  sorted_unique_list_load_from_file(&data.dependencies, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_OPTIONALDEPENDENCIES_FILE)));
  iterate_packages_in_comma_separated_list(pkginfo->optionaldependencies, dependancies_listed_but_not_depended_on_iteration, &data);
  sorted_unique_list_clear(data.dependencies);
  //check build dependancies
  sorted_unique_list_load_from_file(&data.dependencies, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_BUILDDEPENDENCIES_FILE)));
  iterate_packages_in_comma_separated_list(pkginfo->builddependencies, dependancies_listed_but_not_depended_on_iteration, &data);
  //clean up
  sorted_unique_list_free(data.dependencies);
  memory_buffer_free(pkginfopath);
  memory_buffer_free(filepath);
  return data.countmissing;
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
  sortedpackagelist = sorted_unique_list_create(packageinfo_cmp, (sorted_unique_free_fn)free_packageinfo);
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
  {
    char* logfile;
    int skip;
    unsigned long exitcode;
    struct package_info_list_struct* current;
    struct package_info_struct* pkginfo;
    struct package_metadata_struct* dbpkginfo;
    while (!interrupted && (current = packagebuildlist) != NULL) {
      skip = 0;
      //check installed version
      dbpkginfo = pkgdb_read_package(db, current->info->basename);
      printf("--> %s %s (", current->info->basename, current->info->version);
      if (!dbpkginfo)
        printf("currently not installed");
      else if (!dbpkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION])
        printf("installed without version information");
      else if (strcmp(dbpkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION], current->info->version) == 0)
          printf("already installed");
      else
        printf("installed version: %s", dbpkginfo->datafield[PACKAGE_METADATA_INDEX_VERSION]);
      printf(")\n");
      //check latest package information and determine if package should be skipped
      if ((pkginfo = read_packageinfo(packageinfopath, current->info->basename)) == NULL) {
        printf("package information for %s can no longer be found, skipping\n", current->info->basename);
        skip++;
      } else {
        //check if package still builds
        if (!pkginfo->buildok) {
          printf("%s is no longer marked as possible to build, skipping\n", current->info->basename);
          skip++;
        }
        //check if prerequisites are installed
        if (dstdir && (pkgdb_packages_are_installed(db, pkginfo->dependencies) == 0 || pkgdb_packages_are_installed(db, pkginfo->builddependencies) == 0)) {
          printf("missing dependencies, skipping\n");
          skip++;
        }
      }
      //determine if already installed package should be rebuilt
      if (!skip && dstdir && dbpkginfo) {
        if (PKG_XTRA(current->info)->cyclic_start_pkginfo) {
          //part of cyclic loop
          struct package_info_struct* cyclic_pkginfo;
          if ((cyclic_pkginfo = read_packageinfo(packageinfopath, PKG_XTRA(current->info)->cyclic_start_pkginfo->basename)) != NULL) {
            if (!dependancies_listed_but_not_depended_on(dstdir, cyclic_pkginfo))
              skip++;
            free_packageinfo(cyclic_pkginfo);
          }
          if (!skip)
            printf("part of cyclic dependency (via %s), building anyway\n", PKG_XTRA(current->info)->cyclic_start_pkginfo->basename);
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
          if ((install_lastchanged = installed_package_lastchanged(dstdir, current->info->basename)) != 0 && install_lastchanged < pkginfo->lastchanged) {
            printf("build recipe for %s was changed, rebuilding (installed: %lu, package: %lu)\n", pkginfo->basename, (unsigned long)install_lastchanged, (unsigned long)pkginfo->lastchanged);
            skip = 0;
          }
        }
      }
      //clean up
      if (dbpkginfo)
        package_metadata_free(dbpkginfo);
      if (pkginfo)
        free_packageinfo(pkginfo);
      //build package (unless it should be skipped)
      if (!skip) {
        //determine log file path
        logfile = NULL;
        if (logdir) {
          size_t logdirlen = strlen(logdir);
          size_t basenamelen = strlen(current->info->basename);
          if ((logfile = (char*)malloc(logdirlen + basenamelen + LOG_FILE_EXTENSION_LEN + 2)) != NULL) {
            memcpy(logfile, logdir, logdirlen);
            logfile[logdirlen] = PATH_SEPARATOR;
            memcpy(logfile + logdirlen + 1, current->info->basename, basenamelen);
            strcpy(logfile + logdirlen + 1 + basenamelen, LOG_FILE_EXTENSION);
          }
        }
        //build package
        exitcode = build_package(packageinfopath, current->info->basename, shellcmd, logfile, builddir);
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

