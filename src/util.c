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
};

static const char weekdays[] = "SunMonTueWedThuFriSat";
static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";

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

static int32_t days_from_civil(int32_t y, int32_t m, int32_t d)
{
  int32_t era, yoe, doy, doe;
  y -= m <= 2;
  era = (y >= 0 ? y : y - 399) / 400;
  yoe = y - era * 400;
  doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

static void put2(char *out, uint32_t v)
{
  char tens = '0';
  uint8_t n = (uint8_t)v;
  while (n >= 10) {
    n -= 10;
    tens++;
  }
  out[0] = tens;
  out[1] = (char)('0' + n);
}

size_t lwhttp_date_format(uint32_t timestamp, char *out)
{
  int32_t z = (int32_t)(timestamp / 86400ul);
  uint32_t secs = timestamp % 86400ul;
  int32_t wday = (z + 4) % 7;
  int32_t era, doe, yoe, y, doy, mp, d, m;
  z += 719468;
  era = z / 146097;
  doe = z - era * 146097;
  yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = yoe + era * 400;
  doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
  memcpy(out, weekdays + wday * 3, 3);
  out[3] = ',';
  out[4] = ' ';
  put2(out + 5, (uint32_t)d);
  out[7] = ' ';
  memcpy(out + 8, months + (m - 1) * 3, 3);
  out[11] = ' ';
  put2(out + 12, (uint32_t)(y / 100));
  put2(out + 14, (uint32_t)(y % 100));
  out[16] = ' ';
  put2(out + 17, secs / 3600ul);
  out[19] = ':';
  put2(out + 20, secs / 60ul % 60ul);
  out[22] = ':';
  put2(out + 23, secs % 60ul);
  memcpy(out + 25, " GMT", 5);
  return LWHTTP_DATE_SIZE - 1;
}

static bool digits(const char **s, size_t count, int32_t *out)
{
  int32_t v = 0;
  size_t i;
  for (i = 0; i < count; i++) {
    if (!LWHTTP_IS_DIGIT((*s)[i])) return false;
    v = v * 10 + ((*s)[i] - '0');
  }
  *s += count;
  *out = v;
  return true;
}

static bool expect(const char **s, char c)
{
  if (**s != c) return false;
  (*s)++;
  return true;
}

static bool month(const char **s, int32_t *out)
{
  int32_t i;
  for (i = 0; i < 12; i++) {
    if (!strncmp(*s, months + i * 3, 3)) {
      *s += 3;
      *out = i + 1;
      return true;
    }
  }
  return false;
}

static bool clock_time(const char **s, int32_t *h, int32_t *mi, int32_t *sec)
{ return digits(s, 2, h) && expect(s, ':') && digits(s, 2, mi) && expect(s, ':') && digits(s, 2, sec); }

lwhttp_error_t lwhttp_date_parse(const char *s, uint32_t *timestamp)
{
  const char *p = s;
  int32_t y, mo, d, h, mi, sec, days;
  if (!s || !timestamp) return LWHTTP_ERR_ARG;
  while (LWHTTP_IS_ALPHA(*p)) p++;
  if (p == s) return LWHTTP_ERR_PROTOCOL;
  if (*p == ',') {
    p++;
    if (!expect(&p, ' ') || !digits(&p, 2, &d)) return LWHTTP_ERR_PROTOCOL;
    if (*p == ' ') {
      p++;
      if (!month(&p, &mo) || !expect(&p, ' ') || !digits(&p, 4, &y)) return LWHTTP_ERR_PROTOCOL;
    } else {
      if (!expect(&p, '-') || !month(&p, &mo) || !expect(&p, '-') || !digits(&p, 2, &y)) return LWHTTP_ERR_PROTOCOL;
      y += y < 70 ? 2000 : 1900;
    }
    if (!expect(&p, ' ') || !clock_time(&p, &h, &mi, &sec) || strcmp(p, " GMT")) return LWHTTP_ERR_PROTOCOL;
  } else if (*p == ' ') {
    p++;
    if (!month(&p, &mo) || !expect(&p, ' ')) return LWHTTP_ERR_PROTOCOL;
    if (*p == ' ') {
      p++;
      if (!digits(&p, 1, &d)) return LWHTTP_ERR_PROTOCOL;
    } else if (!digits(&p, 2, &d)) {
      return LWHTTP_ERR_PROTOCOL;
    }
    if (!expect(&p, ' ') || !clock_time(&p, &h, &mi, &sec) || !expect(&p, ' ') || !digits(&p, 4, &y) || *p)
      return LWHTTP_ERR_PROTOCOL;
  } else {
    return LWHTTP_ERR_PROTOCOL;
  }
  if (d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60 || y < 1970 || y > 2105) return LWHTTP_ERR_PROTOCOL;
  if (sec == 60) sec = 59;
  days = days_from_civil(y, mo, d);
  *timestamp = (uint32_t)days * 86400ul + (uint32_t)(h * 3600 + mi * 60 + sec);
  return LWHTTP_OK;
}
