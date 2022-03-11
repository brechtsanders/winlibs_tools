/*
  header file for package build functions
*/

#ifndef INCLUDED_BUILD_PACKAGE_H
#define INCLUDED_BUILD_PACKAGE_H

extern unsigned int interrupted;           //variable set by signal handler

//!build package from source
/*!
  \param  infopath              full path(s) of directory containing build information files
  \param  basename              name of package
  \param  shell                 full shell script path (and optional arguments)
  \param  logfile               path of log file to write to (NULL for no logging)
  \param  buildpath             path where temporary build folder will be created (NULL build in shell's current path)
  \return shell exit code (non-zero usually means an error occurred in the last command executed)
*/
unsigned long build_package (const char* infopath, const char* basename, const char* shell, const char* logfile, const char* buildpath);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_BUILD_PACKAGE_H
