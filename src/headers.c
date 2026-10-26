#include <string.h>

#include "internal.h"

void lwhttp_headers_init(lwhttp_headers_t *headers, char *data, size_t size)
{
  headers->data = data;
  headers->size = size;
  headers->len = 0;
  headers->count = 0;
}

void lwhttp_headers_clear(lwhttp_headers_t *headers)
{
  headers->len = 0;
  headers->count = 0;
}

lwhttp_error_t lwhttp__headers_put(lwhttp_headers_t *headers, const char *name, size_t name_len, const char *value,
                                   size_t value_len)
{
  char *p = headers->data + headers->len;
  size_t total = name_len + value_len + 2;
  if (total > headers->size - headers->len) return LWHTTP_ERR_HEADERS_TOO_LARGE;
  memmove(p, name, name_len);
  p[name_len] = '\0';
  memmove(p + name_len + 1, value, value_len);
  p[total - 1] = '\0';
  headers->len += total;
  headers->count++;
  return LWHTTP_OK;
}

lwhttp_error_t lwhttp_headers_add(lwhttp_headers_t *headers, const char *name, const char *value)
{ return lwhttp__headers_put(headers, name, strlen(name), value, strlen(value)); }

const char *lwhttp_headers_get(const lwhttp_headers_t *headers, const char *name)
{
  const char *p;
  const char *end;
  if (!headers || !name) return NULL;
  p = headers->data;
  end = p + headers->len;
  while (p < end) {
    const char *v = p + strlen(p) + 1;
    if (lwhttp__ieq(p, name)) return v;
    p = v + strlen(v) + 1;
  }
  return NULL;
}

bool lwhttp_headers_at(const lwhttp_headers_t *headers, size_t index, const char **name, const char **value)
{
  const char *p;
  if (!headers || index >= headers->count) return false;
  p = headers->data;
  while (index--) {
    p += strlen(p) + 1;
    p += strlen(p) + 1;
  }
  if (name) *name = p;
  if (value) *value = p + strlen(p) + 1;
  return true;
}

size_t lwhttp_headers_count(const lwhttp_headers_t *headers) { return headers ? headers->count : 0; }
