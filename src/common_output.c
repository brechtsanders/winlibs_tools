#include "common_output.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

//#define PTHREAD_USE_MUTEX_AS_LOCK

struct commonoutput_stuct {
  FILE* logfile;
  int verbose;
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_t lock;
#else
  pthread_mutex_t lock;
#endif
};

struct commonoutput_stuct* commonoutput_create (const char* filename, int verbose)
{
  struct commonoutput_stuct* handle;
  FILE* dst;
  //try to open output file (or use standard output if NULL or empty)
  if (!filename || !*filename)
    dst = stdout;
  else if ((dst = fopen(filename, "a")) == NULL)
    return NULL;
  //initialize handle
  if ((handle = (struct commonoutput_stuct*)malloc(sizeof(struct commonoutput_stuct))) == NULL)
    return NULL;
  handle->logfile = dst;
  handle->verbose = verbose;
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  if (pthread_rwlock_init(&handle->lock, NULL/*PTHREAD_MUTEX_ERRORCHECK*/) != 0) {
#else
  if (pthread_mutex_init(&handle->lock, NULL) != 0) {
#endif
    fprintf(stderr, "Error initializing mutex\n");
    if (handle->logfile != stdout)
      fclose(handle->logfile);
    free(handle);
    return NULL;
  }
  return handle;
}

void commonoutput_free (struct commonoutput_stuct* handle)
{
  if (!handle)
    return;
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_wrlock(&handle->lock);
#else
  pthread_mutex_lock(&handle->lock);
#endif
  if (handle->logfile != stdout)
    fclose(handle->logfile);
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_unlock(&handle->lock);
  pthread_rwlock_destroy(&handle->lock);
#else
  pthread_mutex_unlock(&handle->lock);
  pthread_mutex_destroy(&handle->lock);
#endif
  free(handle);
}

void commonoutput_put (struct commonoutput_stuct* handle, int verbose, const char* data)
{
  if (!handle || !handle->logfile || verbose > handle->verbose || !data || !*data)
    return;
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_wrlock(&handle->lock);
#else
  pthread_mutex_lock(&handle->lock);
#endif
  fputs(data, handle->logfile);
  fflush(handle->logfile);
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_unlock(&handle->lock);
#else
  pthread_mutex_unlock(&handle->lock);
#endif
}

void commonoutput_putbuf (struct commonoutput_stuct* handle, int verbose, const char* data, size_t datalen)
{
  if (!handle || !handle->logfile || verbose > handle->verbose || !data || !*data)
    return;
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_wrlock(&handle->lock);
#else
  pthread_mutex_lock(&handle->lock);
#endif
  fwrite(data, 1, datalen, handle->logfile);
  fflush(handle->logfile);
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_unlock(&handle->lock);
#else
  pthread_mutex_unlock(&handle->lock);
#endif
}

int commonoutput_vprintf (struct commonoutput_stuct* handle, int verbose, const char* format, va_list args)
{
  int result;
  if (!handle || !handle->logfile)
    return -1;
  if (verbose > handle->verbose)
    return 0;
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_wrlock(&handle->lock);
#else
  pthread_mutex_lock(&handle->lock);
#endif
  result = vfprintf(handle->logfile, format, args);
  fflush(handle->logfile);
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_unlock(&handle->lock);
#else
  pthread_mutex_unlock(&handle->lock);
#endif
  return result;
}

int commonoutput_printf (struct commonoutput_stuct* handle, int verbose, const char* format, ...)
{
  int result;
  va_list args;
  va_start(args, format);
  result = commonoutput_vprintf(handle, verbose, format, args);
  va_end(args);
  return result;
}

int commonoutput_flush (struct commonoutput_stuct* handle)
{
  if (!handle || !handle->logfile)
    return -1;
  return fflush(handle->logfile);
}

