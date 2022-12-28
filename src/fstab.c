#include "fstab.h"
#include "filesystem.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <limits.h>
#ifdef _WIN32
#define realpath(N,R) _fullpath((R),(N),_MAX_PATH)
#if defined(_WIN32) && !defined(__MINGW64_VERSION_MAJOR)
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif
#endif

int remove_path_levels (char* path, int levels)
{
  size_t pathlen;
  int levelsremoved = 0;
  if (!path || !*path)
    return 0;
  pathlen = strlen(path);
  while (pathlen && levelsremoved < levels) {
    while (--pathlen > 0 && path[pathlen] != '/'
#ifdef _WIN32
      && path[pathlen] != '\\'
#endif
    )
      ;
    levelsremoved++;
  }
  path[pathlen] = 0;
  return levelsremoved;
}

char* get_fstab_path ()
{
#ifdef _WIN32
  const char* pathlist;
  if ((pathlist = getenv("PATH")) != NULL) {
    const char* currentpath;
    size_t currentpathlen;
    const char* nextseparator;
    currentpath = pathlist;
    while (currentpath) {
      currentpathlen = ((nextseparator = strchr(currentpath, ';')) ? nextseparator - currentpath : strlen(currentpath));
      if (*currentpath && *currentpath != '.') {
        char* fstabfilename;
        if ((fstabfilename = (char*)malloc(currentpathlen + 17)) != NULL) {
          char full_path[PATH_MAX];
          memcpy(fstabfilename, currentpath, currentpathlen);
          strcpy(fstabfilename + currentpathlen, "/../etc/fstab");
          if (file_exists(fstabfilename)) {
            if (realpath(fstabfilename, full_path)) {
              free(fstabfilename);
              fstabfilename = strdup(full_path);
            }
            return fstabfilename;
          }
          strcpy(fstabfilename + currentpathlen, "/../../etc/fstab");
          if (file_exists(fstabfilename)) {
            if (realpath(fstabfilename, full_path)) {
              free(fstabfilename);
              fstabfilename = strdup(full_path);
            }
            return fstabfilename;
          }
          free(fstabfilename);
        }
      }
      if (!nextseparator)
        break;
      currentpath = nextseparator + 1;
    }

  }
  return NULL;
#else
  return strdup("/etc/fstab");
#endif
}

void cleanup_fstab_data (struct fstab_data_struct* fstabdata)
{
  struct fstab_data_struct* next;
  struct fstab_data_struct* current = fstabdata;
  while (current) {
    next = current->next;
    free(current->fullpath);
    free(current->mountpath);
    free(current);
    current = next;
  }
}

struct fstab_data_struct* fstab_data_new_entry (const char* fullpath, size_t fullpathlen, const char* mountpath, size_t mountpathlen, struct fstab_data_struct* next)
{
  struct fstab_data_struct* entry;
  if ((entry = (struct fstab_data_struct*)malloc(sizeof(struct fstab_data_struct))) != NULL) {
    /////TO DO: unescape space when written as \040
    entry->fullpath = strdup(fullpath);
    entry->fullpathlen = fullpathlen;
    entry->mountpath = strdup(mountpath);
    entry->mountpathlen = mountpathlen;
    entry->next = next;
  }
  return entry;
}

struct fstab_data_struct* read_fstab_data (const char* fstabpath)
{
  struct fstab_data_struct* result;
  FILE* fstab;
  char* line;
  if (!fstabpath || !*fstabpath)
    return NULL;
  result = NULL;
  //add entries from /etc/fstab
  if ((fstab = fopen(fstabpath, "rb")) != NULL) {
    char* p;
    char* q;
    char* fullpath;
    size_t fullpathlen;
    char* mountpath;
    size_t mountpathlen;
    struct fstab_data_struct** entry = &result;
    while ((line = readline_from_file(fstab)) != NULL) {
      fullpath = NULL;
      mountpath = NULL;
      mountpathlen = 0;
      //skip spaces
      p = line;
      while (*p && isspace(*p) && *p != '#')
        p++;
      //skip comment
      if (*p && *p != '#') {
        //locate end of first parameter
        q = p;
        while (*q && !isspace(*q) && *q != '#')
          q++;
        if (*q && *q != '#') {
          *q = 0;
          fullpath = p;
          fullpathlen = q - p;
          //skip spaces to second parameter
          p = q + 1;
          while (*p && isspace(*p) && *p != '#')
            p++;
          if (*p && *p != '#') {
            //locate end of second parameter
            q = p;
            while (*q && !isspace(*q) && *q != '#')
              q++;
            if (q > p) {
              *q = 0;
              mountpath = p;
              mountpathlen = q - p;
            }
          }
        }
        if (fullpath && *fullpath && mountpath && *mountpath && strcmp(fullpath, "none") != 0) {
          if ((*entry = fstab_data_new_entry(fullpath, fullpathlen, mountpath, mountpathlen, NULL)) != NULL)
            entry = &(*entry)->next;
        }
      }
      //clean up
      free(line);
    }
    fclose(fstab);
#ifdef _WIN32
    //add /home path based on location of /etc/fstab
    {
      char rootpath[PATH_MAX];
      if (realpath(fstabpath, rootpath)) {
        if (remove_path_levels(rootpath, 2) == 2) {
          strcat(rootpath, "\\home");
          if ((*entry = fstab_data_new_entry(rootpath, strlen(rootpath), "/home", 5, NULL)) != NULL)
            entry = &(*entry)->next;
        }
      }
    }
#endif
  }
  return result;
};

char* fstabpath_from_fullpath (const struct fstab_data_struct* fstabdata, const char* fullpath)
{
  const struct fstab_data_struct* entry;
  size_t fullpathlen;

  char real_path[PATH_MAX];
  if (!fullpath || !*fullpath)
    return NULL;
  if (realpath(fullpath, real_path))
    fullpath = real_path;
  fullpathlen = strlen(fullpath);
  entry = fstabdata;
  while (entry) {
    if (fullpathlen >= entry->fullpathlen && strncasecmp(fullpath, entry->fullpath, entry->fullpathlen) == 0) {
      char* result;
      if ((result = (char*)malloc(entry->mountpathlen + fullpathlen - entry->fullpathlen + 1)) != NULL) {
        memcpy(result, entry->mountpath, entry->mountpathlen);
        strcpy(result + entry->mountpathlen, fullpath + entry->fullpathlen);
        return result;
      }
    }
    entry = entry->next;
  }
  return NULL;
}

char* fstabpath_to_fullpath (const struct fstab_data_struct* fstabdata, const char* mountpath)
{
  const struct fstab_data_struct* entry;
  size_t mountpathlen;
  if (!mountpath || !*mountpath)
    return NULL;
  mountpathlen = strlen(mountpath);
  entry = fstabdata;
  while (entry) {
    if (mountpathlen >= entry->mountpathlen && strncasecmp(mountpath, entry->mountpath, entry->mountpathlen) == 0) {
      char* result;
      if ((result = (char*)malloc(entry->fullpathlen + mountpathlen - entry->mountpathlen + 10)) != NULL) {
        memcpy(result, entry->fullpath, entry->fullpathlen);
        strcpy(result + entry->fullpathlen, mountpath + entry->mountpathlen);
        return result;
      }
    }
    entry = entry->next;
  }
  return NULL;
}

/////TO DO: /etc/fstab.d (per user))
