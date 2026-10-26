#include <string.h>

#include "internal.h"

enum {
  ST_START,
  ST_START_LINE,
  ST_HEADER,
  ST_HEADER_END,
  ST_TRAILER,
  ST_TRAILER_END,
  ST_BODY,
  ST_BODY_CLOSE,
  ST_CHUNK_SIZE,
  ST_CHUNK_DATA,
  ST_CHUNK_END,
  ST_DONE,
  ST_ERROR
};

enum { MODE_NONE, MODE_LENGTH, MODE_CHUNKED, MODE_CLOSE };

static const uint16_t mode_flags[] = {LWHTTP_MSG_NO_BODY, LWHTTP_MSG_CONTENT_LENGTH, LWHTTP_MSG_CHUNKED,
                                      LWHTTP_MSG_CLOSE_DELIMITED};

#define PF_CL 0x0001u
#define PF_TE 0x0002u
#define PF_TE_CHUNKED 0x0004u
#define PF_TE_FINAL 0x0008u
#define PF_CLOSE 0x0010u
#define PF_KEEP 0x0020u

#define RQ_HEAD 1
#define RQ_CONNECT 2

void lwhttp_parser_init(lwhttp_parser_t *parser, char *line, size_t line_size,
                        const lwhttp_parser_callbacks_t *callbacks, void *user)
{
  parser->callbacks = callbacks;
  parser->user = user;
  parser->line = line;
  parser->line_size = line_size;
  parser->request = 0;
  lwhttp_parser_reset(parser);
}

void lwhttp_parser_reset(lwhttp_parser_t *parser)
{
  parser->line_len = 0;
  parser->content_length = 0;
  parser->remaining = 0;
  parser->status = 0;
  parser->flags = 0;
  parser->internal = 0;
  parser->state = ST_START;
  parser->version_major = 0;
  parser->version_minor = 0;
  parser->error = LWHTTP_OK;
}

void lwhttp_parser_set_request(lwhttp_parser_t *parser, const char *method)
{
  parser->request = 0;
  if (method && !strcmp(method, "HEAD")) parser->request = RQ_HEAD;
  else if (method && !strcmp(method, "CONNECT")) parser->request = RQ_CONNECT;
}

bool lwhttp_parser_is_complete(const lwhttp_parser_t *parser) { return parser->state == ST_DONE; }

bool lwhttp_parser_keep_alive(const lwhttp_parser_t *parser) { return (parser->flags & LWHTTP_MSG_KEEP_ALIVE) != 0; }

lwhttp_error_t lwhttp_parser_error(const lwhttp_parser_t *parser) { return (lwhttp_error_t)parser->error; }

static size_t fail(lwhttp_parser_t *p, lwhttp_error_t e, size_t at)
{
  p->state = ST_ERROR;
  p->error = (uint8_t)e;
  return at;
}

static int take_line(lwhttp_parser_t *p, const uint8_t *data, size_t len, size_t *used)
{
  const uint8_t *lf = memchr(data, '\n', len);
  size_t n = lf ? (size_t)(lf - data) : len;
  *used = lf ? n + 1 : n;
  if (n >= p->line_size - p->line_len) return -2;
  memcpy(p->line + p->line_len, data, n);
  p->line_len += n;
  if (!lf) return 0;
  if (p->line_len && p->line[p->line_len - 1] == '\r') p->line_len--;
  p->line[p->line_len] = '\0';
  return 1;
}

static char *version(lwhttp_parser_t *p, char *s)
{
  if (strncmp(s, "HTTP/", 5) || !LWHTTP_IS_DIGIT(s[5]) || s[6] != '.' || !LWHTTP_IS_DIGIT(s[7])) return NULL;
  p->version_major = (uint8_t)(s[5] - '0');
  p->version_minor = (uint8_t)(s[7] - '0');
  return s + 8;
}

