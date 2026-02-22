/*
  header file with common definitions for winlibs tools
*/

#ifndef INCLUDED_WINLIBS_COMMON_H
#define INCLUDED_WINLIBS_COMMON_H

/*! \brief version number constants
 * \name   WINLIBS_VERSION_*
 * \{
 */
/*! \brief major version number */
#define WINLIBS_VERSION_MAJOR 1
/*! \brief minor version number */
#define WINLIBS_VERSION_MINOR 0
/*! \brief micro version number */
#define WINLIBS_VERSION_MICRO 21
/*! @} */

/*! \brief packed version number */
#define WINLIBS_VERSION (WINLIBS_VERSION_MAJOR * 0x01000000 + WINLIBS_VERSION_MINOR * 0x00010000 + WINLIBS_VERSION_MICRO * 0x00000100)

/*! \cond PRIVATE */
#define WINLIBS_VERSION_STRINGIZE_(major, minor, micro) #major"."#minor"."#micro
#define WINLIBS_VERSION_STRINGIZE(major, minor, micro) WINLIBS_VERSION_STRINGIZE_(major, minor, micro)
/*! \endcond */

/*! \brief string with dotted version number \hideinitializer */
#define WINLIBS_VERSION_STRING WINLIBS_VERSION_STRINGIZE(WINLIBS_VERSION_MAJOR, WINLIBS_VERSION_MINOR, WINLIBS_VERSION_MICRO)

/*! \brief project license type \hideinitializer */
#define WINLIBS_LICENSE "GPL"

/*! \brief project credits \hideinitializer */
#define WINLIBS_CREDITS "Brecht Sanders (2020-2025)"

/*! \cond PRIVATE */
#if defined(_WIN32) && !defined(NOWINDOWSCONSOLE)
#define WINLIBS_HELP_COLOR_WIN "the output is a Windows console then its features will be used. If not and\n"
#else
#define WINLIBS_HELP_COLOR_WIN
#endif
/*! \endcond */

/*! \brief help text for color output */
#define WINLIBS_HELP_COLOR \
  "Color will be used and the terminal window title will be set if possible.\n" \
  "If " WINLIBS_HELP_COLOR_WIN "the TERM environment variable is set ANSI/VT color codes will be used.\n" \
  "Otherwise no color will be used and the terminal window title will not be set.\n" \
  "Set environment variable NO_COLOR to explicitly force plain output.\n"

/*! \brief name of file in package containing metadata */
#define PACKAGE_INFO_METADATA_FILE ".packageinfo.xml"

/*! \brief name of folder in package containing license files */
#define PACKAGE_INFO_LICENSE_FOLDER ".license"

/*! \brief name of file containing version number */
#define PACKAGE_INFO_VERSION_FILE "version"

/*! \brief name of file containing file list */
#define PACKAGE_INFO_CONTENT_FILE "files"

/*! \brief name of file containing folder list */
#define PACKAGE_INFO_FOLDERS_FILE "folders"

/*! \brief name of file containing dependencies */
#define PACKAGE_INFO_DEPENDENCIES_FILE "deps"
#define PACKAGE_INFO_OPTIONALDEPENDENCIES_FILE "deps_optional"
#define PACKAGE_INFO_BUILDDEPENDENCIES_FILE "deps_build"


/*! \brief path separator character */
#ifdef _WIN32
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif

/*! \brief path list separator character */
#ifdef _WIN32
#define PATHLIST_SEPARATOR ';'
#else
#define PATHLIST_SEPARATOR ':'
#endif

/*! \cond PRIVATE */
#define WINLIBS_CHR2STR_(c) #c
#define WINLIBS_CHR2STR(c) WINLIBS_CHR2STR_(c)
/*! \endcond */

/*! \brief path where information about installed packages is stored */
#ifdef _WIN32
#define PACKAGE_INFO_PATH "\\var\\lib\\packages"
#else
#define PACKAGE_INFO_PATH "/var/lib/packages"
#endif

/*! \brief path where package information database is stored */
#ifdef _WIN32
#define PACKAGE_DATABASE_PATH "\\var\\lib\\winlibs"
#else
#define PACKAGE_DATABASE_PATH "/var/lib/winlibs"
#endif

/*! \brief package information database filename */
#define PACKAGE_DATABASE_FILE "wl-pkg.db"

/*! \brief extension for package recipe files */
#define PACKAGE_RECIPE_EXTENSION ".winlib"

/*! \brief base name for lock file used for downloads */
#define SOURCE_DOWNLOAD_LOCK_FILE_BASE ".winlibsdownload"

/*! \brief sleep for specified number of seconds */
#ifdef _WIN32
#define SLEEP_SECONDS(s) Sleep(s * 1000);
#else
#define SLEEP_SECONDS(s) sleep(s);
#endif

///*! \cond PRIVATE */
//#if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
//#define __USE_MINGW_ANSI_STDIO 0  /* use Windows' printf functions as they are faster */
//#endif
///*! \endcond */

#endif //INCLUDED_WINLIBS_COMMON_H
