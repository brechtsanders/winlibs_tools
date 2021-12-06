#include "sorted_unique_list.h"
#include <avl.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

struct sorted_unique_list {
  avl_tree_t* tree;
  sorted_unique_compare_fn cmp_fn;
  sorted_unique_free_fn free_fn;
};

sorted_unique_list* sorted_unique_list_create (sorted_unique_compare_fn comparefunction, sorted_unique_free_fn freefunction)
{
  sorted_unique_list* result;
  if ((result = (struct sorted_unique_list*)malloc(sizeof(struct sorted_unique_list))) == NULL)
    return NULL;
  if ((result->tree = avl_alloc_tree((avl_compare_t)comparefunction, (avl_freeitem_t)freefunction)) == NULL) {
    free(result);
    return NULL;
  }
  result->cmp_fn = comparefunction;
  result->free_fn = freefunction;
  return result;
}

void sorted_unique_list_free (sorted_unique_list* sortuniqlist)
{
  if (!sortuniqlist)
    return;
  avl_free_tree(sortuniqlist->tree);
  free(sortuniqlist);
}

void sorted_unique_list_clear (sorted_unique_list* sortuniqlist)
{
  avl_free_nodes(sortuniqlist->tree);
}

int sorted_unique_list_add (sorted_unique_list* sortuniqlist, const char* data)
{
  char* s;
  if (!sortuniqlist || !data || !*data || (s = strdup(data)) == NULL)
    return -1;
  if (avl_insert(sortuniqlist->tree, (void*)s) == NULL) {
    //insert failed (not unique)
    free(s);
    return 1;
  }
  return 0;
}

int sorted_unique_list_add_buf (sorted_unique_list* sortuniqlist, const char* data, size_t datalen)
{
  char* s;
  if (!sortuniqlist || !data || !*data || (s = (char*)malloc(datalen + 1)) == NULL)
    return -1;
  memcpy(s, data, datalen);
  s[datalen] = 0;
  if (avl_insert(sortuniqlist->tree, (void*)s) == NULL) {
    //insert failed (not unique)
    free(s);
    return 1;
  }
  return 0;
}

int sorted_unique_list_add_allocated (sorted_unique_list* sortuniqlist, char* data)
{
  if (!sortuniqlist || !data)
    return -1;
  if (avl_insert(sortuniqlist->tree, (void*)data) == NULL) {
    //insert failed (not unique)
    (*sortuniqlist->free_fn)(data);
    return 1;
  }
  return 0;
}

void sorted_unique_list_remove (sorted_unique_list* sortuniqlist, const char* data)
{
  if (!sortuniqlist || !data || !*data)
    return;
  avl_delete(sortuniqlist->tree, (void*)data);
}

char* sorted_unique_list_search (sorted_unique_list* sortuniqlist, const char* data)
{
  avl_node_t* result;
  if ((result = avl_search(sortuniqlist->tree, data)) == NULL)
    return NULL;
  return (char*)result->item;
}

int sorted_unique_list_find (sorted_unique_list* sortuniqlist, const char* data)
{
  return (avl_search(sortuniqlist->tree, data) != NULL ? 1 : 0);
}

unsigned int sorted_unique_list_size (const sorted_unique_list* sortuniqlist)
{
  if (!sortuniqlist)
    return 0;
  return avl_count(sortuniqlist->tree);
}

const char* sorted_unique_list_get (const sorted_unique_list* sortuniqlist, unsigned int index)
{
  avl_node_t* node;
  if (!sortuniqlist)
    return NULL;
  if ((node = avl_at(sortuniqlist->tree, index)) == NULL)
    return NULL;
  return (char*)node->item;
}

char* sorted_unique_list_get_and_remove_first (sorted_unique_list* sortuniqlist)
{
  avl_node_t* node;
  char* result;
  if (!sortuniqlist)
    return NULL;
  if ((node = avl_at(sortuniqlist->tree, 0)) == NULL)
    return NULL;
/*
  result = (node->item ? strdup((char*)node->item) : NULL);
  avl_delete_node(sortuniqlist->tree, node);
*/
  result = (char*)node->item;
  avl_unlink_node(sortuniqlist->tree, node);
/**/
  return result;
}

