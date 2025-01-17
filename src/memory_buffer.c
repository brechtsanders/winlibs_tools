#include "memory_buffer.h"
#include <string.h>
#include <stdio.h>

struct memory_buffer* memory_buffer_create ()
{
  return memory_buffer_create_from_allocated_string(NULL);
}

struct memory_buffer* memory_buffer_create_from_string (const char* data)
{
  char* s;
  if ((s = strdup(data)) == NULL)
    return NULL;
  return memory_buffer_create_from_allocated_string(s);
}

struct memory_buffer* memory_buffer_create_from_allocated_string (char* data)
{
  struct memory_buffer* membuf;
  if ((membuf = (struct memory_buffer*)malloc(sizeof(struct memory_buffer))) != NULL) {
    membuf->datalen = (data ? strlen(data) : 0);
    membuf->data = data;
  }
  return membuf;
}

void memory_buffer_free (struct memory_buffer* membuf)
{
  if (!membuf)
    return;
  free(membuf->data);
  free(membuf);
}

char* memory_buffer_free_to_allocated_string (struct memory_buffer* membuf)
{
  char* result;
  if (!membuf)
    return NULL;
  if (membuf->datalen == 0 || (membuf->datalen > 0 && membuf->data[membuf->datalen - 1] != 0)) {
    char nullterminator = 0;
    memory_buffer_append_buf(membuf, &nullterminator, 1);
  }
  result = membuf->data;
  free(membuf);
  return result;
}

const char* memory_buffer_get (struct memory_buffer* membuf)
{
  if (!membuf)
    return NULL;
  if (membuf->datalen > 0 && membuf->data[membuf->datalen - 1] != 0) {
    if ((membuf->data = (char*)realloc(membuf->data, membuf->datalen + 1)) == NULL)
      return NULL;
    membuf->data[membuf->datalen] = 0;
  }
  return membuf->data;
}

size_t memory_buffer_length (struct memory_buffer* membuf)
{
  if (!membuf)
    return 0;
  return membuf->datalen;
}

struct memory_buffer* memory_buffer_set (struct memory_buffer* membuf, const char* data)
{
  size_t datalen;
  if (!membuf)
    return NULL;
  datalen = (data ? strlen(data) : 0);
  if ((membuf->data = (char*)realloc(membuf->data, datalen)) == NULL) {
    membuf->datalen = 0;
    return NULL;
  }
  memcpy(membuf->data, data, datalen);
  membuf->datalen = datalen;
  return membuf;
}

struct memory_buffer* memory_buffer_set_allocated (struct memory_buffer* membuf, char* data)
{
  if (!membuf)
    return NULL;
  free(membuf->data);
  if (!data) {
    membuf->data = NULL;
    membuf->datalen = 0;
    return NULL;
  }
  membuf->data = data;
  membuf->datalen = strlen(data);
  return membuf;
}

size_t memory_buffer_grow (struct memory_buffer* membuf, size_t newsize)
{
  if (newsize > membuf->datalen) {
    if ((membuf->data = (char*)realloc(membuf->data, (membuf->datalen = newsize))) == NULL) {
      membuf->datalen = 0;
      return 0;
    }
  }
  return membuf->datalen;
}

int memory_buffer_append (struct memory_buffer* membuf, const char* data)
{
  return memory_buffer_append_buf(membuf, data, strlen(data));
}

int memory_buffer_append_buf (struct memory_buffer* membuf, const char* data, size_t datalen)
{
  if (!membuf || !data)
    return 0;
  if ((membuf->data = (char*)realloc(membuf->data, membuf->datalen + datalen)) == NULL) {
    membuf->datalen = 0;
    return 0;
  }
  memcpy(membuf->data + membuf->datalen, data, datalen);
  membuf->datalen += datalen;
  return datalen;
}

struct memory_buffer* memory_buffer_set_printf (struct memory_buffer* membuf, const char* format, ...)
{
  int result;
  va_list args;
  va_list args2;
  if (!membuf || !format)
    return NULL;
  va_copy(args2, args);
  va_start(args, format);
  if ((result = vsnprintf(NULL, 0, format, args)) > 0) {
    if ((membuf->data = (char*)realloc(membuf->data, result + 1)) == NULL) {
      membuf->datalen = 0;
      result = -1;
    } else {
      va_start(args2, format);
      if ((result = vsnprintf(membuf->data, result + 1, format, args2)) >= 0)
        membuf->datalen = result;
      else
        membuf->datalen = 0;
      va_end(args2);
    }
  }
  va_end(args);
  return membuf;
}

