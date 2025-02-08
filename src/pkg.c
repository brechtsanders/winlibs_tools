#include "pkg.h"
#include <stdlib.h>
#include <string.h>

const char* package_metadata_field_name[] = {
  "Name",
  "Version",
  "Title",
  "Description",
  "Website URL",
  "Downloads URL",
  "Source code URL",
  "Category",
  "Type",
  "Version date",
  "License file",
  "License type",
  "Status",
  NULL
};

struct package_metadata_struct* package_metadata_create ()
{
  int i;
  struct package_metadata_struct* pkginfo;
  if ((pkginfo = (struct package_metadata_struct*)malloc(sizeof(struct package_metadata_struct))) == NULL)
    return NULL;
  for (i = 0; i < PACKAGE_METADATA_TOTAL_FIELDS; i++)
    pkginfo->datafield[i] = NULL;
  pkginfo->fileexclusions = sorted_unique_list_create(strcmp, free);
  pkginfo->folderexclusions = sorted_unique_list_create(strcmp, free);
  pkginfo->filelist = sorted_unique_list_create(strcmp, free);
  pkginfo->folderlist = sorted_unique_list_create(strcmp, free);
  pkginfo->dependencies = sorted_unique_list_create(strcmp, free);
  pkginfo->optionaldependencies = sorted_unique_list_create(strcmp, free);
  pkginfo->builddependencies = sorted_unique_list_create(strcmp, free);
  pkginfo->optionalbuilddependencies = sorted_unique_list_create(strcmp, free);
  pkginfo->totalsize = 0;
  pkginfo->version_linenumber = 0;
  pkginfo->nextversion_linenumber = 0;
  pkginfo->buildok = 0;
  pkginfo->lastchanged = 0;
  pkginfo->extradata = NULL;
  pkginfo->extradata_free_fn = NULL;
  return pkginfo;
}

void package_metadata_free (struct package_metadata_struct* pkginfo)
{
  int i;
  if (!pkginfo)
    return;
  for (i = 0; i < PACKAGE_METADATA_TOTAL_FIELDS; i++)
    if (pkginfo->datafield[i])
      free(pkginfo->datafield[i]);
  sorted_unique_list_free(pkginfo->fileexclusions);
  sorted_unique_list_free(pkginfo->folderexclusions);
  sorted_unique_list_free(pkginfo->filelist);
  sorted_unique_list_free(pkginfo->folderlist);
  sorted_unique_list_free(pkginfo->dependencies);
  sorted_unique_list_free(pkginfo->optionaldependencies);
  sorted_unique_list_free(pkginfo->builddependencies);
  sorted_unique_list_free(pkginfo->optionalbuilddependencies);
  if (pkginfo->extradata && pkginfo->extradata_free_fn)
    (*(pkginfo->extradata_free_fn))(pkginfo->extradata);
  free(pkginfo);
}
