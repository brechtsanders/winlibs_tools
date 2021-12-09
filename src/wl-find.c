#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <miniargv.h>
#include <portcolcon.h>
#include "pkgdb.h"
#include "winlibs_common.h"
#include "memory_buffer.h"

#define PROGRAM_NAME    "wl-find"
#define PROGRAM_DESC    "Command line utility to search in installed winlibs packages"

#if defined(_WIN32) && !defined(__MINGW64_VERSION_MAJOR)
#define strcasecmp _stricmp
#endif
#ifdef _WIN32
#define realpath(N,R) _fullpath((R),(N),_MAX_PATH)
#endif

#define SQL_SELECT_PACKAGE "SELECT basename, version, name, description, url, downloadurl, downloadsourceurl, category, type, versiondate, licensefile, licensetype, status, installed FROM package"
#define SQL_SELECT_PATH "SELECT package, path FROM package_path"

enum search_type {
  package_name_exact,
  package_name,
  package_info,
  file_name,
  file_name_only,
  folder_name,
  folder_name_only,
  package_files,
  package_folders
};

#define SEARCHTYPE2VOIDPTR(val) (void*)(uintptr_t)(enum search_type)(val)
#define VOIDPTR2SEARCHTYPE(ptr) (enum search_type)(uintptr_t)(ptr)

int process_arg_searchtype (const miniargv_definition* argdef, const char* value, void* callbackdata)
{
  *((enum search_type*)callbackdata) = VOIDPTR2SEARCHTYPE(argdef->userdata);
  return 0;
}

int process_arg_strdup (const miniargv_definition* argdef, const char* value, void* callbackdata)
{
  if (*(char**)argdef->userdata)
    free(*(char**)argdef->userdata);
  *(char**)argdef->userdata = strdup(value);
  return 0;
}

int process_arg_param (const miniargv_definition* argdef, const char* value, void* callbackdata)
{
  const char** paramlist = (const char**)argdef->userdata;
  int i = 0;
  while (paramlist[i])
    i++;
  paramlist[i++] = value;
  paramlist[i] = NULL;
  return 0;
}