static lwhttp_error_t start_line(lwhttp_parser_t *p)
{
  const lwhttp_parser_callbacks_t *cb = p->callbacks;
  char *q = version(p, p->line);
  if (!q || *q != ' ' || !LWHTTP_IS_DIGIT(q[1]) || !LWHTTP_IS_DIGIT(q[2]) || !LWHTTP_IS_DIGIT(q[3]))
    return LWHTTP_ERR_PROTOCOL;
  p->status = (uint16_t)((q[1] - '0') * 100 + (q[2] - '0') * 10 + (q[3] - '0'));
  q += 4;
  if (*q == ' ') q++;
  else if (*q) return LWHTTP_ERR_PROTOCOL;
  if (p->status < 100 || !lwhttp__value_ok(q, (size_t)(p->line + p->line_len - q))) return LWHTTP_ERR_PROTOCOL;
  if (p->version_major != 1) return LWHTTP_ERR_VERSION;
  if (cb && cb->on_status_line && cb->on_status_line(p, p->status, q)) return LWHTTP_ERR_ABORTED;
  return LWHTTP_OK;
}

static lwhttp_error_t content_length(const char *s, uint32_t *out)
{
  bool have = false;
  uint32_t first = 0;
  for (;;) {
    uint32_t v = 0;
    while (LWHTTP_IS_OWS(*s)) s++;
    if (!LWHTTP_IS_DIGIT(*s)) return LWHTTP_ERR_PROTOCOL;
    while (LWHTTP_IS_DIGIT(*s)) {
      uint8_t d = (uint8_t)(*s++ - '0');
      if (v > 429496729ul || (v == 429496729ul && d > 4)) return LWHTTP_ERR_TOO_LARGE;
      v = v * 10 + d;
    }
    while (LWHTTP_IS_OWS(*s)) s++;
    if (have && v != first) return LWHTTP_ERR_PROTOCOL;
    first = v;
    have = true;
    if (!*s) break;
    if (*s++ != ',') return LWHTTP_ERR_PROTOCOL;
  }
  *out = first;
  return LWHTTP_OK;
}

static lwhttp_error_t semantics(lwhttp_parser_t *p, const char *name, const char *value)
{
  const char *cursor = value;
  const char *tok;
  size_t n;
  lwhttp_error_t e;
  uint32_t len;
  switch (LWHTTP_LOWER(name[0])) {
  case 'c':
    if (lwhttp__ieq(name, "content-length")) {
      e = content_length(value, &len);
      if (e) return e;
      if ((p->internal & PF_CL) && len != p->content_length) return LWHTTP_ERR_PROTOCOL;
      p->content_length = len;
      p->internal |= PF_CL;
    } else if (lwhttp__ieq(name, "connection")) {
      while (lwhttp__list_next(&cursor, &tok, &n))
        if (lwhttp__ieqn(tok, n, "close")) p->internal |= PF_CLOSE;
        else if (lwhttp__ieqn(tok, n, "keep-alive")) p->internal |= PF_KEEP;
    }
    break;
  case 't':
    if (lwhttp__ieq(name, "transfer-encoding")) {
      while (lwhttp__list_next(&cursor, &tok, &n)) {
        if (!n) return LWHTTP_ERR_PROTOCOL;
        p->internal |= PF_TE;
        if (lwhttp__ieqn(tok, n, "chunked")) {
          if (p->internal & PF_TE_CHUNKED) return LWHTTP_ERR_PROTOCOL;
          p->internal |= PF_TE_CHUNKED | PF_TE_FINAL;
        } else {
          p->internal &= (uint16_t)~PF_TE_FINAL;
          p->flags |= LWHTTP_MSG_TRANSFER_CODED;
        }
      }
    }
    break;
  default: break;
  }
  return LWHTTP_OK;
}

static lwhttp_error_t field(lwhttp_parser_t *p, bool trailer)
{
  const lwhttp_parser_callbacks_t *cb = p->callbacks;
  char *name = p->line;
  char *q = name;
  char *end;
  char *v;
  size_t name_len;
  size_t value_len;
  int r;
  while (LWHTTP_TCHAR(*q)) q++;
  if (q == name) return LWHTTP_ERR_PROTOCOL;
  end = q;
  name_len = (size_t)(q - name);
  if (*q != ':') {
    while (LWHTTP_IS_OWS(*q)) q++;
    if (*q != ':') return LWHTTP_ERR_PROTOCOL;
  }
  *end = '\0';
  v = q + 1;
  while (LWHTTP_IS_OWS(*v)) v++;
  end = p->line + p->line_len;
  while (end > v && LWHTTP_IS_OWS(end[-1])) end--;
  *end = '\0';
  value_len = (size_t)(end - v);
  if (!lwhttp__value_ok(v, value_len)) return LWHTTP_ERR_PROTOCOL;
  if (trailer) {
    r = cb && cb->on_trailer ? cb->on_trailer(p, name, name_len, v, value_len) : 0;
  } else {
    lwhttp_error_t e = semantics(p, name, v);
    if (e) return e;
    r = cb && cb->on_header ? cb->on_header(p, name, name_len, v, value_len) : 0;
  }
  return r ? LWHTTP_ERR_ABORTED : LWHTTP_OK;
}