long sorted_unique_list_load_from_file (sorted_unique_list** sortuniqlist, const char* filename)
{
  FILE* src;
  long count = 0;
  if ((src = fopen(filename, "rb")) != NULL) {
    char buf[256];
    size_t len;
    size_t beginpos;
    size_t endpos;
    char* current;
    size_t currentlen;
    char* partial = NULL;
    size_t partiallen = 0;
    size_t partialallocatedlen = 0;
    //read file contents (skipping empty lines)
    while ((len = fread(buf, 1, sizeof(buf), src)) > 0) {
      beginpos = 0;
      endpos = 0;
      while (endpos < len) {
        if (buf[endpos] != '\r' && buf[endpos] != '\n') {
          endpos++;
        } else {
          //line end found
          if (endpos > beginpos || partiallen) {
            //not an empty line
            current = buf + beginpos;
            currentlen = endpos - beginpos;
            if (partiallen) {
              if (partiallen + endpos - beginpos > partialallocatedlen)
                partial = (char*)realloc(partial, partialallocatedlen = partiallen + endpos - beginpos);
              if (partial) {
                memcpy(partial + partiallen, buf + beginpos, endpos - beginpos);
                partiallen += endpos - beginpos;
                current = partial;
                currentlen = partiallen;
              }
            }
            //add to list
            if (sorted_unique_list_add_buf(*sortuniqlist, current, currentlen) == 0)
              count++;
            //clear partial data length
            if (partiallen)
              partiallen = 0;
          }
          //skip to after end of line character
          beginpos = ++endpos;
        }
      }
      //keep unprocessed data at end of buffer
      if (endpos > beginpos) {
        if (partiallen + endpos - beginpos > partialallocatedlen)
          partial = (char*)realloc(partial, partialallocatedlen = partiallen + endpos - beginpos);
        if (partial) {
          memcpy(partial + partiallen, buf + beginpos, endpos - beginpos);
          partiallen += endpos - beginpos;
        }
      }
    }
    fclose(src);
    if (partiallen) {
      //add to list
      if (sorted_unique_list_add_buf(*sortuniqlist, partial, partiallen) == 0)
        count++;
    }
    if (partial)
      free(partial);
  }
  return count;
}

long sorted_unique_list_save_to_file (sorted_unique_list* sortuniqlist, const char* filename)
{
  FILE* dst;
  int i;
  int n;
  avl_node_t* node;
  long count = 0;
  if ((dst = fopen(filename, "wb")) == NULL)
    return -1;
  n = avl_count(sortuniqlist->tree);
  for (i = 0; i < n; i++) {
    if ((node = avl_at(sortuniqlist->tree, i)) != NULL) {
      fprintf(dst, "%s\n", (char*)node->item);
      count++;
    }
  }
  fclose(dst);
  return count;
}

int sorted_unique_list_compare_lists (const sorted_unique_list* sortuniqlist1, const sorted_unique_list* sortuniqlist2, sorted_unique_list_compare_callback_fn callback_both_fn, sorted_unique_list_compare_callback_fn callback_only1_fn, sorted_unique_list_compare_callback_fn callback_only2_fn, void* callbackdata)
{
  int cmp;
  int result;
  const char* data1;
  const char* data2;
  unsigned int sortuniqlist1size;
  unsigned int sortuniqlist2size;
  unsigned int pos1 = 0;
  unsigned int pos2 = 0;
  sortuniqlist1size = sorted_unique_list_size(sortuniqlist1);
  sortuniqlist2size = sorted_unique_list_size(sortuniqlist2);
  //iterate through both lists as long as the end is not reached for one of them
  while (pos1 < sortuniqlist1size && pos2 < sortuniqlist2size) {
    data1 = sorted_unique_list_get(sortuniqlist1, pos1);
    data2 = sorted_unique_list_get(sortuniqlist2, pos2);
    if ((cmp = (sortuniqlist1->cmp_fn)(data1, data2)) == 0) {
      if (callback_both_fn)
        if ((result = (*callback_both_fn)(data1, callbackdata)) != 0)
          return result;
      pos1++;
      pos2++;
    } else if (cmp < 0) {
      if (callback_only1_fn)
        if ((result = (*callback_only1_fn)(data1, callbackdata)) != 0)
          return result;
      pos1++;
    } else {
      if (callback_only2_fn)
        if ((result = (*callback_only2_fn)(data2, callbackdata)) != 0)
          return result;
      pos2++;
    }
  }
  //iterate through remaining individual lists
  if (callback_only1_fn) {
    data1 = sorted_unique_list_get(sortuniqlist1, pos1);
    while (pos1 < sortuniqlist1size) {
      if ((result = (*callback_only1_fn)(data1, callbackdata)) != 0)
        return result;
      pos1++;
    }
  }
  if (callback_only2_fn) {
    while (pos2 < sortuniqlist2size) {
      data2 = sorted_unique_list_get(sortuniqlist2, pos2);
      if ((result = (*callback_only2_fn)(data2, callbackdata)) != 0)
        return result;
      pos2++;
    }
  }
  return 0;
}