int main (int argc, char** argv, char *envp[])
{
  portcolconhandle con;
  pkgdb_handle db;
  const char** paramlist;
  int showhelp = 0;
  enum search_type searchtype = package_info;
  int fullpath = 0;
  int ormultiple = 0;
  int shortoutput = 0;
  char* dstdir = NULL;
  //initialize list to hold search values
  paramlist = (const char**)malloc(argc * sizeof(char*));
  paramlist[0] = NULL;
  //definition of command line arguments
  const miniargv_definition argdef[] = {
    {'h', "help", NULL, miniargv_cb_increment_int, &showhelp, "show command line help"},
    {'i', NULL, "PATH", process_arg_strdup, &dstdir, "installation path (defaults to $MINGWPREFIX)"},
    {'p', NULL, NULL, process_arg_searchtype, SEARCHTYPE2VOIDPTR(package_name), "search in package name"},
    {'P', "package", NULL, process_arg_searchtype, SEARCHTYPE2VOIDPTR(package_name_exact), "search exact package name"},
    {'f', "filename", NULL, process_arg_searchtype, SEARCHTYPE2VOIDPTR(file_name_only), "search in file name"},
    {0, "filepath", NULL, process_arg_searchtype, SEARCHTYPE2VOIDPTR(file_name), "search in entire file path"},
    {'F', "foldername", NULL, process_arg_searchtype, SEARCHTYPE2VOIDPTR(folder_name_only), "search in folder name"},
    {0, "folderpath", NULL, process_arg_searchtype, SEARCHTYPE2VOIDPTR(folder_name), "search in entire folder path"},
    {'l', "files", NULL, process_arg_searchtype, SEARCHTYPE2VOIDPTR(package_files), "list all files in specified package(s) (implies --or)"},
    {'d', "folders", NULL, process_arg_searchtype, SEARCHTYPE2VOIDPTR(package_folders), "list all folders in specified package(s) (implies --or)"},
    {0, "or", NULL, miniargv_cb_increment_int, &ormultiple, "search any of the given arguments instead of all"},
    {0, "fullpath", NULL, miniargv_cb_increment_int, &fullpath, "show absolute installation path when showing file or folder names"},
    {'s', "short", NULL, miniargv_cb_increment_int, &shortoutput, "short output"},
    {0, NULL, "TEXT", process_arg_param, paramlist, "text to search for"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //definition of environment variables
  const miniargv_definition envdef[] = {
    {0, "MINGWPREFIX", "PATH", process_arg_strdup, &dstdir, "installation path"},
    {0, NULL, NULL, NULL, NULL, NULL}
  };
  //parse environment and command line flags
  if (miniargv_process(argv, envp, argdef, envdef, NULL, &searchtype) != 0)
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
  //check parameters
  if (!dstdir || !*dstdir) {
    fprintf(stderr, "Installation directory not specified\n");
    return 2;
  } else {
    char fullpath[PATH_MAX];
    if (!realpath(dstdir, fullpath)) {
      fprintf(stderr, "Unable to get real path for package destination directory: %s\n", dstdir);
      return 3;
    }
    free(dstdir);
    dstdir = strdup(fullpath);
  }
  //open package database
  if ((db = pkgdb_open(dstdir)) == NULL) {
    fprintf(stderr, "No valid package database found for: %s\n", dstdir);
    return 4;
  }
  //initialize color console
  con = portcolcon_initialize();
  //get data
  {
    int i;
    unsigned int valueindex;
    int status;
    const char* s;
    const char* t;
    struct memory_buffer* sql;
    struct memory_buffer* searchvalue;
    sqlite3_stmt* sqlresult;
    uint64_t count = 0;
    //build SQL query
    sql = memory_buffer_create();
    switch (searchtype) {
      case package_name_exact:
      case package_name:
      case package_info:
        memory_buffer_set(sql, SQL_SELECT_PACKAGE " WHERE ((");
        break;
      case package_files:
        ormultiple++;
      case file_name:
      case file_name_only:
        memory_buffer_set(sql, SQL_SELECT_PATH " WHERE (type = 0 AND (");
        break;
      case package_folders:
        ormultiple++;
      case folder_name:
      case folder_name_only:
        memory_buffer_set(sql, SQL_SELECT_PATH " WHERE (type = 1 AND (");
        break;
    }
    valueindex = 0;
    while (paramlist[valueindex]) {
      if (valueindex > 0) {
        memory_buffer_append(sql, ") ");
        if (ormultiple)
          memory_buffer_append(sql, "OR");
        else
          memory_buffer_append(sql, "AND");
        memory_buffer_append(sql, " (");
      }
      switch (searchtype) {
        case package_name_exact:
          //exact case-insensitive match with package name only
          memory_buffer_append_printf(sql, "LOWER(basename)=LOWER(?%u)", valueindex + 1);
          break;
        case package_name:
        case package_info:
          //case-insensitive substring match
          memory_buffer_append_printf(sql, "LOWER(basename) LIKE '%%' || LOWER(?%u) || '%%' ESCAPE '\\'", valueindex + 1);
          if (searchtype == package_info)
            memory_buffer_append_printf(sql, " OR LOWER(name) LIKE '%%' || LOWER(?%u) || '%%' ESCAPE '\\' OR LOWER(description) LIKE '%%' || LOWER(?%u) || '%%' ESCAPE '\\'", valueindex + 1, valueindex + 1);
          break;
        case file_name:
        case file_name_only:
        case folder_name:
        case folder_name_only:
          memory_buffer_append_printf(sql, "path LIKE '%%' || ?%u || '%%' ESCAPE '\\'", valueindex + 1);
          break;
        case package_files:
        case package_folders:
          memory_buffer_append_printf(sql, "package=?%u", valueindex + 1);
          break;
      }
      valueindex++;
    }
    memory_buffer_append(sql, ")) ORDER BY ");
    switch (searchtype) {
      case package_name_exact:
      case package_name:
      case package_info:
        memory_buffer_append(sql, "basename");
        break;
      case file_name:
      case file_name_only:
      case folder_name:
      case folder_name_only:
      case package_files:
      case package_folders:
        memory_buffer_append(sql, "package, path");
        break;
    }
//printf("SQL:\n%s\n", memory_buffer_get(sql));/////
    if ((status = sqlite3_prepare_v2(pkgdb_get_sqlite3_handle(db), memory_buffer_get(sql), -1, &sqlresult, NULL)) == SQLITE_OK) {
      valueindex = 0;
      searchvalue = memory_buffer_create();
      while (paramlist[valueindex]) {
        memory_buffer_set(searchvalue, paramlist[valueindex]);
        if (searchtype != package_name_exact) {
          memory_buffer_find_replace_data(searchvalue, "\\", "\\\\");
          memory_buffer_find_replace_data(searchvalue, "%", "\\%");
          memory_buffer_find_replace_data(searchvalue, "_", "\\_");
        }
        sqlite3_bind_text(sqlresult, valueindex + 1, memory_buffer_get(searchvalue), -1, SQLITE_TRANSIENT);
        valueindex++;
      }
      memory_buffer_free(searchvalue);
      while ((status = pkgdb_sql_query_next_row(sqlresult)) == SQLITE_ROW) {
        switch (searchtype) {
          case package_name_exact:
          case package_name:
          case package_info:
            if (!shortoutput) {
              if (count++ > 0)
                portcolcon_printf(con, "\n");
              portcolcon_printf_in_color(con, PORTCOLCON_COLOR_BRIGHT_CYAN, PORTCOLCON_COLOR_CYAN, "[");
              portcolcon_printf_in_color(con, PORTCOLCON_COLOR_WHITE, PORTCOLCON_COLOR_CYAN, "%s", sqlite3_column_text(sqlresult, 0));
              portcolcon_printf_in_color(con, PORTCOLCON_COLOR_BRIGHT_CYAN, PORTCOLCON_COLOR_CYAN, "]");
              portcolcon_printf(con, "\n");
              for (i = 0; i < PACKAGE_METADATA_TOTAL_FIELDS; i++) {
                s = (char*)sqlite3_column_text(sqlresult, i);
                portcolcon_printf_in_color(con, PORTCOLCON_COLOR_CYAN, PORTCOLCON_COLOR_IGNORE, "%*s: ", -15, package_metadata_field_name[i]);
                if (s) {
                  if (i == PACKAGE_METADATA_INDEX_BASENAME || (searchtype == package_info && (i == PACKAGE_METADATA_INDEX_NAME || i == PACKAGE_METADATA_INDEX_DESCRIPTION)))
                    portcolcon_write_multiple_highlights(con, s, paramlist, 0, PORTCOLCON_COLOR_YELLOW, PORTCOLCON_COLOR_BLUE);
                  else
                    portcolcon_write(con, s);
                }
                portcolcon_write(con, "\n");
              }
            } else {
              s = (char*)sqlite3_column_text(sqlresult, PACKAGE_METADATA_INDEX_BASENAME);
              portcolcon_set_foreground(con, PORTCOLCON_COLOR_WHITE);
              portcolcon_write_multiple_highlights(con, s, paramlist, 0, PORTCOLCON_COLOR_YELLOW, PORTCOLCON_COLOR_BLUE);
              portcolcon_reset_color(con);
              portcolcon_write(con, " - ");
              if ((s = (char*)sqlite3_column_text(sqlresult, PACKAGE_METADATA_INDEX_VERSION)) != NULL) {
                portcolcon_set_foreground(con, PORTCOLCON_COLOR_CYAN);
                portcolcon_write_multiple_highlights(con, s, paramlist, 0, PORTCOLCON_COLOR_YELLOW, PORTCOLCON_COLOR_BLUE);
                portcolcon_reset_color(con);
              }
              portcolcon_write(con, " - ");
              if ((s = (char*)sqlite3_column_text(sqlresult, PACKAGE_METADATA_INDEX_NAME)) != NULL) {
                portcolcon_set_foreground(con, PORTCOLCON_COLOR_GREEN);
                portcolcon_write_multiple_highlights(con, s, paramlist, 0, PORTCOLCON_COLOR_YELLOW, PORTCOLCON_COLOR_BLUE);
                portcolcon_reset_color(con);
              }
              portcolcon_write(con, " - ");
              if ((s = (char*)sqlite3_column_text(sqlresult, PACKAGE_METADATA_INDEX_DESCRIPTION)) != NULL) {
                portcolcon_set_foreground(con, PORTCOLCON_COLOR_DARK_GRAY);
                portcolcon_write_multiple_highlights(con, s, paramlist, 0, PORTCOLCON_COLOR_YELLOW, PORTCOLCON_COLOR_BLUE);
                portcolcon_reset_color(con);
              }
              portcolcon_printf(con, "\n");
            }
            break;
          case file_name_only:
          case folder_name_only:
            s = (char*)sqlite3_column_text(sqlresult, 1);
            if ((t = strrchr(s, '/')) == NULL) {
              t = s;
            } else if (*++t == 0) {
              if (searchtype == folder_name_only) {
                t--;
                while (t != s && *(t - 1) != '/')
                  t--;
              } else {
                break;
              }
            }
            i = 0;
            valueindex = 0;
            while (paramlist[valueindex]) {
              if (strstr(t, paramlist[valueindex])) {
                i++;
                break;
              }
              valueindex++;
            }
            if (i == 0)
              break;
            if (!shortoutput)
              portcolcon_printf_in_color(con, PORTCOLCON_COLOR_CYAN, PORTCOLCON_COLOR_IGNORE, "%s: ", (char*)sqlite3_column_text(sqlresult, 0));
            if (fullpath)
              portcolcon_printf(con, "%s/", dstdir);
            portcolcon_printf(con, "%.*s", (int)(t - s), s);
            portcolcon_write_multiple_highlights(con, t, paramlist, 0, PORTCOLCON_COLOR_YELLOW, PORTCOLCON_COLOR_BLUE);
            portcolcon_write(con, "\n");
            break;
          case file_name:
          case folder_name:
            s = (char*)sqlite3_column_text(sqlresult, 1);
            if (!shortoutput)
              portcolcon_printf_in_color(con, PORTCOLCON_COLOR_CYAN, PORTCOLCON_COLOR_IGNORE, "%s: ", (char*)sqlite3_column_text(sqlresult, 0));
            if (fullpath)
              portcolcon_printf(con, "%s/", dstdir);
            portcolcon_write_multiple_highlights(con, s, paramlist, 0, PORTCOLCON_COLOR_YELLOW, PORTCOLCON_COLOR_BLUE);
            portcolcon_write(con, "\n");
          case package_files:
          case package_folders:
            s = (char*)sqlite3_column_text(sqlresult, 1);
            if (!shortoutput)
              portcolcon_printf_in_color(con, PORTCOLCON_COLOR_CYAN, PORTCOLCON_COLOR_IGNORE, "%s: ", (char*)sqlite3_column_text(sqlresult, 0));
            if (fullpath)
              portcolcon_printf(con, "%s/", dstdir);
            portcolcon_printf(con, "%s\n", s);
            break;
        }
      }
      sqlite3_finalize(sqlresult);
    } else {
      fprintf(stderr, "Error %i (%s) in SQL:\n%s\n", status, sqlite3_errstr(status), memory_buffer_get(sql));/////
    }
    memory_buffer_free(sql);
  }

  //clean up
  pkgdb_close(db);
  free(paramlist);
  free(dstdir);
  portcolcon_cleanup(con);

  return 0;
}
/////TO DO: delete folders not used by any other package (what if they are not empty?)