int memory_buffer_append_printf (struct memory_buffer* membuf, const char* format, ...)
{
  int result;
  va_list args;
  va_list args2;
  if (!membuf || !format)
    return -1;
  va_copy(args2, args);
  va_start(args, format);
  if ((result = vsnprintf(NULL, 0, format, args)) > 0) {
    if ((membuf->data = (char*)realloc(membuf->data, membuf->datalen + result + 1)) == NULL) {
      membuf->datalen = 0;
      result = -1;
    } else {
      va_start(args2, format);
      if ((result = vsnprintf(membuf->data + membuf->datalen, result + 1, format, args2)) > 0)
        membuf->datalen += result;
      va_end(args2);
    }
  }
  va_end(args);
  return result;
}

struct memory_buffer* memory_buffer_replace (struct memory_buffer* membuf, size_t pos, size_t len, const char* replacement)
{
  return memory_buffer_replace_data(membuf, pos, len, replacement, (replacement ? strlen(replacement) : 0));
}

struct memory_buffer* memory_buffer_replace_data (struct memory_buffer* membuf, size_t pos, size_t len, const char* replacement, size_t replacementlen)
{
  if (membuf->datalen > 0) {
    if (pos > membuf->datalen)
      pos = membuf->datalen;
    if (pos + len > membuf->datalen)
      len = membuf->datalen - pos;
    if (!replacement)
      replacementlen = 0;
    if (replacementlen > len)
      if (memory_buffer_grow(membuf, membuf->datalen - len + replacementlen) <= 0)
        return membuf;
    if (replacementlen != len)
      memmove(membuf->data + pos + replacementlen, membuf->data + pos + len, membuf->datalen - (pos + len) + 1);
    if (replacementlen)
      memcpy(membuf->data + pos, replacement, replacementlen);
  }
  return membuf;
}

struct memory_buffer* memory_buffer_find_replace_data (struct memory_buffer* membuf, const char* s1, const char* s2)
{
  const char* found;
  size_t s1len;
  size_t s2len;
  size_t pos = 0;
  if (s1 && (s1len = strlen(s1) > 0)) {
    s2len = (s2 ? strlen(s2) : 0);
    while ((found = strstr(membuf->data + pos, s1)) != NULL) {
      pos = found - membuf->data;
      memory_buffer_replace_data(membuf, pos, s1len, s2, s2len);
      pos += s2len;
    }
  }
  return membuf;
};

struct memory_buffer* memory_buffer_xml_special_chars (struct memory_buffer* membuf)
{
	size_t pos = 0;
	while (membuf->data && pos < membuf->datalen) {
		switch ((membuf->data)[pos]) {
			case '&' :
        memory_buffer_replace(membuf, pos, 1, "&amp;");
				pos += 5;
				break;
			case '\"' :
				memory_buffer_replace(membuf, pos, 1, "&quot;");
				pos += 6;
				break;
			case '\'' :
				memory_buffer_replace(membuf, pos, 1, "&apos;");
				pos += 6;
				break;
			case '<' :
				memory_buffer_replace(membuf, pos, 1, "&lt;");
				pos += 4;
				break;
			case '>' :
				memory_buffer_replace(membuf, pos, 1, "&gt;");
				pos += 4;
				break;
			case '\r' :
				memory_buffer_replace(membuf, pos, 1, "");
				break;
			default:
				pos++;
				break;
		}
	}
	return membuf;
}

static const char hexdigits[] = "0123456789ABCDEF";

struct memory_buffer* url_encode (struct memory_buffer* membuf)
{
  size_t pos = membuf->datalen;
  while (pos-- > 0) {
    if (membuf->data[pos] == ' ') {
      membuf->data[pos] = '+';
    } else if (membuf->data[pos] <= ' ' || strchr("+'\",;:=%?&#@*<>", membuf->data[pos]) != NULL) {
      char hex[3];
      hex[0] = '%';
      hex[1] = hexdigits[membuf->data[pos] >> 4];
      hex[2] = hexdigits[membuf->data[pos] & 0x0F];
      memory_buffer_replace_data(membuf, pos, 1, hex, 3);
    }
  }
  return membuf;
}

