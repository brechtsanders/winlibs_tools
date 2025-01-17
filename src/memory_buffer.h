/*
  header file for memory buffer functions
*/

#ifndef INCLUDED_MEMORY_BUFFER_H
#define INCLUDED_MEMORY_BUFFER_H

#include <stdlib.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

//!data structure for memory buffer
struct memory_buffer {
  size_t datalen;
  char* data;
};

//!create memory buffer
/*!
  \return memory buffer
*/
struct memory_buffer* memory_buffer_create ();

//!create memory buffer from string
/*!
  \param  data                  string to add to memory buffer
  \return memory buffer
*/
struct memory_buffer* memory_buffer_create_from_string (const char* data);

//!create memory buffer from allocated string, the caller must not free() it
/*!
  \param  data                  string to add to memory buffer
  \return memory buffer
*/
struct memory_buffer* memory_buffer_create_from_allocated_string (char* data);

//!clean up memory buffer
/*!
  \param  membuf                memory buffer
*/
void memory_buffer_free (struct memory_buffer* membuf);

//!clean up memory buffer and return allocated string with the contents
/*!
  \param  membuf                memory buffer
  \return null-terminated contents (or NULL if nothing was added), the caller must call free()
*/
char* memory_buffer_free_to_allocated_string (struct memory_buffer* membuf);

//!get memory buffer contents
/*!
  \param  membuf                memory buffer
  \return null-terminated contents (or NULL if empty)
*/
const char* memory_buffer_get (struct memory_buffer* membuf);

//!get size of memory buffer
/*!
  \param  membuf                memory buffer
  \return size of memory buffer
*/
size_t memory_buffer_length (struct memory_buffer* membuf);

//!set memory buffer to string
/*!
  \param  membuf                memory buffer
  \param  data                  string to set memory buffer to
  \return memory buffer
*/
struct memory_buffer* memory_buffer_set (struct memory_buffer* membuf, const char* data);

//!set memory buffer to an  allocated string, the caller must not free() it
/*!
  \param  membuf                memory buffer
  \param  data                  string to set memory buffer to
  \return memory buffer
*/
struct memory_buffer* memory_buffer_set_allocated (struct memory_buffer* membuf, char* data);

//!grow memory buffer (contents of the additional data will be undefined, this will not shrink if the buffer if newsize is smaller than the current size)
/*!
  \param  membuf                memory buffer
  \param  newsize               string to add to memory buffer
  \return new size of memory buffer (or 0 on error)
*/
size_t memory_buffer_grow (struct memory_buffer* membuf, size_t newsize);

//!append string to memory buffer
/*!
  \param  membuf                memory buffer
  \param  data                  string to add to memory buffer
  \return number of bytes added
*/
int memory_buffer_append (struct memory_buffer* membuf, const char* data);

//!append data to memory buffer
/*!
  \param  membuf                memory buffer
  \param  data                  data to add to memory buffer
  \param  datalen               size (in bytes) of data to add to memory buffer
  \return number of bytes added
*/
int memory_buffer_append_buf (struct memory_buffer* membuf, const char* data, size_t datalen);

//!set memory buffer using printf formatting
/*!
  \param  membuf                memory buffer
  \param  format                format as used by printf()
  \return memory buffer or NULL on error
*/
struct memory_buffer* memory_buffer_set_printf (struct memory_buffer* membuf, const char* format, ...);

//!append data to memory buffer using printf formatting
/*!
  \param  membuf                memory buffer
  \param  format                format as used by printf()
  \return number of bytes added
*/
int memory_buffer_append_printf (struct memory_buffer* membuf, const char* format, ...);

//!replace part of memory buffer
/*!
  \param  membuf                memory buffer
  \param  pos                   starting position of part to replace
  \param  len                   length of part to replace
  \param  replacement           new data to replace with
  \return memory buffer
*/
struct memory_buffer* memory_buffer_replace (struct memory_buffer* membuf, size_t pos, size_t len, const char* replacement);

//!replace part of memory buffer
/*!
  \param  membuf                memory buffer
  \param  pos                   starting position of part to replace
  \param  len                   length of part to replace
  \param  replacement           new data to replace with
  \param  replacementlen        size of new data to replace with
  \return memory buffer
*/
struct memory_buffer* memory_buffer_replace_data (struct memory_buffer* membuf, size_t pos, size_t len,const  char* replacement, size_t replacementlen);

//!replace text in memory buffer
/*!
  \param  membuf                memory buffer
  \param  s1                    text to search
  \param  s2                    text to replace with
  \return memory buffer
*/
struct memory_buffer* memory_buffer_find_replace_data (struct memory_buffer* membuf, const char* s1, const char* s2);

//!fix memory buffer for use as XML data
/*!
  \param  membuf                memory buffer
  \return memory buffer
*/
struct memory_buffer* memory_buffer_xml_special_chars (struct memory_buffer* membuf);

//!fix memory buffer for use in a URL
/*!
  \param  membuf                memory buffer
  \return memory buffer
*/
struct memory_buffer* url_encode (struct memory_buffer* membuf);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_MEMORY_BUFFER_H
