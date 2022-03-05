/*
  header file for sorted unique list functions
*/

#ifndef INCLUDED_SORTED_UNIQUE_LIST_H
#define INCLUDED_SORTED_UNIQUE_LIST_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

//!data structure for sorted unique list
typedef struct sorted_unique_list sorted_unique_list;

//!comparison function
typedef int (*sorted_unique_compare_fn)(const char* data1, const char* data2);

//!free function
typedef void (*sorted_unique_free_fn)(void* data);

//!duplication function
typedef char* (*sorted_unique_duplicate_fn)(const char* data);

//!create new sorted unique list
/*!
  \param  comparefunction       comparison function used for sorting
  \param  freefunction          function for freeing entries
  \return new sorted unique list or NULL on error (memory allocation error)
*/
sorted_unique_list* sorted_unique_list_create (sorted_unique_compare_fn comparefunction, sorted_unique_free_fn freefunction);

//!clean up sorted unique list
/*!
  \param  sortuniqlist          sorted unique list
*/
void sorted_unique_list_free (sorted_unique_list* sortuniqlist);

//!empty sorted unique list
/*!
  \param  sortuniqlist          sorted unique list
*/
void sorted_unique_list_clear (sorted_unique_list* sortuniqlist);

//!add copy of data to sorted unique list (uses strdup() to allocate memory, so only works with free() as freefunction)
/*!
  \param  sortuniqlist          sorted unique list
  \param  data                  data to add to sorted unique list
  \return zero on success or non-zero on error (memory allocation error or sortuniqlist NULL or empty)
*/
int sorted_unique_list_add (sorted_unique_list* sortuniqlist, const char* data);

//!add copy of data from buffer to sorted unique list (uses malloc() to allocate memory, so only works with free() as freefunction)
/*!
  \param  sortuniqlist          sorted unique list
  \param  data                  data to add to sorted unique list (does not have to be null-terminated)
  \param  datalen               length of data to add to sorted unique list
  \return zero on success or non-zero on error (memory allocation error or sortuniqlist NULL or empty)
*/
int sorted_unique_list_add_buf (sorted_unique_list* sortuniqlist, const char* data, size_t datalen);

//!add allocated data to sorted unique list
/*!
  \param  sortuniqlist          sorted unique list
  \param  data                  allocated data to add to sorted unique list (will be freed by the library)
  \return zero on success or non-zero on error (memory allocation error or sortuniqlist NULL or empty)
*/
int sorted_unique_list_add_allocated (sorted_unique_list* sortuniqlist, char* data);

//!add copies of entries from a comma separated list to sorted unique list (uses malloc() to allocate memory, so only works with free() as freefunction)
/*!
  \param  sortuniqlist          sorted unique list
  \param  list                  comma sperated list of data to add to sorted unique list
  \return zero on success or non-zero on error (memory allocation error or sortuniqlist NULL or empty)
*/
size_t sorted_unique_list_add_comma_separated_list (sorted_unique_list* sortuniqlist, const char* list);

//!remove data from sorted unique list
/*!
  \param  sortuniqlist          sorted unique list
  \param  data                  data to remove from sorted unique list
*/
void sorted_unique_list_remove (sorted_unique_list* sortuniqlist, const char* data);

//!get data entry from sorted unique list
/*!
  \param  sortuniqlist          sorted unique list
  \param  data                  data to find
  \return pointer to data or NULL if not found
*/
char* sorted_unique_list_search (sorted_unique_list* sortuniqlist, const char* data);

//!check if data exists in sorted unique list
/*!
  \param  sortuniqlist          sorted unique list
  \param  data                  data to find
  \return non-zero if data exists, or zero if not found
*/
int sorted_unique_list_find (sorted_unique_list* sortuniqlist, const char* data);

//!get number of entries in sorted unique list
/*!
  \param  sortuniqlist          sorted unique list
  \return number of entries in sorted unique list
*/
unsigned int sorted_unique_list_size (const sorted_unique_list* sortuniqlist);

//!get sorted unique list entry
/*!
  \param  sortuniqlist          sorted unique list
  \param  index                 index of sorted unique list entry (first one is 0)
  \return entry data (or NULL if invalid index)
*/
const char* sorted_unique_list_get (const sorted_unique_list* sortuniqlist, unsigned int index);

//!get and remove first entry from sorted unique list
/*!
  \param  sortuniqlist          sorted unique list
  \return first entry data or NULL if list is empty, the caller must free()
*/
char* sorted_unique_list_get_and_remove_first (sorted_unique_list* sortuniqlist);

//!load entries from file (assumes entries are strings, uses malloc()/realloc() to allocate memory, so only works with free() as freefunction)
/*!
  \param  sortuniqlist          pointer to sorted unique list
  \param  filename              path of file to load data from
  \return number of entries loaded
*/
long sorted_unique_list_load_from_file (sorted_unique_list** sortuniqlist, const char* filename);

//!save entries to file (assumes entries are strings)
/*!
  \param  sortuniqlist          sorted unique list
  \param  filename              path of file to save data to
  \return number of entries loaded
*/
long sorted_unique_list_save_to_file (sorted_unique_list* sortuniqlist, const char* filename);

//!list compare callback function
typedef int (*sorted_unique_list_compare_callback_fn)(const char* data, void* callbackdata);

//!compare two sorted unique lists
/*!
  \param  sortuniqlist1         first unique list
  \param  sortuniqlist2         second unique list
  \param  callback_both_fn      callback function to call for entries that are in both lists
  \param  callback_only1_fn     callback function to call for entries that are only in the first list
  \param  callback_only2_fn     callback function to call for entries that are only in the second list
  \param  callbackdata
  \return first entry data, the caller must free()
*/
int sorted_unique_list_compare_lists (const sorted_unique_list* sortuniqlist1, const sorted_unique_list* sortuniqlist2, sorted_unique_list_compare_callback_fn callback_both_fn, sorted_unique_list_compare_callback_fn callback_only1_fn, sorted_unique_list_compare_callback_fn callback_only2_fn, void* callbackdata);

//!create copy of sorted unique list
/*!
  \param  sortuniqlist          sorted unique list to copy
  \param  duplicatefunction     duplication function (e.g. strdup), or NULL to point items to same data is in source tree
  \return copy of sorted unique list or NULL on error
*/
sorted_unique_list* sorted_unique_list_duplicate (const sorted_unique_list* sortuniqlist, sorted_unique_duplicate_fn duplicatefunction);

//!print values in a sorted unique list
/*!
  \param  sortuniqlist          sorted unique list to print
  \param  separator             separator to use between listed entries
*/
void sorted_unique_list_print (const sorted_unique_list* sortuniqlist, const char* separator);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_SORTED_UNIQUE_LIST_H
