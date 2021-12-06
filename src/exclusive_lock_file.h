/*
  header file for exclusive lock file functions
*/

#ifndef INCLUDED_EXCLUSIVELOCKFILE_H
#define INCLUDED_EXCLUSIVELOCKFILE_H

#ifdef __cplusplus
extern "C" {
#endif

//!data structure for exclusive lock file
typedef struct exclusive_lock_file_struct* exclusive_lock_file;

//!callback function used while waiting for another process to release the lock on the lock file
/*!
  \param  handle                database handle
  \param  callbackdata          user data
  \return zero to continue or non-zero to abort
*/
typedef int (*exclusive_lock_file_create_progress_callback_fn)(exclusive_lock_file handle, void* callbackdata);

//!create exclusive lock file handle
/*!
  \param  directory             path to directory where exclusive lock file will be created
  \param  basename              base name to use for lock file (an extension will be added)
  \param  errmsg                pointer to where error message will be stored (or NULL if not needed)
  \param  callback              user function to call while trying to get the lock (not called if lock succeeds right away, called multiple times if multiple attempts are made)
  \param  callbackdata          user data to pass to user function
  \return exclusive lock file handle or NULL on error
*/
exclusive_lock_file exclusive_lock_file_create (const char* directory, const char* basename, const char** errmsg, exclusive_lock_file_create_progress_callback_fn callback, void* callbackdata);

//!clean up file handle
/*!
  \param  handle                exclusive lock file handle
*/
void exclusive_lock_file_destroy (exclusive_lock_file handle);

//!get full path of lock file
/*!
  \param  handle                exclusive lock file handle
  \return full path to lock file or NULL on error (if handle is NULL)
*/
const char* exclusive_lock_file_get_path (exclusive_lock_file handle);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_EXCLUSIVELOCKFILE_H