static lwhttp_error_t complete(lwhttp_parser_t *p)
{
  const lwhttp_parser_callbacks_t *cb = p->callbacks;
  p->state = ST_DONE;
  if (cb && cb->on_message_complete && cb->on_message_complete(p)) return LWHTTP_ERR_ABORTED;
  return LWHTTP_OK;
}

static lwhttp_error_t headers_done(lwhttp_parser_t *p)
{
  const lwhttp_parser_callbacks_t *cb = p->callbacks;
  uint16_t in = p->internal;
  bool keep = p->version_minor ? !(in & PF_CLOSE) : (in & PF_KEEP) && !(in & PF_CLOSE);
  uint8_t mode;
  int r;
  uint16_t st = p->status;
  if (st == 101 || ((p->request & RQ_CONNECT) && st >= 200 && st < 300)) {
    p->flags |= LWHTTP_MSG_UPGRADE;
    keep = false;
    mode = MODE_NONE;
  } else if (st < 200 || st == 204 || st == 304 || (p->request & RQ_HEAD)) {
    mode = MODE_NONE;
  } else if (in & PF_TE) {
    mode = (in & PF_TE_FINAL) ? MODE_CHUNKED : MODE_CLOSE;
    if (mode == MODE_CLOSE || (in & PF_CL) || !p->version_minor) keep = false;
  } else if (in & PF_CL) {
    mode = MODE_LENGTH;
  } else {
    mode = MODE_CLOSE;
    keep = false;
  }
  if (keep) p->flags |= LWHTTP_MSG_KEEP_ALIVE;
  p->flags |= mode_flags[mode];
  p->line_len = 0;
  r = cb && cb->on_headers_complete ? cb->on_headers_complete(p) : 0;
  if (r == LWHTTP_PARSER_SKIP_BODY) {
    mode = MODE_NONE;
    p->flags = (uint16_t)((p->flags & ~(LWHTTP_MSG_CONTENT_LENGTH | LWHTTP_MSG_CHUNKED | LWHTTP_MSG_CLOSE_DELIMITED)) |
                          LWHTTP_MSG_NO_BODY);
  } else if (r) {
    return LWHTTP_ERR_ABORTED;
  }
  switch (mode) {
  case MODE_LENGTH:
    if (!p->content_length) return complete(p);
    p->remaining = p->content_length;
    p->state = ST_BODY;
    return LWHTTP_OK;
  case MODE_CHUNKED: p->state = ST_CHUNK_SIZE; return LWHTTP_OK;
  case MODE_CLOSE: p->state = ST_BODY_CLOSE; return LWHTTP_OK;
  default: return complete(p);
  }
}

