#include "build-order.h"
#include <stdio.h>
#include <string.h>
#include "pkgfile.h"
#include "pkgdb.h"
#include "memory_buffer.h"
#include "winlibs_common.h"

int packageinfo_cmp_basename (const char* data1, const char* data2)
{
  return strcasecmp(((struct package_metadata_struct*)data1)->datafield[PACKAGE_METADATA_INDEX_BASENAME], ((struct package_metadata_struct*)data2)->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
}

int add_package_and_dependencies_to_list (const char* basename, struct add_package_and_dependencies_to_list_struct* data)
{
  struct package_metadata_struct* pkginfo;
  struct package_metadata_struct searchpkginfo;
  //check if package is already in list
  searchpkginfo.datafield[PACKAGE_METADATA_INDEX_BASENAME] = (char*)basename;
  if (!sorted_unique_list_find(data->packagenamelist, (char*)&searchpkginfo)) {
    //read package information
    if ((pkginfo = read_packageinfo(data->packageinfopath, basename)) == NULL) {
      //fprintf(stderr, "Error reading package information for package %s from %s\n", basename, data->packageinfopath);
      return 0;
    }
    //skip package if it doesn't build
    if (!pkginfo->buildok) {
      package_metadata_free(pkginfo);
      return 0;
    }
    //add additional data
    if ((pkginfo->extradata = malloc(sizeof(struct package_info_extradata_struct))) == NULL) {
      return -1;
    }
    PKG_XTRA(pkginfo)->filtertype = data->filtertype;
    PKG_XTRA(pkginfo)->visited = 0;
    PKG_XTRA(pkginfo)->checkingcyclic = 0;
    PKG_XTRA(pkginfo)->cyclic_start_pkginfo = NULL;
    PKG_XTRA(pkginfo)->cyclic_next_pkginfo = NULL;
    PKG_XTRA(pkginfo)->cyclic_size = 0;
    pkginfo->extradata_free_fn = free;
    //add package information to list
    if (/*!interrupted &&*/ sorted_unique_list_add_allocated(data->packagenamelist, (char*)pkginfo) == 0) {
      //recurse for each dependency
      iterate_packages_in_list(pkginfo->dependencies, (package_callback_fn)add_package_and_dependencies_to_list, data);
      iterate_packages_in_list(pkginfo->builddependencies, (package_callback_fn)add_package_and_dependencies_to_list, data);
      iterate_packages_in_list(pkginfo->optionaldependencies, (package_callback_fn)add_package_and_dependencies_to_list, data);
    }
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////

typedef enum {
  dep_type_none = 0,
  dep_type_normal = 1,
  dep_type_build = 2,
  dep_type_optional = 3,
} package_dependency_type;

typedef void (*cyclic_graph_detected_fn)(struct package_metadata_struct* pkginfo, package_dependency_type dependency_type, struct package_metadata_struct* startpkginfo, void* callbackdata);

struct check_node_for_cyclic_graph_struct {
  sorted_unique_list* sortedpackagelist;
  size_t count_loops;
  package_dependency_type current_link_type;
  struct package_metadata_struct* parent_pkginfo;
  struct package_metadata_struct* cyclic_start_pkginfo;
};

int check_node_for_cyclic_graph_iterate_dependencies (const char* entryname, struct check_node_for_cyclic_graph_struct* data);

size_t check_node_for_cyclic_graph (struct package_metadata_struct* pkginfo, struct check_node_for_cyclic_graph_struct* data)
{
  size_t result;
  package_dependency_type prev_link_type;
  struct package_metadata_struct* prev_pkginfo;
  //mark package as being checked
  PKG_XTRA(pkginfo)->checkingcyclic = 1;
  //recursively process dependencies
  prev_link_type = data->current_link_type;
  data->current_link_type = dep_type_normal;
  prev_pkginfo = data->parent_pkginfo;
  data->parent_pkginfo = pkginfo;
  result = iterate_packages_in_list(pkginfo->dependencies, (package_callback_fn)check_node_for_cyclic_graph_iterate_dependencies, data);
  if (!result) {
    data->current_link_type = dep_type_build;
    result = iterate_packages_in_list(pkginfo->builddependencies, (package_callback_fn)check_node_for_cyclic_graph_iterate_dependencies, data);
  }
  if (!result) {
    data->current_link_type = dep_type_optional;
    result = iterate_packages_in_list(pkginfo->optionaldependencies, (package_callback_fn)check_node_for_cyclic_graph_iterate_dependencies, data);
  }
  data->current_link_type = prev_link_type;
  data->parent_pkginfo = prev_pkginfo;
  //check if circular loop has come full circle
  if (result && pkginfo == PKG_XTRA(pkginfo)->cyclic_start_pkginfo) {
/*
    if (data->cyclic_graph_detected_callback)
      (*data->cyclic_graph_detected_callback)(pkginfo, none, pkginfo, data->cyclic_graph_detected_callbackdata);
*/
    data->cyclic_start_pkginfo = NULL;
    result = 0;
  }
  //store circular loop information if part of circular loop
  if (data->cyclic_start_pkginfo) {
    PKG_XTRA(data->cyclic_start_pkginfo)->cyclic_size++;
    PKG_XTRA(pkginfo)->cyclic_start_pkginfo = data->cyclic_start_pkginfo;
    PKG_XTRA(pkginfo)->cyclic_next_pkginfo = data->parent_pkginfo;
  }
  //unmark package as being checked
  PKG_XTRA(pkginfo)->checkingcyclic = 0;
  //mark package as visited
  PKG_XTRA(pkginfo)->visited = 1;
  return result;
}

int check_node_for_cyclic_graph_iterate_dependencies (const char* entryname, struct check_node_for_cyclic_graph_struct* data)
{
  struct package_metadata_struct searchpkginfo;
  struct package_metadata_struct* pkginfo;
  int result = 0;
  //get entry from list
  searchpkginfo.datafield[PACKAGE_METADATA_INDEX_BASENAME] = (char*)entryname;
  if ((pkginfo = (struct package_metadata_struct*)sorted_unique_list_search(data->sortedpackagelist, (const char*)&searchpkginfo)) != NULL) {
    //don't check entry if it was checked before
    if (!PKG_XTRA(pkginfo)->visited) {
      //cyclic graph detected if package already being checked
      if (PKG_XTRA(pkginfo)->checkingcyclic) {
        data->count_loops++;
        data->cyclic_start_pkginfo = pkginfo;
        PKG_XTRA(pkginfo)->cyclic_size = 1;
        PKG_XTRA(pkginfo)->cyclic_start_pkginfo = pkginfo;
        PKG_XTRA(pkginfo)->cyclic_next_pkginfo = data->parent_pkginfo;
/*
        if (data->cyclic_graph_detected_callback)
          (*data->cyclic_graph_detected_callback)(pkginfo, data->current_link_type, pkginfo, data->cyclic_graph_detected_callbackdata);
*/
        return 1;
      }
      //otherwise recurse
      if (check_node_for_cyclic_graph(pkginfo, data)) {
        //cyclic graph detected
/*
        if (data->cyclic_graph_detected_callback)
          (*data->cyclic_graph_detected_callback)(pkginfo, data->current_link_type, PKG_XTRA(pkginfo)->cyclic_start_pkginfo, data->cyclic_graph_detected_callbackdata);
*/
        result = 1;
      }
    }
  }
  return result;
}

/*
void cyclic_graph_detected (struct package_metadata_struct* pkginfo, package_dependency_type dependency_type, struct package_metadata_struct* startpkginfo, void* callbackdata)
{
  static const char* package_dependency_type_left_arrow[] = {"|", "<-", "<=", "<~"};
  if (pkginfo == startpkginfo && dependency_type != none) {
    printf("Cyclic dependency detected for %s:\n  ", pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
  }
  printf("%s", pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME]);
  if (dependency_type != none) {
    printf(" %s ", package_dependency_type_left_arrow[dependency_type]);
  } else {
    printf("\n");
  }
}
*/

////////////////////////////////////////////////////////////////////////

//use topological sort algorithm for directed acyclic graph

struct topological_sort_depth_first_search_struct {
  sorted_unique_list* sortedpackagelist;
  size_t pkgorderpos;
  struct package_metadata_struct** pkgorder;
};

int topological_sort_depth_first_search_iterate_dependencies (const char* entryname, struct topological_sort_depth_first_search_struct* data);

void topological_sort_depth_first_search (struct package_metadata_struct* pkginfo, struct topological_sort_depth_first_search_struct* data)
{
  //mark package as visited
  PKG_XTRA(pkginfo)->visited = 1;
  //recursively process dependencies
  iterate_packages_in_list(pkginfo->dependencies, (package_callback_fn)topological_sort_depth_first_search_iterate_dependencies, data);
  iterate_packages_in_list(pkginfo->builddependencies, (package_callback_fn)topological_sort_depth_first_search_iterate_dependencies, data);
  if (!PKG_XTRA(pkginfo)->cyclic_start_pkginfo)
    iterate_packages_in_list(pkginfo->optionaldependencies, (package_callback_fn)topological_sort_depth_first_search_iterate_dependencies, data);
  ////else printf("Skipping optional dependencies for %s (%lu-step loop via: %s)\n", pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], (unsigned long)PKG_XTRA(PKG_XTRA(pkginfo)->cyclic_start_pkginfo)->cyclic_size, PKG_XTRA(pkginfo)->cyclic_start_pkginfo->basename);/////
  //store current package as next in line
  data->pkgorder[data->pkgorderpos++] = pkginfo;
}

int topological_sort_depth_first_search_iterate_dependencies (const char* entryname, struct topological_sort_depth_first_search_struct* data)
{
  struct package_metadata_struct searchpkginfo;
  struct package_metadata_struct* pkginfo;
  //get entry from list
  searchpkginfo.datafield[PACKAGE_METADATA_INDEX_BASENAME] = (char*)entryname;
  if ((pkginfo = (struct package_metadata_struct*)sorted_unique_list_search(data->sortedpackagelist, (const char*)&searchpkginfo)) != NULL) {
    //recurse if not already visited
    if (!PKG_XTRA(pkginfo)->visited) {
      topological_sort_depth_first_search(pkginfo, data);
    }
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////

struct package_info_list_struct* generate_build_list (sorted_unique_list* sortedpackagelist)
{
  size_t i;
  size_t n;
  struct package_metadata_struct* pkginfo;
  struct check_node_for_cyclic_graph_struct cyclic_check_data;
  struct topological_sort_depth_first_search_struct topsort_data;
  struct package_info_list_struct* packagebuildlist = NULL;
  struct package_info_list_struct** packagebuildlistnext = &packagebuildlist;

  //check if directed graph is cyclic
  cyclic_check_data.sortedpackagelist = sortedpackagelist;
  cyclic_check_data.count_loops = 0;
  cyclic_check_data.parent_pkginfo = NULL;
  cyclic_check_data.cyclic_start_pkginfo = NULL;
  if ((n = sorted_unique_list_size(sortedpackagelist)) > 0) {
    //process all entries
    for (i = 0; i < n; i++) {
      pkginfo = (struct package_metadata_struct*)sorted_unique_list_get(sortedpackagelist, i);
      if (!PKG_XTRA(pkginfo)->visited) {
        check_node_for_cyclic_graph(pkginfo, &cyclic_check_data);
      }
    }
    //clear visited flags
    for (i = 0; i < n; i++) {
      pkginfo = (struct package_metadata_struct*)sorted_unique_list_get(sortedpackagelist, i);
      PKG_XTRA(pkginfo)->visited = 0;
    }
  }

  //determine build order based on dependencies using topological sort algorithm for directed acyclic graph
  if ((n = sorted_unique_list_size(sortedpackagelist)) > 0) {
    if ((topsort_data.pkgorder = (struct package_metadata_struct**)malloc(n * sizeof(struct package_metadata_struct*))) == NULL) {
      fprintf(stderr, "Memory allocation error\n");
      return NULL;
    }
    topsort_data.sortedpackagelist = sortedpackagelist;
    topsort_data.pkgorderpos = 0;
    for (i = 0; i < n; i++) {
      pkginfo = (struct package_metadata_struct*)sorted_unique_list_get(sortedpackagelist, i);
      if (!PKG_XTRA(pkginfo)->visited) {
        topological_sort_depth_first_search(pkginfo, &topsort_data);
      }
    }
  }

  //create build order list
  for (i = 0; i < n; i++) {
    //add package to list
    if ((*packagebuildlistnext = (struct package_info_list_struct*)malloc(sizeof(struct package_info_list_struct))) == NULL) {
      fprintf(stderr, "Memory allocation error\n");
      return NULL;
    }
    (*packagebuildlistnext)->info = topsort_data.pkgorder[i];
    (*packagebuildlistnext)->next = NULL;
    packagebuildlistnext = &(*packagebuildlistnext)->next;
    //when part of cyclic loop rebuild packages as needed
    if (PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo && PKG_XTRA(PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo)->cyclic_size > 0) {
      PKG_XTRA(PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo)->cyclic_size--;
      if (PKG_XTRA(PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo)->cyclic_size == 0) {
        pkginfo = PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo;
        struct package_metadata_struct* pkginfo;
        pkginfo = PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo;
        //skip to current package
        while (pkginfo && pkginfo != topsort_data.pkgorder[i])
          pkginfo = PKG_XTRA(pkginfo)->cyclic_next_pkginfo;
        while ((pkginfo = PKG_XTRA(pkginfo)->cyclic_next_pkginfo) != NULL) {
          //add package to list
          if ((*packagebuildlistnext = (struct package_info_list_struct*)malloc(sizeof(struct package_info_list_struct))) == NULL) {
            fprintf(stderr, "Memory allocation error\n");
            return NULL;
          }
          (*packagebuildlistnext)->info = pkginfo;
          (*packagebuildlistnext)->next = NULL;
          packagebuildlistnext = &(*packagebuildlistnext)->next;
          //abort when circular loop completed
          if (pkginfo == PKG_XTRA(topsort_data.pkgorder[i])->cyclic_start_pkginfo)
            break;
        }
      }
    }
  }
  free(topsort_data.pkgorder);
  return packagebuildlist;
}

////////////////////////////////////////////////////////////////////////

struct dependancies_listed_but_not_depended_on_struct {
  size_t countmissing;
  sorted_unique_list* dependencies;
};

int dependancies_listed_but_not_depended_on_iteration (const char* basename, void* callbackdata)
{
  if (!sorted_unique_list_find(((struct dependancies_listed_but_not_depended_on_struct*)callbackdata)->dependencies, basename))
    ((struct dependancies_listed_but_not_depended_on_struct*)callbackdata)->countmissing++;
  return 0;
}

size_t dependancies_listed_but_not_depended_on (const char* dstdir, struct package_metadata_struct* pkginfo)
{
  struct memory_buffer* pkginfopath = memory_buffer_create();
  struct memory_buffer* filepath = memory_buffer_create();
  struct dependancies_listed_but_not_depended_on_struct data = {0, sorted_unique_list_create(strcmp, free)};
  //determine package information path
/*
  TO DO: use package sqlite3 database
*/
  memory_buffer_set_printf(pkginfopath, "%s%s%c%s%c", dstdir, PACKAGE_INFO_PATH, PATH_SEPARATOR, pkginfo->datafield[PACKAGE_METADATA_INDEX_BASENAME], PATH_SEPARATOR);
  //check dependancies
  sorted_unique_list_load_from_file(&data.dependencies, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_DEPENDENCIES_FILE)));
  iterate_packages_in_list(pkginfo->dependencies, dependancies_listed_but_not_depended_on_iteration, &data);
  sorted_unique_list_clear(data.dependencies);
  //check optional dependancies
  sorted_unique_list_load_from_file(&data.dependencies, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_OPTIONALDEPENDENCIES_FILE)));
  iterate_packages_in_list(pkginfo->optionaldependencies, dependancies_listed_but_not_depended_on_iteration, &data);
  sorted_unique_list_clear(data.dependencies);
  //check build dependancies
  sorted_unique_list_load_from_file(&data.dependencies, memory_buffer_get(memory_buffer_set_printf(filepath, "%s%s", memory_buffer_get(pkginfopath), PACKAGE_INFO_BUILDDEPENDENCIES_FILE)));
  iterate_packages_in_list(pkginfo->builddependencies, dependancies_listed_but_not_depended_on_iteration, &data);
  //clean up
  sorted_unique_list_free(data.dependencies);
  memory_buffer_free(pkginfopath);
  memory_buffer_free(filepath);
  return data.countmissing;
}

