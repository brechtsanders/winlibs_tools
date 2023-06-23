#include "winlibs_common.h"
#include "pkgfile.h"
#include "filesystem.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

void set_var (char** var, const char* value)
{
  if (!var)
    return;
  if (*var)
    free(*var);
  char* result = strdup(value);
  char* p = result;
  while (*p) {
    if (*p == '\\') {
      memmove(p, p + 1, strlen(p + 1) + 1);
    }
    if (*p == '\"') {
      memmove(p, p + 1, strlen(p + 1) + 1);
    } else {
      p++;
    }
  }
  *var = result;
}

////////////////////////////////////////////////////////////////////////

struct packageinfo_file_struct {
  FILE* handle;
  time_t lastchanged;
};

packageinfo_file open_packageinfo_file (const char* infopath, const char* basename)
{
  const char* p;
  const char* q;
  struct stat statbuf;
  FILE* handle;
  size_t path_len;
  char* fullpath;
  packageinfo_file result = NULL;
  //abort on invalid parameters
  if (infopath == NULL || basename == NULL)
    return NULL;
  //go through each path in (semi)colon-separated list
  p = infopath;
  while (!result && p && *p) {
    //get separate path
    if ((q = strchr(p, PATHLIST_SEPARATOR)) == NULL) {
      q = p + strlen(p);
    }
    //process item
    if (p && *p) {
      //determine full path
      path_len = q - p;
      fullpath = (char*)malloc(path_len + strlen(basename) + strlen(PACKAGE_RECIPE_EXTENSION) + 2);
      memcpy(fullpath, p, path_len);
      fullpath[path_len] = PATH_SEPARATOR;
      strcpy(fullpath + path_len + 1, basename);
      strcat(fullpath + path_len + 1, PACKAGE_RECIPE_EXTENSION);
      //check if file exists
      if (stat(fullpath, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
        //open file
        if ((handle = fopen(fullpath, "rb")) != NULL) {
          result = (packageinfo_file)malloc(sizeof(struct packageinfo_file_struct));
          result->handle = handle;
          result->lastchanged = 0;
          //keep last file access time
          if (statbuf.st_mtime)
            result->lastchanged = statbuf.st_mtime;
          if (statbuf.st_ctime > result->lastchanged)
            result->lastchanged = statbuf.st_ctime;
        }
      }
      free(fullpath);
    }
    //point to next path in list
    p = (*q ? q + 1 : NULL);
  }
  return result;
}

void close_packageinfo_file (packageinfo_file pkgfile)
{
  if (pkgfile) {
    fclose(pkgfile->handle);
    free(pkgfile);
  }
}

char* packageinfo_file_readline (packageinfo_file pkgfile)
{
  int datalen;
  char data[128];
  int resultlen = 0;
  char* result = NULL;
  while (fgets(data, sizeof(data), pkgfile->handle)) {
    datalen = strlen(data);
    result = (char*)realloc(result, resultlen + datalen + 1);
    memcpy(result + resultlen, data, datalen + 1);
    resultlen += datalen;
    if (resultlen > 0 && result[resultlen - 1] == '\n') {
      result[--resultlen] = 0;
      if (resultlen > 0 && result[resultlen - 1] == '\r')
        result[--resultlen] = 0;
      break;
    }
  }
  return result;
}

int check_packageinfo_paths (const char* infopath)
{
  const char* p;
  const char* q;
  char* s;
  int count_good = 0;
  int count_bad = 0;
  //go through each path in (semi)colon-separated list
  p = infopath;
  while (p && *p) {
    //get separate path
    if ((q = strchr(p, PATHLIST_SEPARATOR)) == NULL) {
      s = strdup(p);
    } else {
      s = (char*)malloc(q - p + 1);
      memcpy(s, p, q - p);
      s[q - p] = 0;
    }
    //process item
    if (s && *s) {
      if (folder_exists(s))
        count_good++;
      else
        count_bad++;
    }
    //point to next path in list
    free(s);
    p = (q ? q + 1 : NULL);
  }
  if (count_good == 0)
    return 0;
  if (count_bad > 0)
    return count_bad;
  return -count_good;
}

struct package_metadata_struct* read_packageinfo (const char* infopath, const char* basename)
{
  packageinfo_file pkgfile;
  char* line;
  size_t linenumber = 0;
  struct package_metadata_struct* info = NULL;
  //abort on invalid parameters
  if (infopath == NULL || basename == NULL)
    return NULL;
  //process file
  if ((pkgfile = open_packageinfo_file(infopath, basename)) != NULL) {
    //initialize data
    info = package_metadata_create();
    //process file
    while ((line = packageinfo_file_readline(pkgfile)) != NULL) {
      char* p;
      char* q;
      linenumber++;
      //detect and skip comment
      p = line;
      while (*p && isspace(*p))
        p++;
      if (*p != '#') {
        //detect variables
        if (memcmp(p, "export", 6) == 0 && isspace(p[6])) {
          p += 7;
          while (*p && isspace(*p))
            p++;
          if ((q = strchr(p, '=')) != NULL) {
            *q++ = 0;
            if (strcmp(p, "NAME") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_NAME]), q);
            else if (strcmp(p, "STATUS") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_STATUS]), q);
            else if (strcmp(p, "URL") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_URL]), q);
            else if (strcmp(p, "BASENAME") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_BASENAME]), q);
            else if (strcmp(p, "DESCRIPTION") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_DESCRIPTION]), q);
            else if (strcmp(p, "CATEGORY") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_CATEGORY]), q);
            else if (strcmp(p, "TYPE") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_TYPE]), q);
            else if (strcmp(p, "VERSION") == 0) {
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_VERSION]), q);
              info->version_linenumber = linenumber;
            } else if (strcmp(p, "VERSIONDATE") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_VERSIONDATE]), q);
            else if (strcmp(p, "DEPENDENCIES") == 0 || strcmp(p, "DEPENDANCIES") == 0)
              sorted_unique_list_add_comma_separated_list(info->dependencies, q);
            else if (strcmp(p, "OPTIONALDEPENDENCIES") == 0 || strcmp(p, "OPTIONALDEPENDANCIES") == 0)
              sorted_unique_list_add_comma_separated_list(info->optionaldependencies, q);
            else if (strcmp(p, "BUILDDEPENDENCIES") == 0 || strcmp(p, "BUILDDEPENDANCIES") == 0)
              sorted_unique_list_add_comma_separated_list(info->builddependencies, q);
            else if (strcmp(p, "OPTIONALBUILDDEPENDENCIES") == 0 || strcmp(p, "OPTIONALBUILDDEPENDANCIES") == 0)
              sorted_unique_list_add_comma_separated_list(info->optionalbuilddependencies, q);
            else if (strcmp(p, "LICENSEFILE") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_LICENSEFILE]), q);
            else if (strcmp(p, "LICENSETYPE") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_LICENSETYPE]), q);
            else if (strcmp(p, "DOWNLOADURL") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_DOWNLOADURL]), q);
            else if (strcmp(p, "DOWNLOADSOURCEURL") == 0)
              set_var(&(info->datafield[PACKAGE_METADATA_INDEX_DOWNLOADSOURCEURL]), q);
          }
        } else if (memcmp(p, "~/makeDevPak.sh", 15) == 0 || memcmp(p, "wl-makepackage", 14) == 0) {
          info->buildok++;
          free(line);
          break;
        }
      }
      free(line);
    }
    close_packageinfo_file(pkgfile);
  }
  return info;
}

