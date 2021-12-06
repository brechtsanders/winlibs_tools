/*
  header file for functions related to fstab mount points (implemented by MSYS on Windows)
*/

#ifndef INCLUDED_FSTAB_H
#define INCLUDED_FSTAB_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

//!get path of fstab file, caller must free() the result, NULL on error
/*!
  on non-Windows this will be "/etc/fstab"
  on windows detection is done by looking for looking for ../etc/fstab or ../../etc/fstab relative to each entry in the PATH environment variable
  \return path to fstab file (or NULL on error), result must be cleaned up with free()
*/
char* get_fstab_path ();

//!data structure used to store fstab data, see also: read_fstab_data()
struct fstab_data_struct {
  char* fullpath;
  size_t fullpathlen;
  char* mountpath;
  size_t mountpathlen;
  struct fstab_data_struct* next;
};

//!clean up data structure
/*!
  \param  fstabdata             pointer to data structure
*/
void cleanup_fstab_data (struct fstab_data_struct* fstabdata);

//!populate data structure by reading the fstab file
/*!
  \param  fstabpath             location of ftsab file
  \return pointer to data structure (or NULL on error), result must be cleaned up with cleanup_fstab_data()
*/
struct fstab_data_struct* read_fstab_data (const char* fstabpath);

//!convert a full path to a mounted path, caller must free() the result, NULL on error or when not a mounted path
/*!
  \param  fstabdata             pointer to data structure
  \param  fullpath              full path
  \return mounted path (or NULL on error), result must be cleaned up with free()
*/
char* fstabpath_from_fullpath (const struct fstab_data_struct* fstabdata, const char* fullpath);

//!convert a mounted path to a full path, caller must free() the result, NULL on error or when not a mounted path
/*!
  \param  fstabdata             pointer to data structure
  \param  mountpath             mounted path
  \return full path (or NULL on error), result must be cleaned up with free()
*/
char* fstabpath_to_fullpath (const struct fstab_data_struct* fstabdata, const char* mountpath);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_FSTAB_H
