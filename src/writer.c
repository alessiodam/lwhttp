#include <string.h>

#include "internal.h"

void lwhttp_buffer_init(lwhttp_buffer_t *buffer, char *data, size_t size)
{
  buffer->data = data;
  buffer->size = size;
  buffer->len = 0;
}

LWHTTP_NOINLINE void lwhttp_buffer_append(lwhttp_buffer_t *buffer, const void *data, size_t len)
{
  if (len && buffer->len <= buffer->size && len <= buffer->size - buffer->len)
    memcpy(buffer->data + buffer->len, data, len);
  buffer->len += len;
}

void lwhttp_buffer_append_str(lwhttp_buffer_t *buffer, const char *s) { lwhttp_buffer_append(buffer, s, strlen(s)); }

void lwhttp_buffer_append_u32(lwhttp_buffer_t *buffer, uint32_t value)
{
  char tmp[10];
  size_t i = sizeof(tmp);
  do {
    uint32_t q = value / 10;
    tmp[--i] = (char)('0' + (uint8_t)(value - q * 10));
    value = q;
  } while (value);
  lwhttp_buffer_append(buffer, tmp + i, sizeof(tmp) - i);
}

bool lwhttp_buffer_ok(const lwhttp_buffer_t *buffer) { return buffer->len <= buffer->size; }

bool lwhttp_write_header(lwhttp_buffer_t *buffer, const char *name, const char *value)
{
  if (!lwhttp_is_token(name) || !value || !lwhttp__value_ok(value, strlen(value))) return false;
  lwhttp_buffer_append_str(buffer, name);
  lwhttp_buffer_append(buffer, ": ", 2);
  lwhttp_buffer_append_str(buffer, value);
  lwhttp_buffer_append(buffer, "\r\n", 2);
  return true;
}

void lwhttp_write_end(lwhttp_buffer_t *buffer) { lwhttp_buffer_append(buffer, "\r\n", 2); }
