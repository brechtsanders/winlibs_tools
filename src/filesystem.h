/*
  header file for package information functions
*/

#ifndef INCLUDED_FILESYSTEM_H
#define INCLUDED_FILESYSTEM_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

//!check if a file exists
/*!
  \param  path                  path of file to check
  \return 1 if path exists and is a regular file, otherwise 0
*/
int file_exists (const char* path);

//!check if a folder exists
/*!
  \param  path                  path of folder to check
  \return 1 if path exists and is a folder, otherwise 0
*/
int folder_exists (const char* path);

//!create directory and if needed also create missing parent directories
/*!
  \param  path                  path of directory to create
  \return 0 on success, 1 if the directory already exists or -1 on error
*/
int recursive_mkdir (const char* path);

//!delete file
/*!
  \param  path                  path of file to delete
  \return 0 on success, 1 if file didn't exist or -1 on error
*/
int delete_file (const char* path);

//!read line from file
/*!
  \param  f                     open file handle to read line from
  \return line read from file or NULL on end of file or error, caller must free() the result
*/
char* readline_from_file (FILE* f);

//!write data to file
/*!
  \param  path                  path of file to write to
  \param  data                  data to write to file
  \return 0 on success
*/
int write_to_file (const char* path, const char* data);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_FILESYSTEM_H
