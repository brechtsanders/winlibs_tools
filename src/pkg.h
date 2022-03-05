/*
  header file for the package information data structure
*/

#ifndef INCLUDED_PKG_H
#define INCLUDED_PKG_H

#include "sorted_unique_list.h"
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

//!macros specifying package data field index
/*!
 * \name   PACKAGE_METADATA_INDEX_*
 * \{
 */
#define PACKAGE_METADATA_INDEX_BASENAME             0
#define PACKAGE_METADATA_INDEX_VERSION              1
#define PACKAGE_METADATA_INDEX_NAME                 2
#define PACKAGE_METADATA_INDEX_DESCRIPTION          3
#define PACKAGE_METADATA_INDEX_URL                  4
#define PACKAGE_METADATA_INDEX_DOWNLOADURL          5
#define PACKAGE_METADATA_INDEX_DOWNLOADSOURCEURL    6
#define PACKAGE_METADATA_INDEX_CATEGORY             7
#define PACKAGE_METADATA_INDEX_TYPE                 8
#define PACKAGE_METADATA_INDEX_VERSIONDATE          9
#define PACKAGE_METADATA_INDEX_LICENSEFILE          10
#define PACKAGE_METADATA_INDEX_LICENSETYPE          11
#define PACKAGE_METADATA_INDEX_STATUS               12
#define PACKAGE_METADATA_TOTAL_FIELDS               13
/*! @} */

//!names of package data fields
/*!
 * \sa     PACKAGE_METADATA_INDEX_*
 */
extern const char* package_metadata_field_name[];

//!data structure for package information
struct package_metadata_struct {
  char* datafield[PACKAGE_METADATA_TOTAL_FIELDS];
  sorted_unique_list* fileexclusions;
  sorted_unique_list* folderexclusions;
  sorted_unique_list* filelist;
  sorted_unique_list* folderlist;
  sorted_unique_list* dependencies;
  sorted_unique_list* optionaldependencies;
  sorted_unique_list* builddependencies;
  uint64_t totalsize;
  size_t version_linenumber;
  int buildok;
  time_t lastchanged;
  void* extradata;
  void (*extradata_free_fn)(void*);
};

//!create and initialize data structure for package information
struct package_metadata_struct* package_metadata_create ();

//!clean up data structure for package information
void package_metadata_free (struct package_metadata_struct* metadata);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_PKG_H
