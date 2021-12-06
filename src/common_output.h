/*
  header file for common output functions (with locking to avoid simultaneous output)
*/

#ifndef INCLUDED_COMMON_OUTPUT_H
#define INCLUDED_COMMON_OUTPUT_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

//!data structure for common output
struct commonoutput_stuct;

//!create common output
/*!
  \param  filename              path of file to append to (standard output if NULL)
  \param  verbose               verbosity level
  \return common output handle
*/
struct commonoutput_stuct* commonoutput_create (const char* filename, int verbose);

//!clean up common output
/*!
  \param  handle                common output handle
*/
void commonoutput_free (struct commonoutput_stuct* handle);

//!print data to common output
/*!
  \param  handle                common output handle
  \param  verbose               verbosity level
  \param  data                  data to print
  \return number of characters printed
*/
void commonoutput_put (struct commonoutput_stuct* handle, int verbose, const char* data);

//!print buffer to common output
/*!
  \param  handle                common output handle
  \param  verbose               verbosity level
  \param  data                  data to print
  \param  datalen               length of data to print
  \return number of characters printed
*/
void commonoutput_putbuf (struct commonoutput_stuct* handle, int verbose, const char* data, size_t datalen);

//!print to common output
/*!
  \param  handle                common output handle
  \param  verbose               verbosity level
  \param  format                format as used by printf()
  \param  args                  arguments as used by vprintf()
  \return number of characters printed
*/
int commonoutput_vprintf (struct commonoutput_stuct* handle, int verbose, const char* format, va_list args);

//!print to common output
/*!
  \param  handle                common output handle
  \param  verbose               verbosity level
  \param  format                format as used by printf()
  \return number of characters printed
*/
int commonoutput_printf (struct commonoutput_stuct* handle, int verbose, const char* format, ...);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_COMMON_OUTPUT_H
