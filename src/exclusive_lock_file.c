#include "exclusive_lock_file.h"
#include "winlibs_common.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <stdio.h>
#endif

#define LOCK_EXTENSION ".lock"
#define LOCK_RETRIES   59       //number if retries (not including first attempt)
#define LOCK_RETRYWAIT 1        //time between retries (in seconds)

struct exclusive_lock_file_struct {
  char* path;
  int filehandle;
};

static int is_directory (const char* path)
{
  struct stat statbuf;
  if (stat(path, &statbuf) < 0)
    return 0;
  return S_ISDIR(statbuf.st_mode);
}

exclusive_lock_file exclusive_lock_file_create (const char* directory, const char* basename, const char** errmsg, exclusive_lock_file_create_progress_callback_fn callback, void* callbackdata)
{
  struct exclusive_lock_file_struct* handle;
  size_t directorylen;
  size_t basenamelen;
  int retriesleft = LOCK_RETRIES;
  if (errmsg)
    *errmsg = NULL;
  if (!basename || !*basename) {
    if (errmsg)
      *errmsg = "Lock file base name missing";
    return NULL;
  }
  if (!directory || !*directory) {
    directory = ".";
  } else if (!is_directory(directory)) {
    if (errmsg)
      *errmsg = "Lock file destination is not a directory";
    return NULL;
  }
  //allocate structure
  if ((handle = (struct exclusive_lock_file_struct*)malloc(sizeof(struct exclusive_lock_file_struct))) == NULL) {
    if (errmsg)
      *errmsg = "Memory allocation error";
    return NULL;
  }
  //determine lock file name
  directorylen = strlen(directory);
  basenamelen = strlen(basename);
  if ((handle->path = (char*)malloc(directorylen + basenamelen + strlen(LOCK_EXTENSION) + 2)) == NULL) {
    if (errmsg)
      *errmsg = "Memory allocation error";
    free(handle);
    return NULL;
  }
  memcpy(handle->path, directory, directorylen);
  handle->path[directorylen] = PATH_SEPARATOR;
  memcpy(handle->path + directorylen + 1, basename, basenamelen);
  strcpy(handle->path + directorylen + 1 + basenamelen, LOCK_EXTENSION);
  //create lock file (retry multiple times if needed)
  do {
    if ((handle->filehandle = open(handle->path, O_WRONLY | O_CREAT | O_EXCL, S_IWUSR | S_IWGRP | S_IWOTH)) < 0) {
      if (errno != EEXIST) {
        if (errmsg)
          *errmsg = "Unable to create lock file";
      } else {
        /////TO DO: check if process that created lock file still exists
        if (errmsg)
          *errmsg = "Lock file already exists";
      }
      if (callback) {
        if ((*callback)(handle, callbackdata) != 0) {
          if (errmsg)
            *errmsg = "Unable to get lock and retry aborted";
          break;
        }
      }
#ifdef _WIN32
      Sleep(LOCK_RETRYWAIT * 1000);
#else
      sleep(LOCK_RETRYWAIT);
#endif
    } else {
      if (errmsg)
        *errmsg = NULL;
    }
  } while (handle->filehandle < 0 && retriesleft-- > 0);
  if (handle->filehandle < 0) {
    free(handle->path);
    free(handle);
    return NULL;
  }

  //write hostname to lock file
#ifdef _WIN32
  char hostname[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD hostnamelen = sizeof(hostname);
  if (!GetComputerNameA(hostname, &hostnamelen))
    hostname[0] = 0;
#else
#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 255
#endif
  char hostname[HOST_NAME_MAX + 1];
  hostname[0] = 0;
  gethostname(hostname, sizeof(hostname));
#endif
  write(handle->filehandle, hostname, strlen(hostname));
  write(handle->filehandle, "\n", 1);

  //write process id to lock file
  char buf[32];
#ifdef _WIN32
  DWORD pid = GetCurrentProcessId();
  lltoa((long long)pid, buf, 10);
#else
  pid_t pid = getpid();
  snprintf(buf, sizeof(buf), "%lli", (long long)pid);
#endif
  write(handle->filehandle, buf, strlen(buf));
  write(handle->filehandle, "\n", 1);

  return handle;
}

void exclusive_lock_file_destroy (exclusive_lock_file handle)
{
  if (handle) {
    close(handle->filehandle);
    if (unlink(handle->path) != 0) {
      //fprintf(stderr, "Error %i deleting lock file: %s\n", errno, handle->path);
    }
    free(handle->path);
    free(handle);
  }
}

const char* exclusive_lock_file_get_path (exclusive_lock_file handle)
{
  if (!handle)
    return NULL;
  return handle->path;
}