char* my_strndup (const char* data, size_t datalen)
{
  char* result;
  if (!data)
    return NULL;
  if ((result = (char*)malloc(datalen + 1)) == NULL)
    return NULL;
  memcpy(result, data, datalen);
  result[datalen] = 0;
  return result;
}

void get_package_downloadurl_info (struct package_metadata_struct* packageinfo, char** purl, char** pprefix, char** psuffix)
{
  const char* url = NULL;
  size_t urllen = 0;
  const char* prefix = NULL;
  size_t prefixlen = 0;
  const char* suffix = NULL;
  size_t suffixlen = 0;
  if (packageinfo->datafield[PACKAGE_METADATA_INDEX_DOWNLOADURL]) {
    const char* p;
    p = packageinfo->datafield[PACKAGE_METADATA_INDEX_DOWNLOADURL];
    url = p;
    while (*p && !isspace(*p))
      p++;
    urllen = p - url;
    if (*p) {
      p++;
      prefix = p;
      while (*p && !isspace(*p))
        p++;
      prefixlen = p - prefix;
      if (*p) {
        p++;
        suffix = p;
        while (*p && !isspace(*p))
          p++;
        suffixlen = p - suffix;
      }
    }
  }
  if (purl)
    *purl = my_strndup(url, urllen);
  if (pprefix)
    *pprefix = my_strndup(prefix, prefixlen);
  if (psuffix)
    *psuffix = my_strndup(suffix, suffixlen);
}

