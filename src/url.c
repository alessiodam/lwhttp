#include <string.h>

#include "internal.h"

static bool scheme_char(char c) { return LWHTTP_IS_ALPHA(c) || LWHTTP_IS_DIGIT(c) || c == '+' || c == '-' || c == '.'; }

lwhttp_error_t lwhttp_url_parse(const char *s, lwhttp_url_t *url)
{
  const char *p = s;
  const char *q;
  if (!s || !url) return LWHTTP_ERR_ARG;
  memset(url, 0, sizeof(*url));
  for (q = s; *q; q++)
    if ((uint8_t)*q <= 0x20 || (uint8_t)*q == 0x7f) return LWHTTP_ERR_URL;
  if (LWHTTP_IS_ALPHA(*p)) {
    q = p;
    while (scheme_char(*q)) q++;
    if (*q == ':') {
      url->scheme = p;
      url->scheme_len = (size_t)(q - p);
      url->has_scheme = true;
      p = q + 1;
    }
  }
  if (p[0] == '/' && p[1] == '/') {
    const char *a = p + 2;
    const char *end = a;
    const char *at = NULL;
    const char *h = a;
    while (*end && *end != '/' && *end != '?' && *end != '#') {
      if (*end == '@') at = end;
      end++;
    }
    url->has_authority = true;
    url->authority = a;
    url->authority_len = (size_t)(end - a);
    if (at) {
      url->has_userinfo = true;
      url->userinfo = a;
      url->userinfo_len = (size_t)(at - a);
      h = at + 1;
    }
    if (*h == '[') {
      q = memchr(h, ']', (size_t)(end - h));
      if (!q) return LWHTTP_ERR_URL;
      url->host = h + 1;
      url->host_len = (size_t)(q - h - 1);
      url->ipv6 = true;
      q++;
      if (q != end && *q != ':') return LWHTTP_ERR_URL;
    } else {
      q = h;
      while (q < end && *q != ':') q++;
      url->host = h;
      url->host_len = (size_t)(q - h);
    }
    if (q < end && ++q < end) {
      uint32_t port = 0;
      while (q < end) {
        if (!LWHTTP_IS_DIGIT(*q)) return LWHTTP_ERR_URL;
        port = port * 10 + (uint32_t)(*q++ - '0');
        if (port > 65535ul) return LWHTTP_ERR_URL;
      }
      url->port = (uint16_t)port;
      url->has_port = true;
    }
    p = end;
  }
  url->path = p;
  while (*p && *p != '?' && *p != '#') p++;
  url->path_len = (size_t)(p - url->path);
  if (*p == '?') {
    url->has_query = true;
    url->query = ++p;
    while (*p && *p != '#') p++;
    url->query_len = (size_t)(p - url->query);
  }
  if (*p == '#') {
    url->has_fragment = true;
    url->fragment = p + 1;
    url->fragment_len = strlen(p + 1);
  }
  return LWHTTP_OK;
}

static bool prefix(const char *s, size_t n, const char *pre, size_t len) { return n >= len && !memcmp(s, pre, len); }

static size_t pop_segment(const char *s, size_t out)
{
  while (out && s[out - 1] != '/') out--;
  return out ? out - 1 : 0;
}

static size_t remove_dots(char *s, size_t n)
{
  size_t in = 0;
  size_t out = 0;
  while (in < n) {
    size_t left = n - in;
    char *c = s + in;
    if (prefix(c, left, "../", 3)) {
      in += 3;
    } else if (prefix(c, left, "./", 2) || prefix(c, left, "/./", 3)) {
      in += 2;
    } else if (left == 2 && c[0] == '/' && c[1] == '.') {
      c[1] = '/';
      in++;
    } else if (prefix(c, left, "/../", 4)) {
      in += 3;
      out = pop_segment(s, out);
    } else if (left == 3 && !memcmp(c, "/..", 3)) {
      c[2] = '/';
      in += 2;
      out = pop_segment(s, out);
    } else if ((left == 1 && c[0] == '.') || (left == 2 && c[0] == '.' && c[1] == '.')) {
      in = n;
    } else {
      size_t start = in;
      if (s[in] == '/') in++;
      while (in < n && s[in] != '/') in++;
      memmove(s + out, s + start, in - start);
      out += in - start;
    }
  }
  return out;
}

lwhttp_error_t lwhttp_url_resolve(const char *base, const char *reference, char *out, size_t size)
{
  lwhttp_url_t b;
  lwhttp_url_t r;
  lwhttp_buffer_t o;
  const lwhttp_url_t *scheme;
  const lwhttp_url_t *auth = NULL;
  const lwhttp_url_t *query;
  size_t start;
  bool dots = true;
  if (!base || !reference || !out || !size) return LWHTTP_ERR_ARG;
  if (lwhttp_url_parse(base, &b) || lwhttp_url_parse(reference, &r) || !b.has_scheme) return LWHTTP_ERR_URL;
  scheme = r.has_scheme ? &r : &b;
  if (r.has_scheme || r.has_authority) auth = r.has_authority ? &r : NULL;
  else if (b.has_authority) auth = &b;
  lwhttp_buffer_init(&o, out, size - 1);
  lwhttp_buffer_append(&o, scheme->scheme, scheme->scheme_len);
  lwhttp_buffer_append(&o, ":", 1);
  if (auth) {
    lwhttp_buffer_append(&o, "//", 2);
    lwhttp_buffer_append(&o, auth->authority, auth->authority_len);
  }
  start = o.len;
  query = &r;
  if (r.has_scheme || r.has_authority) {
    lwhttp_buffer_append(&o, r.path, r.path_len);
  } else if (!r.path_len) {
    lwhttp_buffer_append(&o, b.path, b.path_len);
    dots = false;
    if (!r.has_query) query = &b;
  } else {
    if (r.path[0] != '/') {
      if (b.has_authority && !b.path_len) {
        lwhttp_buffer_append(&o, "/", 1);
      } else {
        size_t n = b.path_len;
        while (n && b.path[n - 1] != '/') n--;
        lwhttp_buffer_append(&o, b.path, n);
      }
    }
    lwhttp_buffer_append(&o, r.path, r.path_len);
  }
  if (!lwhttp_buffer_ok(&o)) return LWHTTP_ERR_TOO_LARGE;
  if (dots) o.len = start + remove_dots(out + start, o.len - start);
  if (!auth && o.len - start >= 2 && out[start] == '/' && out[start + 1] == '/') {
    if (o.len + 2 > o.size) return LWHTTP_ERR_TOO_LARGE;
    memmove(out + start + 2, out + start, o.len - start);
    out[start] = '/';
    out[start + 1] = '.';
    o.len += 2;
  }
  if (query->has_query) {
    lwhttp_buffer_append(&o, "?", 1);
    lwhttp_buffer_append(&o, query->query, query->query_len);
  }
  if (r.has_fragment) {
    lwhttp_buffer_append(&o, "#", 1);
    lwhttp_buffer_append(&o, r.fragment, r.fragment_len);
  }
  if (!lwhttp_buffer_ok(&o)) return LWHTTP_ERR_TOO_LARGE;
  out[o.len] = '\0';
  return LWHTTP_OK;
}
