#include <stdint.h>
#include <string.h>

#include <lwhttp.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void check_parts(const char *s, const lwhttp_url_t *u)
{
  const char *end = s + strlen(s);
  if (u->has_scheme && u->scheme + u->scheme_len > end) __builtin_trap();
  if (u->has_authority && u->authority + u->authority_len > end) __builtin_trap();
  if (u->host && u->host + u->host_len > end) __builtin_trap();
  if (u->path + u->path_len > end) __builtin_trap();
  if (u->has_query && u->query + u->query_len > end) __builtin_trap();
  if (u->has_fragment && u->fragment + u->fragment_len > end) __builtin_trap();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  static char input[512];
  static char out[640];
  lwhttp_url_t u;
  char *ref;
  size_t cap;
  if (size < 1 || size >= sizeof(input)) return 0;
  cap = (size_t)data[0] % sizeof(out) + 1;
  memcpy(input, data + 1, size - 1);
  input[size - 1] = '\0';
  if (lwhttp_url_parse(input, &u) == LWHTTP_OK) check_parts(input, &u);
  ref = strchr(input, '\n');
  if (!ref) return 0;
  *ref++ = '\0';
  if (lwhttp_url_parse(ref, &u) == LWHTTP_OK) check_parts(ref, &u);
  if (lwhttp_url_resolve(input, ref, out, cap) == LWHTTP_OK) {
    if (strlen(out) >= cap) __builtin_trap();
    if (lwhttp_url_parse(out, &u) != LWHTTP_OK || !u.has_scheme) __builtin_trap();
  }
  return 0;
}
