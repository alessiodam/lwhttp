#include <stdint.h>
#include <string.h>

#include <lwhttp.h>

static char line[512];
static char store[1024];
static lwhttp_headers_t headers;

static int on_field(lwhttp_parser_t *p, const char *name, size_t name_len, const char *value, size_t value_len)
{
  (void)p;
  if (strlen(name) != name_len || strlen(value) != value_len || !name_len) __builtin_trap();
  lwhttp_headers_add(&headers, name, value);
  return 0;
}

static int on_body(lwhttp_parser_t *p, const uint8_t *data, size_t len)
{
  (void)p;
  if (!len || !data) __builtin_trap();
  return 0;
}

static const lwhttp_parser_callbacks_t callbacks = {NULL, on_field, NULL, on_body, on_field, NULL};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  static const char *const methods[] = {"GET", "HEAD", "CONNECT", NULL};
  lwhttp_parser_t p;
  size_t off = 0;
  size_t step;
  if (size < 2) return 0;
  lwhttp_headers_init(&headers, store, sizeof(store));
  lwhttp_parser_init(&p, line, (size_t)(data[0] & 0x80 ? 64 : sizeof(line)), &callbacks, NULL);
  lwhttp_parser_set_request(&p, methods[data[0] & 3]);
  step = (size_t)data[1] + 1;
  data += 2;
  size -= 2;
  while (off < size) {
    size_t n = size - off < step ? size - off : step;
    size_t used = lwhttp_parser_execute(&p, data + off, n);
    if (used > n) __builtin_trap();
    if (lwhttp_parser_error(&p) || !used) return 0;
    off += used;
  }
  lwhttp_parser_finish(&p);
  if (lwhttp_headers_count(&headers) > sizeof(store)) __builtin_trap();
  return 0;
}