////////////////////////////////////////////////////////////////////////

size_t iterate_packages (const char* infopath, package_callback_fn callback, void* callbackdata)
{
  const char* p;
  const char* q;
  char* s;
  DIR* dir;
  struct dirent *dp;
  size_t namelen;
  char* basename;
  int abort = 0;
  size_t count = 0;
  const size_t pkgextlen = strlen(PACKAGE_RECIPE_EXTENSION);
  //go through each path in (semi)colon-separated list
  p = infopath;
  while (p && *p) {
    //get separate path
    if ((q = strchr(p, PATHLIST_SEPARATOR)) == NULL) {
      s = strdup(p);
    } else {
      s = (char*)malloc(q - p + 1);
      memcpy(s, p, q - p);
      s[q - p] = 0;
    }
    //process item
    if (s && *s) {
      if ((dir = opendir(s)) != NULL) {
        while (!abort && (dp = readdir(dir)) != NULL) {
          namelen = strlen(dp->d_name);
          if (!(dp->d_name[0] == '.' && (dp->d_name[1] == 0 || (dp->d_name[1] == '.' && dp->d_name[2] == 0))) && namelen > pkgextlen && strcmp(dp->d_name + namelen - pkgextlen, PACKAGE_RECIPE_EXTENSION) == 0) {
            basename = (char*)malloc(namelen - pkgextlen + 1);
            memcpy(basename, dp->d_name, namelen - pkgextlen);
            basename[namelen - pkgextlen] = 0;
            count++;
            if (callback)
              abort = (*callback)(basename, callbackdata);
            free(basename);
          }
        }
        closedir(dir);
      }
    }
    //point to next path in list
    free(s);
    p = (q ? q + 1 : NULL);
  }
  return count;
}

/*
int iterate_packages_in_comma_separated_list (const char* list, package_callback_fn callback, void* callbackdata)
{
  const char* p;
  const char* q;
  char* entry;
  size_t entrylen;
  int result;
  p = list;
  while (p && *p) {
    q = p;
    while (*q && *q != ',')
      q++;
    if (q > p) {
      entrylen = q - p;
      if ((entry = (char*)malloc(entrylen + 1)) != NULL) {
        memcpy(entry, p, entrylen);
        entry[entrylen] = 0;
        result = (*callback)(entry, callbackdata);
        free(entry);
        if (result)
          return result;
      }
      p = q;
    }
    if (*p)
      p++;
  }
  return 0;
}
*/

