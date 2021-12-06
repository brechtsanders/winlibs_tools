#include "filesystem.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define OS_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define OS_MKDIR(path) mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
#endif
#include <errno.h>

int file_exists (const char* path)
{
  struct stat statbuf;
  if (stat(path, &statbuf) == 0 && S_ISREG(statbuf.st_mode))
    return 1;
  return 0;
}

int folder_exists (const char* path)
{
  struct stat statbuf;
  if (stat(path, &statbuf) == 0 && S_ISDIR(statbuf.st_mode))
    return 1;
  return 0;
}

//returns 0 on success, 1 if the directory already exists or -1 on error
int recursive_mkdir (const char* path)
{
  char* p;
  char* q;
  //try to create deepest level directory
  if (OS_MKDIR(path) == 0)
    return 0;
  if (errno == EEXIST)
    return 1;
  //find last separator
  p = strrchr(path, '/');
#ifdef _WIN32
  q = strrchr(path, '\\');
  if (q && (!p || q > p))
    p = q;
#endif
  //skip additional separators (if any)
  while (p && *p && p > path && (*(p - 1) == '/'
#ifdef _WIN32
    || *(p - 1) == '\\'
#endif
  ))
    p--;
  //recurse to create missing upper level(s)
  if (p && *p) {
    int result;
    size_t len = p - path;
    if ((q = (char*)malloc(len + 1)) == NULL)
      return -1;
    memcpy(q, path, len);
    q[len] = 0;
    result = recursive_mkdir(q);
    free(q);
    if (result >= 0) {
      if (OS_MKDIR(path) != 0) {
        if (errno == EEXIST)
          return 1;
        return -1;
      }
      return 0;
    }
  }
  return -1;
}

int delete_file (const char* path)
{
  if (!file_exists(path))
    return 1;
  if (unlink(path) != 0)
      return -1;
  return 0;
}

char* readline (FILE* f)
{
  int datalen;
  char data[128];
  int resultlen = 0;
  char* result = NULL;
  while (fgets(data, sizeof(data), f)) {
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

int write_to_file (const char* path, const char* data)
{
  FILE* dst;
  if (!path || !*path)
    return 1;
  if ((dst = fopen(path, "wb")) == NULL)
    return -1;
  if (data)
    fprintf(dst, "%s\n", data);
  fclose(dst);
  return 0;
}
