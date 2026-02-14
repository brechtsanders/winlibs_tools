/*
  header file for functions related to generating unified diff
*/

#ifndef INCLUDED_GENERATEDIFF_H
#define INCLUDED_GENERATEDIFF_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

//!handle type used by unified diff functions
typedef struct diff_handle_struct* diff_handle;

//!initialize unified diff library
/*!
  \return 0 on success
*/
int diff_initialize ();

//!initialize unified diff handle between 2 files
/*!
  \param  filename1             first file to compare
  \param  filename2             second file to compare
  \return handle on success, NULL on error
*/
diff_handle diff_create (const char* filename1, const char* filename2);

//!clean up unified diff handle
/*!
  \param  handle                unified diff handle
*/
void diff_free (diff_handle handle);

//!check if files are different
/*!
  \param  handle                unified diff handle
  \return 0 if identical, non-zero if different
*/
int diff_cmp (diff_handle handle);

//!write unified diff to specified output
/*!
  \param  handle                unified diff handle
  \param  context               number of lines of context to include
  \param  dst                   destination where to write unified diff output to (may be stdout)
  \return 0 on success
*/
int diff_generate (diff_handle handle, int context, FILE* dst);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_GENERATEDIFF_H
