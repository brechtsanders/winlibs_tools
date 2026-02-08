#ifdef WITH_LIBXDIFF

#include "generatediff.h"

#include <xdiff.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#endif

#define XDLT_STD_BLKSIZE 8 * 1024

void *wrap_malloc (void* priv, unsigned int size)
{
  return malloc(size);
}

void wrap_free (void* priv, void *ptr)
{
  free(ptr);
}

void *wrap_realloc (void* priv, void* ptr, unsigned int size)
{
  return realloc(ptr, size);
}

/*
int xdiff_file_output (void* context, mmbuffer_t* data, int n)
{
  int i;
  for (i = 0; i < n; i++) {
    fwrite(data[i].ptr, data[i].size, 1, (FILE*)context);
  }
  return 0;
}
*/

#define CHARS_TO_ESCAPE "\\$`"

int xdiff_file_output_escaped (void* context, mmbuffer_t* data, int n)
{
  int i;
  int j;
  char* p;
  for (i = 0; i < n; i++) {
    p = data[i].ptr;
    for (j = 0; j < data[i].size; j++) {
      if (*p == '\\' || *p == '$' || *p == '`')
        putc('\\', (FILE*)context);
      putc(*p, (FILE*)context);
      p++;
    }
  }
  return 0;
}

struct memoryfile {
#ifdef _WIN32
  HANDLE mapping;
#endif
  size_t filesize;
  char* data;
  mmfile_t xdiff_memfile;
};

void xdiff_memfile_close (struct memoryfile* memfile)
{
#ifdef _WIN32
  UnmapViewOfFile(memfile->data);
  CloseHandle(memfile->mapping);
#else
  munmap(memfile->data, memfile->filesize);
#endif
  xdl_free_mmfile(&memfile->xdiff_memfile);
  free(memfile);
}

struct memoryfile* xdiff_memfile_open (const char* filename)
{
#ifdef _WIN32
  HANDLE src;
  LARGE_INTEGER srcsize;
#else
  int src;
  struct stat srcinfo;
#endif
  struct memoryfile* memfile;
  //open file and
#ifdef _WIN32
  if ((src = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE) {
  //if ((src = OpenFileMappingA(FILE_MAP_READ, FALSE, filename)) == NULL) {
#else
  if ((src = open(filename, O_RDONLY)) == -1) {
#endif
    fprintf(stderr, "Error opening file: %s\n", filename);
    return NULL;
  }
  //allocate libxdiff memory file structure
  if ((memfile = (struct memoryfile*)malloc(sizeof(struct memoryfile))) == NULL) {
    fprintf(stderr, "Memory allocation error\n");
    return NULL;
  }
  //get file size
#ifdef _WIN32
  if (!GetFileSizeEx(src, &srcsize)) {
    fprintf(stderr, "Error checking file: %s\n", filename);
    free(memfile);
    return NULL;
  }
  memfile->filesize = srcsize.QuadPart;
#else
  if (fstat(src, &srcinfo) != 0) {
    fprintf(stderr, "Error checking file: %s\n", filename);
    free(memfile);
    return NULL;
  }
  memfile->filesize = srcinfo.st_size;
#endif
  //map file to memory
#ifdef _WIN32
  if ((memfile->mapping = CreateFileMappingA(src, NULL, PAGE_READONLY, srcsize.HighPart, srcsize.LowPart, NULL)) == NULL) {
    fprintf(stderr, "Error creating file mapping for: %s\n", filename);
    CloseHandle(src);
    free(memfile);
    return NULL;
  }
  if ((memfile->data = MapViewOfFile(memfile->mapping, FILE_MAP_READ, 0, 0, srcsize.QuadPart)) == NULL) {
#else
  if ((memfile->data = mmap(NULL, memfile->filesize, PROT_READ, MAP_FILE|MAP_PRIVATE, src, 0)) == MAP_FAILED) {
#endif
    fprintf(stderr, "Error mapping file to memory: %s\n", filename);
#ifdef _WIN32
    UnmapViewOfFile(memfile->data);
    CloseHandle(src);
#else
    close(src);
#endif
    free(memfile);
    return NULL;
  }
  //close file (will not invalidate the mapping)
#ifdef _WIN32
  CloseHandle(src);
#else
  close(src);
#endif
  //prepare libxdiff structure for file data
  xdl_init_mmfile(&memfile->xdiff_memfile, memfile->filesize, 0);
  if (xdl_mmfile_ptradd(&memfile->xdiff_memfile, memfile->data, memfile->filesize, XDL_MMB_READONLY) != memfile->filesize) {
    fprintf(stderr, "Error assigning pointer for memory mapped file: %s\n", filename);
    xdiff_memfile_close(memfile);
    return NULL;
  }
  return memfile;
}

struct diff_handle_struct {
  struct memoryfile* file1;
  struct memoryfile* file2;
};

int diff_initialize ()
{
  memallocator_t xdiff_mem_functions;
  xdiff_mem_functions.priv = NULL;
  xdiff_mem_functions.malloc = wrap_malloc;
  xdiff_mem_functions.free = wrap_free;
  xdiff_mem_functions.realloc = wrap_realloc;
  return xdl_set_allocator(&xdiff_mem_functions);
}

diff_handle diff_create (const char* filename1, const char* filename2)
{
  struct diff_handle_struct* handle;
  if ((handle = (struct diff_handle_struct*)malloc(sizeof(struct diff_handle_struct))) == NULL)
    return NULL;
  if ((handle->file1 = xdiff_memfile_open(filename1)) == NULL) {
    free(handle);
    return NULL;
  }
  if ((handle->file2 = xdiff_memfile_open(filename2)) == NULL) {
    xdiff_memfile_close(handle->file1);
    free(handle);
    return NULL;
  }
  return handle;
}

void diff_free (diff_handle handle)
{
  xdiff_memfile_close(handle->file1);
  xdiff_memfile_close(handle->file2);
  free(handle);
}

int diff_generate (diff_handle handle, int context, FILE* dst)
{
  xpparam_t xdiff_param;
  xdemitconf_t xdiff_cfg;
  xdemitcb_t xdiff_output;
  xdiff_param.flags = 0; //alternative (slower): XDF_NEED_MINIMAL
  xdiff_cfg.ctxlen = context;
  xdiff_output.priv = dst;
  xdiff_output.outf = &xdiff_file_output_escaped;
  return xdl_diff(&handle->file1->xdiff_memfile, &handle->file2->xdiff_memfile, &xdiff_param, &xdiff_cfg, &xdiff_output);
}

#endif
