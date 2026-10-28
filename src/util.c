#include <string.h>

#include "internal.h"

static const char *const error_table[] = {
    "ok",
    "invalid argument",
    "out of memory",
    "invalid state",
    "invalid url",
    "dns failure",
    "connect failed",
    "tls failure",
    "send failed",
    "connection closed",
    "protocol error",
    "unsupported http version",
    "unsupported feature",
    "message too large",
    "header section too large",
    "redirect failed",
    "aborted",
    "timed out",
};

const char *lwhttp_version_string(void) { return LWHTTP_VERSION_STRING; }

const char *lwhttp_error_string(lwhttp_error_t error)
{
  if ((size_t)error >= sizeof(error_table) / sizeof(error_table[0])) return "unknown error";
  return error_table[error];
}

const uint8_t lwhttp__tchar_map[16] = {0x00, 0x00, 0x00, 0x00, 0xfa, 0x6c, 0xff, 0x03,
                                       0xfe, 0xff, 0xff, 0xc7, 0xff, 0xff, 0xff, 0x57};

bool lwhttp__ieq(const char *a, const char *b)
{
  while (*a && LWHTTP_LOWER(*a) == LWHTTP_LOWER(*b)) {
    a++;
    b++;
  }
  return *a == *b;
}

bool lwhttp__casen(const char *a, const char *b, size_t len)
{
  size_t i;
  for (i = 0; i < len; i++)
    if (LWHTTP_LOWER(a[i]) != LWHTTP_LOWER(b[i])) return false;
  return true;
}

bool lwhttp__ieqn(const char *a, size_t len, const char *b) { return lwhttp__casen(a, b, len) && b[len] == '\0'; }

bool lwhttp__list_next(const char **cursor, const char **token, size_t *len)
{
  const char *s = *cursor;
  bool quoted = false;
  while (*s == ',' || LWHTTP_IS_OWS(*s)) s++;
  if (!*s) {
    *cursor = s;
    return false;
  }
  *token = s;
  while (*s && *s != ',' && *s != ';' && !LWHTTP_IS_OWS(*s)) s++;
  *len = (size_t)(s - *token);
  while (*s && (quoted || *s != ',')) {
    if (quoted && *s == '\\' && s[1]) s++;
    else if (*s == '"') quoted = !quoted;
    s++;
  }
  *cursor = s;
  return true;
}

bool lwhttp__value_ok(const char *value, size_t len)
{
  const uint8_t *p = (const uint8_t *)value;
  for (; len; len--, p++)
    if ((uint8_t)(*p - 0x20) >= 0x5f && *p < 0x80 && *p != '\t') return false;
  return true;
}

bool lwhttp__target_ok(const char *s, size_t len)
{
  size_t i;
  for (i = 0; i < len; i++)
    if ((uint8_t)s[i] <= 0x20 || (uint8_t)s[i] == 0x7f) return false;
  return true;
}

bool lwhttp_is_token(const char *s)
{
  if (!s || !*s) return false;
  for (; *s; s++)
    if (!LWHTTP_TCHAR(*s)) return false;
  return true;
}

bool lwhttp_header_has_token(const char *value, const char *token)
{
  const char *cursor = value;
  const char *item;
  size_t len;
  if (!value || !token) return false;
  while (lwhttp__list_next(&cursor, &item, &len))
    if (lwhttp__ieqn(item, len, token)) return true;
  return false;
}