static int hex(char c)
{
  if (LWHTTP_IS_DIGIT(c)) return c - '0';
  c |= 0x20;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static lwhttp_error_t chunk_size(lwhttp_parser_t *p)
{
  const char *s = p->line;
  const char *end = s + p->line_len;
  uint32_t v = 0;
  int h = hex(*s);
  if (h < 0) return LWHTTP_ERR_PROTOCOL;
  do {
    if (v > 0x0FFFFFFFul) return LWHTTP_ERR_TOO_LARGE;
    v = (v << 4) | (uint32_t)h;
    h = hex(*++s);
  } while (h >= 0);
  while (LWHTTP_IS_OWS(*s)) s++;
  if (s != end && (*s != ';' || !lwhttp__value_ok(s, (size_t)(end - s)))) return LWHTTP_ERR_PROTOCOL;
  p->remaining = v;
  p->state = v ? ST_CHUNK_DATA : ST_TRAILER;
  return LWHTTP_OK;
}

size_t lwhttp_parser_execute(lwhttp_parser_t *parser, const uint8_t *data, size_t len)
{
  lwhttp_parser_t *p = parser;
  const lwhttp_parser_callbacks_t *cb = p->callbacks;
  size_t i = 0;
  size_t n;
  lwhttp_error_t e;
  int r;
  if (p->state == ST_ERROR) return 0;
  if (p->state == ST_DONE) {
    if (p->flags & LWHTTP_MSG_UPGRADE) return 0;
    lwhttp_parser_reset(p);
  }
  while (i < len) {
    switch (p->state) {
    case ST_START:
      if (data[i] == '\r' || data[i] == '\n') i++;
      else p->state = ST_START_LINE;
      break;
    case ST_START_LINE:
      r = take_line(p, data + i, len - i, &n);
      i += n;
      if (r < 0) return fail(p, r == -1 ? LWHTTP_ERR_PROTOCOL : LWHTTP_ERR_HEADERS_TOO_LARGE, i);
      if (r) {
        e = start_line(p);
        if (e) return fail(p, e, i);
        p->line_len = 0;
        p->state = ST_HEADER;
      }
      break;
    case ST_HEADER:
    case ST_TRAILER:
      if (!p->line_len && LWHTTP_IS_OWS(data[i])) return fail(p, LWHTTP_ERR_PROTOCOL, i);
      r = take_line(p, data + i, len - i, &n);
      i += n;
      if (r < 0) return fail(p, r == -1 ? LWHTTP_ERR_PROTOCOL : LWHTTP_ERR_HEADERS_TOO_LARGE, i);
      if (!r) break;
      if (p->line_len) {
        p->state++;
        break;
      }
      e = p->state == ST_HEADER ? headers_done(p) : complete(p);
      if (e) return fail(p, e, i);
      if (p->state == ST_DONE) return i;
      break;
    case ST_HEADER_END:
    case ST_TRAILER_END:
      if (LWHTTP_IS_OWS(data[i])) {
        if (p->line_len + 1 >= p->line_size) return fail(p, LWHTTP_ERR_HEADERS_TOO_LARGE, i);
        p->line[p->line_len++] = ' ';
        i++;
      } else {
        e = field(p, p->state == ST_TRAILER_END);
        if (e) return fail(p, e, i);
        p->line_len = 0;
      }
      p->state--;
      break;
    case ST_BODY:
    case ST_CHUNK_DATA:
      n = len - i;
      if ((uint32_t)n > p->remaining) n = (size_t)p->remaining;
      if (cb && cb->on_body && cb->on_body(p, data + i, n)) return fail(p, LWHTTP_ERR_ABORTED, i);
      i += n;
      p->remaining -= n;
      if (p->remaining) break;
      if (p->state == ST_CHUNK_DATA) {
        p->state = ST_CHUNK_END;
        break;
      }
      e = complete(p);
      return e ? fail(p, e, i) : i;
    case ST_BODY_CLOSE:
      if (cb && cb->on_body && cb->on_body(p, data + i, len - i)) return fail(p, LWHTTP_ERR_ABORTED, i);
      return len;
    case ST_CHUNK_SIZE:
    case ST_CHUNK_END:
      r = take_line(p, data + i, len - i, &n);
      i += n;
      if (r < 0) return fail(p, r == -1 ? LWHTTP_ERR_PROTOCOL : LWHTTP_ERR_TOO_LARGE, i);
      if (!r) break;
      if (p->state == ST_CHUNK_END) {
        if (p->line_len) return fail(p, LWHTTP_ERR_PROTOCOL, i);
        p->state = ST_CHUNK_SIZE;
      } else {
        e = chunk_size(p);
        if (e) return fail(p, e, i);
      }
      p->line_len = 0;
      break;
    default: return i;
    }
  }
  return i;
}

lwhttp_error_t lwhttp_parser_finish(lwhttp_parser_t *parser)
{
  lwhttp_error_t e;
  switch (parser->state) {
  case ST_START:
  case ST_DONE: return LWHTTP_OK;
  case ST_ERROR: return (lwhttp_error_t)parser->error;
  case ST_BODY_CLOSE:
    e = complete(parser);
    if (e) fail(parser, e, 0);
    return e;
  default: fail(parser, LWHTTP_ERR_CLOSED, 0); return LWHTTP_ERR_CLOSED;
  }
}
