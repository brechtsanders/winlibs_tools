#include "sorted_item_queue.h"
#include <pthread.h>

//#define PTHREAD_USE_MUTEX_AS_LOCK

struct sorted_item_queue_struct {
  sorted_unique_list* list;
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_t lock;
#else
  pthread_mutex_t lock;
#endif
};

struct sorted_item_queue_struct* sorted_item_queue_create (sorted_unique_compare_fn comparefunction)
{
  struct sorted_item_queue_struct* handle;
  if ((handle = (struct sorted_item_queue_struct*)malloc(sizeof(struct sorted_item_queue_struct))) == NULL)
    return NULL;
  if ((handle->list = sorted_unique_list_create(comparefunction, free)) == NULL) {
    free(handle);
    return NULL;
  }
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  if (pthread_rwlock_init(&handle->lock, NULL/*PTHREAD_MUTEX_ERRORCHECK*/) != 0) {
#else
  if (pthread_mutex_init(&handle->lock, NULL) != 0) {
#endif
    free(handle);
    return NULL;
  }
  return handle;
};

void sorted_item_queue_free (struct sorted_item_queue_struct* handle)
{
  if (!handle)
    return;
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_wrlock(&handle->lock);
#else
  pthread_mutex_lock(&handle->lock);
#endif
  sorted_unique_list_free(handle->list);
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_unlock(&handle->lock);
  pthread_rwlock_destroy(&handle->lock);
#else
  pthread_mutex_unlock(&handle->lock);
  pthread_mutex_destroy(&handle->lock);
#endif
  free(handle);
}

size_t sorted_item_queue_size (struct sorted_item_queue_struct* handle)
{
  size_t result;
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_rdlock(&handle->lock);
#else
  pthread_mutex_lock(&handle->lock);
#endif
  result = sorted_unique_list_size(handle->list);
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_unlock(&handle->lock);
#else
  pthread_mutex_unlock(&handle->lock);
#endif
  return result;
}

int sorted_item_queue_add (struct sorted_item_queue_struct* handle, const char* value)
{
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_wrlock(&handle->lock);
#else
  pthread_mutex_lock(&handle->lock);
#endif
  sorted_unique_list_add(handle->list, value);
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_unlock(&handle->lock);
#else
  pthread_mutex_unlock(&handle->lock);
#endif
  return 0;
}

int sorted_item_queue_add_callback (const char* value, void* handle)
{
  return sorted_item_queue_add((struct sorted_item_queue_struct*)handle, value);
}

int sorted_item_queue_add_allocated (struct sorted_item_queue_struct* handle, char* value)
{
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_wrlock(&handle->lock);
#else
  pthread_mutex_lock(&handle->lock);
#endif
  sorted_unique_list_add_allocated(handle->list, value);
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_unlock(&handle->lock);
#else
  pthread_mutex_unlock(&handle->lock);
#endif
  return 0;
}

char* sorted_item_queue_take_next (struct sorted_item_queue_struct* handle, size_t* remaining)
{
  char* result;
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_wrlock(&handle->lock);
#else
  pthread_mutex_lock(&handle->lock);
#endif
  result = sorted_unique_list_get_and_remove_first(handle->list);
  if (remaining)
    *remaining = sorted_unique_list_size(handle->list);
#ifndef PTHREAD_USE_MUTEX_AS_LOCK
  pthread_rwlock_unlock(&handle->lock);
#else
  pthread_mutex_unlock(&handle->lock);
#endif
  return result;
}
