/*
  header file for sorted item queue functions
*/

#ifndef INCLUDED_SORTED_ITEM_QUEUE_H
#define INCLUDED_SORTED_ITEM_QUEUE_H

#include "sorted_unique_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/* thread-safe sorted item queue */

//!data structure for sorted item queue
struct sorted_item_queue_struct;

//!create new sorted item queue
/*!
  \param  comparefunction       comparison function used for sorting
  \return new sorted item queue or NULL on error (memory allocation error)
*/
struct sorted_item_queue_struct* sorted_item_queue_create (sorted_unique_compare_fn comparefunction);

//!clean up sorted item queue
/*!
  \param  handle                sorted item queue
*/
void sorted_item_queue_free (struct sorted_item_queue_struct* handle);

//!get number of entries in sorted item queue
/*!
  \param  handle                sorted item queue
  \return number of entries in sorted item queue
*/
size_t sorted_item_queue_size (struct sorted_item_queue_struct* handle);

//!add value to sorted item queue
/*!
  \param  handle                sorted item queue
  \param  value                 value to add to queue
  \return number of entries in sorted item queue
*/
int sorted_item_queue_add (struct sorted_item_queue_struct* handle, const char* value);

//!add value to sorted item queue (for use as callback function)
/*!
  \param  handle                sorted item queue
  \param  value                 value to add to queue
  \return number of entries in sorted item queue
*/
int sorted_item_queue_add_callback (const char* value, void* handle);

//!add allocated value  to sorted item queue
/*!
  \param  handle                sorted item queue
  \param  value                 allocated value to add to queue (caller must no longer free() it)
  \return number of entries in sorted item queue
*/
int sorted_item_queue_add_allocated (struct sorted_item_queue_struct* handle, char* value);

//!get first entry from sorted item queue and remove it
/*!
  \param  handle                sorted item queue
  \param  value                 value to add to queue
  \return number of entries in sorted item queue
*/
char* sorted_item_queue_take_next (struct sorted_item_queue_struct* handle, size_t* remaining);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_SORTED_ITEM_QUEUE_H
