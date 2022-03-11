/*
  header file for package build order functions
*/

#ifndef INCLUDED_BUILD_ORDER_H
#define INCLUDED_BUILD_ORDER_H

#include "pkgfile.h"

#ifdef __cplusplus
extern "C" {
#endif

enum package_filter_type_enum {
  filter_type_all,
  filter_type_changed
};

struct add_package_and_dependencies_to_list_struct {
  sorted_unique_list* packagenamelist;
  const char* packageinfopath;
  enum package_filter_type_enum filtertype;
};

struct package_info_extradata_struct {
  enum package_filter_type_enum filtertype;
  unsigned char visited;
  unsigned char checkingcyclic;
  struct package_metadata_struct* cyclic_start_pkginfo;
  struct package_metadata_struct* cyclic_next_pkginfo;
  size_t cyclic_size;
};

//!macro for accessing extradata member of struct add_package_and_dependencies_to_list_struct
#define PKG_XTRA(pkginfo) ((struct package_info_extradata_struct*)(pkginfo)->extradata)

//!compare function for package information
/*!
  \param  data1                 first package information to compare
  \param  data2                 second package information to compare
  \return zero if the base name of both packages is equal, negative if it's smaller for data1, otherwise positive
*/
int packageinfo_cmp_basename (const char* data1, const char* data2);

//!add package and its dependencies to list
/*!
  \param  basename              name of package to add to list
  \param  data                  list to add package to
  \return zero on success
*/
int add_package_and_dependencies_to_list (const char* basename, struct add_package_and_dependencies_to_list_struct* data);

//!sort list of packages in the right order to build them based on their dependencies
/*!
  \param  sortedpackagelist     list of packages
  \return shell exit code (non-zero usually means an error occurred in the last command executed)
*/
struct package_info_list_struct* generate_build_list (sorted_unique_list* sortedpackagelist);

size_t dependancies_listed_but_not_depended_on (const char* dstdir, struct package_metadata_struct* pkginfo);

#ifdef __cplusplus
}
#endif

#endif //INCLUDED_BUILD_ORDER_H