int iterate_packages_in_list (const sorted_unique_list* sortuniqlist, package_callback_fn callback, void* callbackdata)
{
  int result;
  unsigned int i;
  unsigned int n;
  if (sortuniqlist) {
    n = sorted_unique_list_size(sortuniqlist);
    for (i = 0; i < n; i++) {
      if ((result = (*callback)(sorted_unique_list_get(sortuniqlist, i), callbackdata)) != 0)
        return result;
    }
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////

#if 0
void insert_packageinfo_sorted_by_basename (struct package_info_list_struct** packagelist, struct package_metadata_struct* packageinfo)
{
  struct package_info_list_struct* nextpackage;
  struct package_info_list_struct** currentpackage = packagelist;
  while (*currentpackage && strcasecmp((*currentpackage)->info->basename, packageinfo->basename) <= 0) {
    currentpackage = &(*currentpackage)->next;
  }
  nextpackage = *currentpackage;
  *currentpackage = (struct package_info_list_struct*)malloc(sizeof(struct package_info_list_struct));
  (*currentpackage)->info = packageinfo;
  (*currentpackage)->next = nextpackage;
}
#endif

void free_packageinfolist (struct package_info_list_struct* packagelist)
{
  struct package_info_list_struct* p;
  struct package_info_list_struct* currentpackage = packagelist;
  while (currentpackage) {
    p = currentpackage->next;
    package_metadata_free(currentpackage->info);
    free(currentpackage);
    currentpackage = p;
  }
}

struct package_info_list_struct* search_packageinfolist_by_basename (struct package_info_list_struct* packagelist, const char* basename)
{
  //abort on invalid parameters
  if (packagelist == NULL || basename == NULL)
    return NULL;
  struct package_info_list_struct* currentpackage = packagelist;
  while (currentpackage && strcmp(currentpackage->info->datafield[PACKAGE_METADATA_INDEX_BASENAME], basename) != 0) {
    currentpackage = currentpackage->next;
  }
  return currentpackage;
}

/*
int basename_is_in_commaseparatedlist (const char* basename, const char* commaseparatedlist)
{
  //abort on invalid parameters
  if (basename == NULL || commaseparatedlist == NULL)
    return -1;
  //find position
  int startpos = 0;
  int endpos;
  if (commaseparatedlist) {
    while (commaseparatedlist[startpos]) {
      endpos = startpos;
      while (commaseparatedlist[endpos] && commaseparatedlist[endpos] != ',')
        endpos++;
      if (endpos > startpos) {
        if (strncmp(commaseparatedlist + startpos, basename, endpos - startpos) == 0)
          return startpos;
      }
      startpos = endpos;
      if (commaseparatedlist[startpos])
        startpos++;
    }
  }
  return -1;
}

int remove_basename_from_commaseparatedlist (const char* basename, char* commaseparatedlist)
{
  //abort on invalid parameters
  if (basename == NULL || commaseparatedlist == NULL)
    return -1;
  //find position
  int pos = basename_is_in_commaseparatedlist(basename, commaseparatedlist);
  if (pos >= 0) {
    char* commapos;
    if ((commapos = strchr(commaseparatedlist + pos, ',')) == NULL) {
      if (pos > 0)
        commaseparatedlist[pos - 1] = 0;
    } else {
      memmove(commaseparatedlist + pos, commapos + 1, strlen(commapos));
    }
  }
  return pos;
}
*/

int insert_packages_by_dependency (struct package_info_list_struct** packagelist, const char* infopath, const sorted_unique_list* packages)
{
  unsigned int i;
  unsigned int n = sorted_unique_list_size(packages);
  int result = 0;
  for (i = 0; i < n; i++) {
    if (insert_package_by_dependency(packagelist, infopath, sorted_unique_list_get(packages, i)))
      result++;
  }
  return result;
}

/*
int insert_packages_by_dependency (struct package_info_list_struct** packagelist, const char* infopath, const char* packagenames)
{
  int result = 0;
  if (packagenames && *packagenames) {
    char* packagenamelist = strdup(packagenames);
    char* p = packagenamelist;
    char* q;
    char c;
    do {
      q = p;
      while (*q && *q != ',')
        q++;
      c = *q;
      *q = 0;
      if (insert_package_by_dependency(packagelist, infopath, p))
        result++;
      p = q;
      if (c)
        p++;
    } while (c);
    free(packagenamelist);
  }
  return result;
}
*/

#if 0
int insert_packages_by_dependency (struct package_info_list_struct** packagelist, const char* infopath, char* packagenamelist)
{
  int result = 0;
  if (packagenamelist && *packagenamelist) {
    char* p = packagenamelist;
    char* q;
    char c;
    do {
      q = p;
      while (*q && *q != ',')
        q++;
      c = *q;
      *q = 0;
      if (insert_package_by_dependency(packagelist, infopath, p)) {
        result++;
        if (c)
          memmove(p, q + 1, strlen(q + 1) + 1);
        else
          *p = 0;
      } else {
        if (p > packagenamelist)
          *(p - 1) = ',';
        p = q + 1;
      }
    } while (c);
  }
  return result;
}
#endif

int insert_package_by_dependency (struct package_info_list_struct** packagelist, const char* infopath, const char* basename)
{
  struct package_metadata_struct* info;
  struct package_info_list_struct* nextpackage;
  struct package_info_list_struct** currentpackage = packagelist;
  //abort if package name is empty
  if (!basename || !*basename)
    return -1;
  //abort if this package is already in the list
  if (search_packageinfolist_by_basename(*packagelist, basename) != NULL)
    return 0;
  //abort if no valid package information was found
  if ((info = read_packageinfo(infopath, basename)) == NULL)
    return -3;
  if (!info->datafield[PACKAGE_METADATA_INDEX_BASENAME] || !*info->datafield[PACKAGE_METADATA_INDEX_BASENAME]) {
    package_metadata_free(info);
    return -4;
  }
  //determine insert position (after last dependency and before first dependent package)
  //determine list of dependencies
  sorted_unique_list* remaining_dependencies = sorted_unique_list_duplicate(info->dependencies, NULL);
  sorted_unique_list* remaining_optionaldependencies = sorted_unique_list_duplicate(info->optionaldependencies, NULL);
  sorted_unique_list* remaining_builddependencies = sorted_unique_list_duplicate(info->builddependencies, NULL);
  sorted_unique_list* remaining_optionalbuilddependencies = sorted_unique_list_duplicate(info->optionalbuilddependencies, NULL);
  //determine insert position
  while (*currentpackage) {
    //check if current package is in list of dependencies and remove if found
    sorted_unique_list_remove(remaining_dependencies, (*currentpackage)->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
    sorted_unique_list_remove(remaining_optionaldependencies, (*currentpackage)->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
    sorted_unique_list_remove(remaining_builddependencies, (*currentpackage)->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
    //if all dependencies are met and the minimal insert position is reached, insert in alfabetical order
    if (sorted_unique_list_size(remaining_dependencies) == 0 && sorted_unique_list_size(remaining_optionaldependencies) == 0 && sorted_unique_list_size(remaining_builddependencies) == 0 && sorted_unique_list_size(remaining_optionalbuilddependencies) == 0 && strcasecmp((*currentpackage)->info->datafield[PACKAGE_METADATA_INDEX_BASENAME], basename) >= 0)
      break;
    //if a dependent package is reached, insert before
    if (sorted_unique_list_find((*currentpackage)->info->dependencies, basename) >= 0 ||
        //sorted_unique_list_find((*currentpackage)->info->optionaldependencies, basename) >= 0 ||  /////TO DO: shouldn't this only be done under certain conditions?
        //sorted_unique_list_find((*currentpackage)->info->optionalbuilddependencies, basename) >= 0 ||  /////TO DO: shouldn't this only be done under certain conditions?
        sorted_unique_list_find((*currentpackage)->info->builddependencies, basename) >= 0) {
#if 1
      if (sorted_unique_list_size(remaining_dependencies) > 0 || /*sorted_unique_list_size(remaining_optionaldependencies) > 0 ||*/ sorted_unique_list_size(remaining_builddependencies) > 0) {
        //if any dependencies still appear after this position there is probably a cyclic dependency problem
        struct package_info_list_struct* nextpackage = *currentpackage;
        while ((nextpackage = nextpackage->next) != NULL) {
          if (sorted_unique_list_find(remaining_dependencies, nextpackage->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]) >= 0 ||
              /*sorted_unique_list_find(remaining_optionaldependencies, nextpackage->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]) >= 0 ||*/
              /*sorted_unique_list_find(remaining_optionalbuilddependencies, nextpackage->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]) >= 0 ||*/
              sorted_unique_list_find(remaining_builddependencies, nextpackage->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]) >= 0)
            break;
        }
        if (nextpackage) {
          fprintf(stderr, "Cyclic dependency detected for %s and %s\n", basename, nextpackage->info->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
          fprintf(stderr, " - remaining dependencies: ");
          fprintf(stderr, "\n");
          sorted_unique_list_print(remaining_dependencies, ",");
          fprintf(stderr, " - remaining optional dependencies: ");
          fprintf(stderr, "\n");
          sorted_unique_list_print(remaining_optionaldependencies, ",");
          fprintf(stderr, " - remaining build dependencies: ");
          fprintf(stderr, "\n");
          sorted_unique_list_print(remaining_builddependencies, ",");
        }
      }
#endif
      break;
    }
    currentpackage = &(*currentpackage)->next;
  }
  free(remaining_dependencies);
  free(remaining_optionaldependencies);
  free(remaining_builddependencies);
  //insert package information
  nextpackage = *currentpackage;
  *currentpackage = (struct package_info_list_struct*)malloc(sizeof(struct package_info_list_struct));
  (*currentpackage)->info = info;
  (*currentpackage)->next = nextpackage;
  //insert dependencies
  insert_packages_by_dependency(packagelist, infopath, info->dependencies);
  insert_packages_by_dependency(packagelist, infopath, info->optionaldependencies);
  insert_packages_by_dependency(packagelist, infopath, info->builddependencies);
  insert_packages_by_dependency(packagelist, infopath, info->optionalbuilddependencies);
/*
  //recursively add dependencies
  int startpos;
  int endpos;
  const char* dependencies[] = {info->dependencies, info->builddependencies, NULL};//to do: info->optionaldependencies
  const char** dependency = dependencies;
  while (*dependency) {
    startpos = 0;
    while ((*dependency)[startpos]) {
      endpos = startpos;
      while ((*dependency)[endpos] && (*dependency)[endpos] != ',')
        endpos++;
      if (endpos > startpos) {
        char* depname = (char*)malloc(endpos - startpos + 1);
        memcpy(depname, (*dependency) + startpos, endpos - startpos);
        depname[endpos - startpos] = 0;
        insert_package_by_dependency(packagelist, infopath, depname);
        free(depname);
      }
      startpos = endpos;
      if ((*dependency)[startpos])
        startpos++;
    }
    dependency++;
  }
*/
  return 0;
}

int package_is_installed (const char* installpath, const char* basename)
{
  struct stat statbuf;
  char* fullpath;
  int result = 0;
  if (!installpath || !*installpath || !basename || !*basename)
    return 0;
  if ((fullpath = (char*)malloc(strlen(installpath) + strlen(basename) + 19)) == NULL)
    return 0;
  strcpy(fullpath, installpath);
  strcat(fullpath, WINLIBS_CHR2STR(PATH_SEPARATOR) "var" WINLIBS_CHR2STR(PATH_SEPARATOR) "lib" WINLIBS_CHR2STR(PATH_SEPARATOR) "packages" WINLIBS_CHR2STR(PATH_SEPARATOR));
  strcat(fullpath, basename);
  if (stat(fullpath, &statbuf) == 0 && S_ISDIR(statbuf.st_mode))
    result = 1;
  free(fullpath);
  return result;
}

/*
int packages_are_installed (const char* installpath, const char* packagelist)
{
  char* deps;
  char* p;
  char* q;
  char c;
  int result = 0;
  if (!installpath || !*installpath || !packagelist || !*packagelist)
    return 0;
  deps = strdup(packagelist);
  p = deps;
  do {
    q = p;
    while (*q && *q != ',')
      q++;
    c = *q;
    *q = 0;
    if (*p) {
      if (!package_is_installed(installpath, p)) {
        result = 0;
        break;
      }
      result++;
    }
    p = q;
    if (c)
      p++;
  } while (c);
  free(deps);
  return result;
}
*/

/*
struct packages_are_installed_struct {
  const char* installpath;
  int installed;
};

int packages_are_installed_callback (const char* basename, void* callbackdata)
{
  if (!package_is_installed(((struct packages_are_installed_struct*)callbackdata)->installpath, basename)) {
    ((struct packages_are_installed_struct*)callbackdata)->installed = 0;
    return -1;
  }
  ((struct packages_are_installed_struct*)callbackdata)->installed++;
  return 0;
}

int packages_are_installed (const char* installpath, const char* packagelist)
{
  struct packages_are_installed_struct data;
  if (!installpath || !*installpath)
    return 0;
  data.installpath = installpath;
  data.installed = 1;
  iterate_packages_in_comma_separated_list(packagelist, packages_are_installed_callback, &data);
  return data.installed;
}

char* installed_package_version (const char* installpath, const char* basename)
{
  struct stat statbuf;
  char* fullpath;
  char* result = NULL;
  if (!installpath || !*installpath || !basename || !*basename)
    return NULL;
  if ((fullpath = (char*)malloc(strlen(installpath) + strlen(basename) + 27)) == NULL)
    return NULL;
  strcpy(fullpath, installpath);
  strcat(fullpath, WINLIBS_CHR2STR(PATH_SEPARATOR) "var" WINLIBS_CHR2STR(PATH_SEPARATOR) "lib" WINLIBS_CHR2STR(PATH_SEPARATOR) "packages" WINLIBS_CHR2STR(PATH_SEPARATOR));
  strcat(fullpath, basename);
  if (stat(fullpath, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
    FILE* f;
    strcat(fullpath, WINLIBS_CHR2STR(PATH_SEPARATOR) "version");
    if ((f = fopen(fullpath, "rb")) != NULL) {
      char buf[64];
      size_t buflen = fread(buf, 1, sizeof(buf) - 1, f);
      fclose(f);
      if (buflen > 0) {
        char* pos;
        if ((pos = strchr(buf, '\r')) != 0)
          *pos = 0;
        if ((pos = strchr(buf, '\n')) != 0)
          *pos = 0;
        buf[buflen] = 0;
        result = strdup(buf);
      }
    }
  }
  free(fullpath);
  return result;
}
*/

time_t installed_package_lastchanged (const char* installpath, const char* basename)
{
  struct stat statbuf;
  char* fullpath;
  time_t result = 0;
  if (!installpath || !*installpath || !basename || !*basename)
    return 0;
  if ((fullpath = (char*)malloc(strlen(installpath) + strlen(basename) + 27)) == NULL)
    return 0;
  strcpy(fullpath, installpath);
  strcat(fullpath, WINLIBS_CHR2STR(PATH_SEPARATOR) "var" WINLIBS_CHR2STR(PATH_SEPARATOR) "lib" WINLIBS_CHR2STR(PATH_SEPARATOR) "packages" WINLIBS_CHR2STR(PATH_SEPARATOR));
  strcat(fullpath, basename);
  if (stat(fullpath, &statbuf) == 0 && S_ISDIR(statbuf.st_mode)) {
    strcat(fullpath, WINLIBS_CHR2STR(PATH_SEPARATOR) "version");
    if (stat(fullpath, &statbuf) == 0 && S_ISREG(statbuf.st_mode)) {
      //open file
      if (statbuf.st_mtime)
        result = statbuf.st_mtime;
      if (statbuf.st_ctime > result)
        result = statbuf.st_ctime;
    }
  }
  free(fullpath);
  return result;
}

int packages_remove_missing_from_list (const char* installpath, char* packagelist)
{
  char* p;
  char* q;
  char c;
  int result = 0;
  if (!installpath || !*installpath || !packagelist || !*packagelist)
    return 0;
  p = packagelist;
  do {
    q = p;
    while (*q && *q != ',')
      q++;
    c = *q;
    *q = 0;
    if (!package_is_installed(installpath, p)) {
      if (c) {
        strcpy(p, q + 1);
      } else {
        if (p == packagelist)
          *p = 0;
        else
          *(p - 1) = 0;
      }
      result++;
    } else {
      if (c)
        *q = c;
      p = q;
      if (c)
        p++;
    }
  } while (c);
  return result;
}
