#include <stdio.h>
#include <string.h>

#include <lwhttp.h>

#include "check.h"

#define MSG(s) s, sizeof(s) - 1

typedef struct {
  uint16_t status;
  char reason[48];
  char headers[384];
  char trailers[96];
  char body[384];
  size_t body_len;
  uint32_t body_total;
  int messages;
  int completes;
  int skip_body;
  int abort_at;
  uint16_t flags_at_headers;
  bool lengths_ok;
} cap_t;

enum { AB_NONE, AB_STATUS, AB_HEADER, AB_HEADERS, AB_BODY, AB_TRAILER, AB_COMPLETE };

static char line[160];
static lwhttp_parser_t parser;
static cap_t cap;

static void append_pair(char *dst, size_t size, const char *name, const char *value)
{
  size_t n = strlen(dst);
  snprintf(dst + n, size - n, "%s=%s;", name, value);
}

static int on_status_line(lwhttp_parser_t *p, uint16_t status, const char *reason)
{
  cap_t *c = p->user;
  c->status = status;
  snprintf(c->reason, sizeof(c->reason), "%s", reason);
  return c->abort_at == AB_STATUS;
}

static int on_header(lwhttp_parser_t *p, const char *name, size_t name_len, const char *value, size_t value_len)
{
  cap_t *c = p->user;
  if (strlen(name) != name_len || strlen(value) != value_len) c->lengths_ok = false;
  append_pair(c->headers, sizeof(c->headers), name, value);
  return c->abort_at == AB_HEADER;
}

static int on_headers_complete(lwhttp_parser_t *p)
{
  cap_t *c = p->user;
  c->flags_at_headers = p->flags;
  if (c->abort_at == AB_HEADERS) return 2;
  return c->skip_body ? LWHTTP_PARSER_SKIP_BODY : 0;
}

static int on_body(lwhttp_parser_t *p, const uint8_t *data, size_t len)
{
  cap_t *c = p->user;
  size_t room = sizeof(c->body) - 1 - c->body_len;
  size_t n = len < room ? len : room;
  memcpy(c->body + c->body_len, data, n);
  c->body_len += n;
  c->body[c->body_len] = '\0';
  c->body_total += len;
  return c->abort_at == AB_BODY;
}

static int on_trailer(lwhttp_parser_t *p, const char *name, size_t name_len, const char *value, size_t value_len)
{
  cap_t *c = p->user;
  if (strlen(name) != name_len || strlen(value) != value_len) c->lengths_ok = false;
  append_pair(c->trailers, sizeof(c->trailers), name, value);
  return c->abort_at == AB_TRAILER;
}

static int on_message_complete(lwhttp_parser_t *p)
{
  cap_t *c = p->user;
  c->messages++;
  return c->abort_at == AB_COMPLETE;
}

static const lwhttp_parser_callbacks_t callbacks = {
    on_status_line, on_header, on_headers_complete, on_body, on_trailer, on_message_complete,
};

static void start(const char *method, size_t line_size)
{
  memset(&cap, 0, sizeof(cap));
  cap.lengths_ok = true;
  lwhttp_parser_init(&parser, line, line_size, &callbacks, &cap);
  lwhttp_parser_set_request(&parser, method);
}

static lwhttp_error_t feed(const char *msg, size_t len, size_t step)
{
  size_t off = 0;
  while (off < len) {
    size_t n = len - off < step ? len - off : step;
    size_t used = 0;
    while (used < n) {
      size_t u = lwhttp_parser_execute(&parser, (const uint8_t *)msg + off + used, n - used);
      if (lwhttp_parser_error(&parser)) return lwhttp_parser_error(&parser);
      used += u;
      if (lwhttp_parser_is_complete(&parser) && (parser.flags & LWHTTP_MSG_UPGRADE)) return LWHTTP_OK;
      if (!u) break;
    }
    off += n;
  }
  return LWHTTP_OK;
}

static lwhttp_error_t parse(const char *method, const char *msg, size_t len)
{
  lwhttp_error_t e;
  start(method, sizeof(line));
  e = feed(msg, len, len ? len : 1);
  return e ? e : lwhttp_parser_finish(&parser);
}

static void accept_all_splits(const char *name, const char *method, const char *msg, size_t len, int messages,
                              const char *body, const char *headers, const char *trailers)
{
  static const size_t steps[] = {1, 2, 3, 7, 64, 4096};
  size_t i;
  t_begin(name);
  for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
    lwhttp_error_t e;
    start(method, sizeof(line));
    e = feed(msg, len, steps[i]);
    if (!e) e = lwhttp_parser_finish(&parser);
    if (!CHECK_INT(e, LWHTTP_OK) || !CHECK_INT(cap.messages, messages) || !CHECK_STR(cap.body, body) ||
        !CHECK(cap.lengths_ok)) {
      CHECK_INT(steps[i], 0);
      break;
    }
    if (headers && !CHECK_STR(cap.headers, headers)) break;
    if (trailers && !CHECK_STR(cap.trailers, trailers)) break;
  }
  t_end();
}

static void reject(const char *name, const char *msg, size_t len, lwhttp_error_t want)
{
  t_begin(name);
  CHECK_INT(parse("GET", msg, len), want);
  CHECK_INT(lwhttp_parser_execute(&parser, (const uint8_t *)"x", 1), 0);
  t_end();
}

static void test_parser_accept(void)
{
  t_section("parser: valid responses");
  accept_all_splits("content-length body", "GET", MSG("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nX-A:  b  \r\n\r\nhello"),
                    1, "hello", "Content-Length=5;X-A=b;", NULL);
  accept_all_splits("chunked with ext and trailers", "GET",
                    MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5;ext=\"a;b\\\"c\"\r\nhello\r\n"
                        "6 ; x = y\r\n world\r\na\r\n0123456789\r\nA\r\nABCDEFGHIJ\r\n0\r\nX-T: 1\r\nX-U:2\r\n\r\n"),
                    1, "hello world0123456789ABCDEFGHIJ", NULL, "X-T=1;X-U=2;");
  accept_all_splits("chunked empty body", "GET", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"),
                    1, "", NULL, "");
  accept_all_splits("bare LF line endings", "GET", MSG("HTTP/1.1 200 OK\nContent-Length: 2\n\nok"), 1, "ok", NULL,
                    NULL);
  accept_all_splits("obs-fold becomes SP", "GET",
                    MSG("HTTP/1.1 200 OK\r\nX-Fold: a\r\n  b\r\n\tc\r\nContent-Length: 0\r\n\r\n"), 1, "",
                    "X-Fold=a  b c;Content-Length=0;", NULL);
  accept_all_splits("close-delimited body", "GET", MSG("HTTP/1.0 200 OK\r\n\r\nuntil close"), 1, "until close", NULL,
                    NULL);
  accept_all_splits("HEAD ignores length", "HEAD", MSG("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\n"), 1, "", NULL,
                    NULL);
  accept_all_splits("204 has no body", "GET", MSG("HTTP/1.1 204 No Content\r\n\r\n"), 1, "", NULL, NULL);
  accept_all_splits("304 ignores length", "GET", MSG("HTTP/1.1 304 Not Modified\r\nContent-Length: 9\r\n\r\n"), 1, "",
                    NULL, NULL);
  accept_all_splits("interim responses", "POST",
                    MSG("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\nLink: </a>\r\n\r\n"
                        "HTTP/1.1 201 Created\r\nContent-Length: 1\r\n\r\nx"),
                    3, "x", NULL, NULL);
  accept_all_splits("repeated equal lengths", "GET", MSG("HTTP/1.1 200 \r\nContent-Length: 1, 1\r\n\r\nz"), 1, "z",
                    NULL, NULL);
  accept_all_splits("missing reason phrase", "GET", MSG("HTTP/1.1 200\r\nContent-Length: 0\r\n\r\n"), 1, "", NULL,
                    NULL);
  accept_all_splits("unknown coding reads to close", "GET",
                    MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nraw"), 1, "raw", NULL, NULL);
  accept_all_splits("leading empty lines", "GET", MSG("\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n!"), 1, "!",
                    NULL, NULL);
  accept_all_splits("empty field value", "GET", MSG("HTTP/1.1 200 OK\r\nX-Empty:\r\nContent-Length: 0\r\n\r\n"), 1, "",
                    "X-Empty=;Content-Length=0;", NULL);
  accept_all_splits("obs-text value", "GET", MSG("HTTP/1.1 200 OK\r\nX-T: caf\xc3\xa9\r\nContent-Length: 0\r\n\r\n"), 1,
                    "", "X-T=caf\xc3\xa9;Content-Length=0;", NULL);
  accept_all_splits("space before colon tolerated", "GET", MSG("HTTP/1.1 200 OK\r\nX : a\r\nContent-Length: 0\r\n\r\n"),
                    1, "", "X=a;Content-Length=0;", NULL);
  accept_all_splits("pipelined responses", "GET",
                    MSG("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\naHTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nb"),
                    2, "ab", NULL, NULL);
}

static void test_parser_framing(void)
{
  t_section("parser: framing and flags");

  t_begin("keep-alive defaults");
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\na")), LWHTTP_OK);
  CHECK(lwhttp_parser_keep_alive(&parser));
  CHECK(parser.flags & LWHTTP_MSG_CONTENT_LENGTH);
  CHECK_INT(parser.version_minor, 1);
  CHECK_INT(parser.version_major, 1);
  CHECK_INT(parser.content_length, 1);
  CHECK_STR(cap.reason, "OK");
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 0\r\n\r\n")), LWHTTP_OK);
  CHECK(!lwhttp_parser_keep_alive(&parser));
  CHECK_INT(parse("GET", MSG("HTTP/1.0 200 OK\r\nConnection: keep-alive\r\nContent-Length: 0\r\n\r\n")), LWHTTP_OK);
  CHECK(lwhttp_parser_keep_alive(&parser));
  CHECK_INT(parser.version_minor, 0);
  CHECK_INT(parse("GET", MSG("HTTP/1.0 200 OK\r\nContent-Length: 0\r\n\r\n")), LWHTTP_OK);
  CHECK(!lwhttp_parser_keep_alive(&parser));
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nConnection: Keep-Alive, CLOSE\r\nContent-Length: 0\r\n\r\n")),
            LWHTTP_OK);
  CHECK(!lwhttp_parser_keep_alive(&parser));
  t_end();

  t_begin("framing flags");
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")), LWHTTP_OK);
  CHECK(parser.flags & LWHTTP_MSG_CHUNKED);
  CHECK(lwhttp_parser_keep_alive(&parser));
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\n\r\nx")), LWHTTP_OK);
  CHECK(parser.flags & LWHTTP_MSG_CLOSE_DELIMITED);
  CHECK(!lwhttp_parser_keep_alive(&parser));
  CHECK_INT(parse("HEAD", MSG("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\n")), LWHTTP_OK);
  CHECK(parser.flags & LWHTTP_MSG_NO_BODY);
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n")), LWHTTP_OK);
  CHECK(parser.flags & LWHTTP_MSG_TRANSFER_CODED);
  CHECK(parser.flags & LWHTTP_MSG_CHUNKED);
  t_end();

  t_begin("flags visible in on_headers_complete");
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n")), LWHTTP_OK);
  CHECK(cap.flags_at_headers & LWHTTP_MSG_CHUNKED);
  CHECK(cap.flags_at_headers & LWHTTP_MSG_KEEP_ALIVE);
  CHECK_INT(parse("HEAD", MSG("HTTP/1.1 200 OK\r\nContent-Length: 9\r\n\r\n")), LWHTTP_OK);
  CHECK(cap.flags_at_headers & LWHTTP_MSG_NO_BODY);
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc")), LWHTTP_OK);
  CHECK(cap.flags_at_headers & LWHTTP_MSG_CONTENT_LENGTH);
  CHECK_INT(parse("GET", MSG("HTTP/1.0 200 OK\r\n\r\nabc")), LWHTTP_OK);
  CHECK(cap.flags_at_headers & LWHTTP_MSG_CLOSE_DELIMITED);
  t_end();

  t_begin("transfer-encoding beats length");
  CHECK_INT(parse("GET",
                  MSG("HTTP/1.1 200 OK\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\n0\r\n\r\n")),
            LWHTTP_OK);
  CHECK_STR(cap.body, "a");
  CHECK(!lwhttp_parser_keep_alive(&parser));
  t_end();

  t_begin("101 stops at headers");
  start("GET", sizeof(line));
  {
    static const char m[] = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n\r\nFRAME";
    size_t used = lwhttp_parser_execute(&parser, (const uint8_t *)m, sizeof(m) - 1);
    CHECK_INT(used, sizeof(m) - 6);
    CHECK(lwhttp_parser_is_complete(&parser));
    CHECK(parser.flags & LWHTTP_MSG_UPGRADE);
    CHECK(!lwhttp_parser_keep_alive(&parser));
    CHECK_INT(lwhttp_parser_execute(&parser, (const uint8_t *)m + used, 5), 0);
  }
  t_end();

  t_begin("CONNECT tunnel");
  start("CONNECT", sizeof(line));
  {
    static const char m[] = "HTTP/1.1 200 Connection Established\r\n\r\nTLS";
    CHECK_INT(lwhttp_parser_execute(&parser, (const uint8_t *)m, sizeof(m) - 1), sizeof(m) - 4);
    CHECK(parser.flags & LWHTTP_MSG_UPGRADE);
    CHECK_INT(cap.body_len, 0);
  }
  t_end();

  t_begin("set_request resets method");
  start("HEAD", sizeof(line));
  lwhttp_parser_set_request(&parser, NULL);
  CHECK_INT(feed(MSG("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi"), 4096), LWHTTP_OK);
  CHECK_STR(cap.body, "hi");
  t_end();

  t_begin("skip body from callback");
  start("GET", sizeof(line));
  cap.skip_body = 1;
  {
    static const char m[] = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    CHECK_INT(lwhttp_parser_execute(&parser, (const uint8_t *)m, sizeof(m) - 1), sizeof(m) - 6);
    CHECK(lwhttp_parser_is_complete(&parser));
    CHECK(parser.flags & LWHTTP_MSG_NO_BODY);
    CHECK_INT(cap.body_len, 0);
  }
  t_end();

  t_begin("finish semantics");
  start("GET", sizeof(line));
  CHECK_INT(lwhttp_parser_finish(&parser), LWHTTP_OK);
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nab")), LWHTTP_ERR_CLOSED);
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nContent")), LWHTTP_ERR_CLOSED);
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nab")), LWHTTP_ERR_CLOSED);
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nContent-Length: 4294967294\r\n\r\n")), LWHTTP_ERR_CLOSED);
  CHECK_INT(parser.content_length, 4294967294ul);
  CHECK_INT(parse("GET", MSG("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n")), LWHTTP_OK);
  CHECK_INT(lwhttp_parser_finish(&parser), LWHTTP_OK);
  t_end();

  t_begin("reset reuses parser");
  CHECK_INT(parse("GET", MSG("HTTP/1.1 500 Oops\r\n\r\nbad")), LWHTTP_OK);
  lwhttp_parser_reset(&parser);
  CHECK_INT(parser.status, 0);
  CHECK_INT(parser.flags, 0);
  CHECK_INT(lwhttp_parser_error(&parser), LWHTTP_OK);
  CHECK(!lwhttp_parser_is_complete(&parser));
  t_end();

  t_begin("large body counted");
  start("GET", sizeof(line));
  {
    static char chunk[256];
    unsigned int i;
    memset(chunk, 'x', sizeof(chunk));
    CHECK_INT(feed(MSG("HTTP/1.1 200 OK\r\nContent-Length: 65536\r\n\r\n"), 4096), LWHTTP_OK);
    for (i = 0; i < 256; i++) feed(chunk, sizeof(chunk), sizeof(chunk));
    CHECK_INT(cap.body_total, 65536ul);
    CHECK(lwhttp_parser_is_complete(&parser));
  }
  t_end();
}

static void test_parser_reject(void)
{
  t_section("parser: invalid responses");
  reject("conflicting lengths", MSG("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n"),
         LWHTTP_ERR_PROTOCOL);
  reject("negative length", MSG("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("length with junk", MSG("HTTP/1.1 200 OK\r\nContent-Length: 1x\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("empty length", MSG("HTTP/1.1 200 OK\r\nContent-Length:\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("length 2^32-1", MSG("HTTP/1.1 200 OK\r\nContent-Length: 4294967295\r\n\r\n"), LWHTTP_ERR_TOO_LARGE);
  reject("length overflow", MSG("HTTP/1.1 200 OK\r\nContent-Length: 99999999999\r\n\r\n"), LWHTTP_ERR_TOO_LARGE);
  reject("HTTP/2.0", MSG("HTTP/2.0 200 OK\r\n\r\n"), LWHTTP_ERR_VERSION);
  reject("HTTP/0.9", MSG("HTTP/0.9 200 OK\r\n\r\n"), LWHTTP_ERR_VERSION);
  reject("lowercase http", MSG("http/1.1 200 OK\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("two digit status", MSG("HTTP/1.1 20 OK\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("four digit status", MSG("HTTP/1.1 2000 OK\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("status below 100", MSG("HTTP/1.1 099 Low\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("CR in field name", MSG("HTTP/1.1 200 OK\r\nBad\rName: x\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("CR in field value", MSG("HTTP/1.1 200 OK\r\nX: a\rb\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("CTL in field value",
         MSG("HTTP/1.1 200 OK\r\nX: a\x01"
             "b\r\n\r\n"),
         LWHTTP_ERR_PROTOCOL);
  reject("DEL in field value",
         MSG("HTTP/1.1 200 OK\r\nX: a\x7f"
             "b\r\n\r\n"),
         LWHTTP_ERR_PROTOCOL);
  reject("NUL in field value", MSG("HTTP/1.1 200 OK\r\nX: a\0b\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("NUL in reason", MSG("HTTP/1.1 200 O\0K\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("NUL in chunk size", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\0\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("missing colon", MSG("HTTP/1.1 200 OK\r\nNoColon\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("empty field name", MSG("HTTP/1.1 200 OK\r\n: v\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("space before first field", MSG("HTTP/1.1 200 OK\r\n X: a\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("bad chunk size", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("chunk data overrun", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nab\r\n"),
         LWHTTP_ERR_PROTOCOL);
  reject("chunk size overflow", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n123456789\r\n"),
         LWHTTP_ERR_TOO_LARGE);
  reject("chunk size junk", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5 x\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("chunked twice", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked, chunked\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("chunked twice across fields",
         MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: gzip, chunked\r\n\r\n"),
         LWHTTP_ERR_PROTOCOL);
  reject("empty coding", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: ;x\r\n\r\n"), LWHTTP_ERR_PROTOCOL);
  reject("bad trailer", MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nbad trailer\r\n\r\n"),
         LWHTTP_ERR_PROTOCOL);

  t_begin("line limits");
  {
    static char m[400];
    memset(m, 'a', sizeof(m));
    memcpy(m, "HTTP/1.1 200 ", 13);
    start("GET", sizeof(line));
    CHECK_INT(feed(m, sizeof(m), sizeof(m)), LWHTTP_ERR_HEADERS_TOO_LARGE);
    memcpy(m, "HTTP/1.1 200 OK\r\nX: ", 20);
    start("GET", sizeof(line));
    CHECK_INT(feed(m, sizeof(m), 7), LWHTTP_ERR_HEADERS_TOO_LARGE);
    start("GET", 32);
    CHECK_INT(feed(MSG("HTTP/1.1 200 OK\r\nX: aaaaaaaaaaaa\r\n  bbbbbbbbbbbbbbb\r\n\r\n"), 4096),
              LWHTTP_ERR_HEADERS_TOO_LARGE);
  }
  t_end();

  t_begin("callback aborts");
  {
    int at;
    for (at = AB_STATUS; at <= AB_COMPLETE; at++) {
      start("GET", sizeof(line));
      cap.abort_at = at;
      CHECK_INT(feed(MSG("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\n0\r\nT: 1\r\n\r\n"), 4096),
                LWHTTP_ERR_ABORTED);
    }
  }
  t_end();
}

static void test_headers(void)
{
  static char data[48];
  lwhttp_headers_t h;
  const char *n;
  const char *v;
  t_section("header store");
  t_begin("add get at count");
  lwhttp_headers_init(&h, data, sizeof(data));
  CHECK_INT(lwhttp_headers_count(&h), 0);
  CHECK_INT(lwhttp_headers_add(&h, "A", "1"), LWHTTP_OK);
  CHECK_INT(lwhttp_headers_add(&h, "Set-Cookie", "x"), LWHTTP_OK);
  CHECK_INT(lwhttp_headers_add(&h, "set-cookie", "y"), LWHTTP_OK);
  CHECK_INT(lwhttp_headers_count(&h), 3);
  CHECK_STR(lwhttp_headers_get(&h, "SET-COOKIE"), "x");
  CHECK_STR(lwhttp_headers_get(&h, "a"), "1");
  CHECK(!lwhttp_headers_get(&h, "missing"));
  CHECK(!lwhttp_headers_get(&h, "A1"));
  CHECK(!lwhttp_headers_get(NULL, "A"));
  CHECK(!lwhttp_headers_get(&h, NULL));
  CHECK(lwhttp_headers_at(&h, 2, &n, &v));
  CHECK_STR(n, "set-cookie");
  CHECK_STR(v, "y");
  CHECK(!lwhttp_headers_at(&h, 3, &n, &v));
  CHECK(lwhttp_headers_at(&h, 0, NULL, &v));
  CHECK_STR(v, "1");
  t_end();
  t_begin("overflow and clear");
  CHECK_INT(lwhttp_headers_add(&h, "Too-Long", "yyyyyyyyyyyyyyyyyyyyyyyy"), LWHTTP_ERR_HEADERS_TOO_LARGE);
  CHECK_INT(lwhttp_headers_count(&h), 3);
  lwhttp_headers_clear(&h);
  CHECK_INT(lwhttp_headers_count(&h), 0);
  CHECK(!lwhttp_headers_get(&h, "A"));
  CHECK_INT(lwhttp_headers_add(&h, "Too-Long", "yyyyyyyyyyyyyyyyyyyyyyyy"), LWHTTP_OK);
  CHECK_INT(lwhttp_headers_count(NULL), 0);
  t_end();
}

static void test_writer(void)
{
  static char data[96];
  lwhttp_buffer_t b;
  t_section("writer");
  t_begin("header validation");
  lwhttp_buffer_init(&b, data, sizeof(data));
  CHECK(lwhttp_write_header(&b, "Host", "a"));
  CHECK(lwhttp_write_header(&b, "X", "tab\there caf\xc3\xa9"));
  CHECK(!lwhttp_write_header(&b, "Bad Name", "a"));
  CHECK(!lwhttp_write_header(&b, "", "a"));
  CHECK(!lwhttp_write_header(&b, "X", "a\r\nInjected: 1"));
  CHECK(!lwhttp_write_header(&b, "X", "a\nb"));
  CHECK(!lwhttp_write_header(&b, "X", NULL));
  CHECK(!lwhttp_write_header(&b, NULL, "a"));
  lwhttp_write_end(&b);
  CHECK(lwhttp_buffer_ok(&b));
  CHECK_INT(b.len, 30);
  CHECK(!memcmp(data, "Host: a\r\nX: tab\there caf\xc3\xa9\r\n\r\n", 30));
  t_end();
  t_begin("numbers and overflow");
  lwhttp_buffer_init(&b, data, sizeof(data));
  lwhttp_buffer_append_u32(&b, 0);
  lwhttp_buffer_append_u32(&b, 9);
  lwhttp_buffer_append_u32(&b, 10);
  lwhttp_buffer_append_u32(&b, 4294967295ul);
  CHECK_INT(b.len, 14);
  CHECK(!memcmp(data, "09104294967295", 14));
  lwhttp_buffer_init(&b, NULL, 0);
  lwhttp_buffer_append_str(&b, "count");
  CHECK_INT(b.len, 5);
  CHECK(!lwhttp_buffer_ok(&b));
  lwhttp_buffer_init(&b, data, 4);
  lwhttp_buffer_append_str(&b, "abc");
  lwhttp_buffer_append_str(&b, "de");
  lwhttp_buffer_append_str(&b, "f");
  CHECK_INT(b.len, 6);
  CHECK(!lwhttp_buffer_ok(&b));
  CHECK(!memcmp(data, "abc", 3));
  t_end();
}

static void test_tokens(void)
{
  t_section("tokens");
  t_begin("token characters");
  {
    char s[2] = {0, 0};
    int i;
    for (i = 1; i < 256; i++) {
      char c = (char)i;
      bool ref = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c && strchr("!#$%&'*+-.^_`|~", c));
      s[0] = c;
      if (!CHECK_INT(lwhttp_is_token(s), ref)) CHECK_INT(i, -1);
    }
    CHECK(!lwhttp_is_token(""));
    CHECK(!lwhttp_is_token(NULL));
    CHECK(lwhttp_is_token("Content-Type"));
  }
  t_end();
  t_begin("token lists");
  CHECK(lwhttp_header_has_token("keep-alive, Upgrade ; q=\"a,b\", close", "close"));
  CHECK(lwhttp_header_has_token("Upgrade", "upgrade"));
  CHECK(lwhttp_header_has_token(",,  ,close", "close"));
  CHECK(!lwhttp_header_has_token("x=\"close\"", "close"));
  CHECK(!lwhttp_header_has_token("closed", "close"));
  CHECK(!lwhttp_header_has_token("", "close"));
  CHECK(!lwhttp_header_has_token(NULL, "close"));
  CHECK(!lwhttp_header_has_token("close", NULL));
  t_end();
  t_begin("error strings");
  CHECK_STR(lwhttp_error_string(LWHTTP_OK), "ok");
  CHECK_STR(lwhttp_error_string(LWHTTP_ERR_TLS), "tls failure");
  CHECK_STR(lwhttp_error_string(LWHTTP_ERR_ABORTED), "aborted");
  CHECK_STR(lwhttp_error_string((lwhttp_error_t)200), "unknown error");
  CHECK_STR(lwhttp_version_string(), LWHTTP_VERSION_STRING);
  t_end();
}

static void date_round(uint32_t t, const char *text)
{
  char out[LWHTTP_DATE_SIZE];
  uint32_t back = 0;
  CHECK_INT(lwhttp_date_format(t, out), LWHTTP_DATE_SIZE - 1);
  CHECK_STR(out, text);
  CHECK_INT(lwhttp_date_parse(text, &back), LWHTTP_OK);
  CHECK_INT(back, t);
}

static void date_parse(const char *text, uint32_t want)
{
  uint32_t t = 0;
  if (CHECK_INT(lwhttp_date_parse(text, &t), LWHTTP_OK)) CHECK_INT(t, want);
}

static void date_bad(const char *text)
{
  uint32_t t;
  CHECK_INT(lwhttp_date_parse(text, &t), LWHTTP_ERR_PROTOCOL);
}

static void test_dates(void)
{
  t_section("dates");
  t_begin("format and parse IMF-fixdate");
  date_round(0, "Thu, 01 Jan 1970 00:00:00 GMT");
  date_round(86399ul, "Thu, 01 Jan 1970 23:59:59 GMT");
  date_round(784111777ul, "Sun, 06 Nov 1994 08:49:37 GMT");
  date_round(951782400ul, "Tue, 29 Feb 2000 00:00:00 GMT");
  date_round(1583020799ul, "Sat, 29 Feb 2020 23:59:59 GMT");
  date_round(2147483647ul, "Tue, 19 Jan 2038 03:14:07 GMT");
  date_round(4102444800ul, "Fri, 01 Jan 2100 00:00:00 GMT");
  {
    char out[LWHTTP_DATE_SIZE];
    lwhttp_date_format(4294967295ul, out);
    CHECK_STR(out, "Sun, 07 Feb 2106 06:28:15 GMT");
  }
  date_parse("Thu, 31 Dec 2105 23:59:59 GMT", 4291747199ul);
  t_end();
  t_begin("RFC 850 and asctime");
  date_parse("Sunday, 06-Nov-94 08:49:37 GMT", 784111777ul);
  date_parse("Thursday, 01-Jan-70 00:00:00 GMT", 0);
  date_parse("Tuesday, 01-Jan-69 00:00:00 GMT", 3124224000ul);
  date_parse("Friday, 31-Dec-99 23:59:59 GMT", 946684799ul);
  date_parse("Sun Nov  6 08:49:37 1994", 784111777ul);
  date_parse("Thu Jan  1 00:00:00 1970", 0);
  date_parse("Tue Feb 29 00:00:00 2000", 951782400ul);
  date_parse("Sat, 31 Dec 2016 23:59:60 GMT", 1483228799ul);
  t_end();
  t_begin("invalid dates");
  date_bad("Sun, 06 Nov 1994 08:49:37 UTC");
  date_bad("Sun, 6 Nov 1994 08:49:37 GMT");
  date_bad("Sun, 06 Foo 1994 08:49:37 GMT");
  date_bad("Sun, 06 nov 1994 08:49:37 GMT");
  date_bad("Sun, 06 Nov 1994 24:00:00 GMT");
  date_bad("Sun, 06 Nov 1994 08:60:00 GMT");
  date_bad("Sun, 32 Nov 1994 08:49:37 GMT");
  date_bad("Sun, 00 Nov 1994 08:49:37 GMT");
  date_bad("Wed, 31 Dec 1969 23:59:59 GMT");
  date_bad("Sun, 06 Nov 1994 08:49:37 GMT ");
  date_bad("Sun Nov  6 08:49:37 1994 ");
  date_bad("garbage");
  date_bad("");
  date_bad(", 06 Nov 1994 08:49:37 GMT");
  CHECK_INT(lwhttp_date_parse(NULL, NULL), LWHTTP_ERR_ARG);
  t_end();
}

static void resolve(const char *ref, const char *want)
{
  static char out[96];
  if (CHECK_INT(lwhttp_url_resolve("http://a/b/c/d;p?q", ref, out, sizeof(out)), LWHTTP_OK)) CHECK_STR(out, want);
}

static void test_urls(void)
{
  lwhttp_url_t u;
  static char out[8];
  static char wide[32];
  t_section("urls");
  t_begin("parse full url");
  CHECK_INT(lwhttp_url_parse("https://user:pw@[::1]:8443/p/a?x=1#frag", &u), LWHTTP_OK);
  CHECK(u.has_scheme && u.scheme_len == 5 && !memcmp(u.scheme, "https", 5));
  CHECK(u.has_authority && u.authority_len == 18);
  CHECK(u.has_userinfo && u.userinfo_len == 7 && !memcmp(u.userinfo, "user:pw", 7));
  CHECK(u.ipv6 && u.host_len == 3 && !memcmp(u.host, "::1", 3));
  CHECK(u.has_port && u.port == 8443);
  CHECK(u.path_len == 4 && !memcmp(u.path, "/p/a", 4));
  CHECK(u.has_query && u.query_len == 3 && !memcmp(u.query, "x=1", 3));
  CHECK(u.has_fragment && u.fragment_len == 4 && !memcmp(u.fragment, "frag", 4));
  t_end();
  t_begin("parse edge cases");
  CHECK_INT(lwhttp_url_parse("http://example.com", &u), LWHTTP_OK);
  CHECK(u.host_len == 11 && !u.has_port && !u.path_len && !u.has_query);
  CHECK_INT(lwhttp_url_parse("http://example.com:/x", &u), LWHTTP_OK);
  CHECK(!u.has_port && u.path_len == 2);
  CHECK_INT(lwhttp_url_parse("http://h:65535/", &u), LWHTTP_OK);
  CHECK_INT(u.port, 65535);
  CHECK_INT(lwhttp_url_parse("http://h?q", &u), LWHTTP_OK);
  CHECK(u.has_query && u.query_len == 1 && !u.path_len);
  CHECK_INT(lwhttp_url_parse("http://h#f", &u), LWHTTP_OK);
  CHECK(u.has_fragment && !u.has_query);
  CHECK_INT(lwhttp_url_parse("/relative/path?x", &u), LWHTTP_OK);
  CHECK(!u.has_scheme && !u.has_authority && u.path_len == 14);
  CHECK_INT(lwhttp_url_parse("//host/p", &u), LWHTTP_OK);
  CHECK(!u.has_scheme && u.has_authority && u.host_len == 4);
  CHECK_INT(lwhttp_url_parse("a@b:c@host:1/", &u), LWHTTP_OK);
  CHECK_INT(lwhttp_url_parse("http://a@b@host/", &u), LWHTTP_OK);
  CHECK(u.host_len == 4 && !memcmp(u.host, "host", 4) && u.userinfo_len == 3);
  CHECK_INT(lwhttp_url_parse("mailto:x@y", &u), LWHTTP_OK);
  CHECK(u.has_scheme && !u.has_authority && u.path_len == 3);
  t_end();
  t_begin("parse rejects");
  CHECK_INT(lwhttp_url_parse("http://h:99999/", &u), LWHTTP_ERR_URL);
  CHECK_INT(lwhttp_url_parse("http://h:65536/", &u), LWHTTP_ERR_URL);
  CHECK_INT(lwhttp_url_parse("http://h:8a/", &u), LWHTTP_ERR_URL);
  CHECK_INT(lwhttp_url_parse("http://h/a b", &u), LWHTTP_ERR_URL);
  CHECK_INT(lwhttp_url_parse("http://h/\x7f", &u), LWHTTP_ERR_URL);
  CHECK_INT(lwhttp_url_parse("http://h/\t", &u), LWHTTP_ERR_URL);
  CHECK_INT(lwhttp_url_parse("http://[::1/", &u), LWHTTP_ERR_URL);
  CHECK_INT(lwhttp_url_parse("http://[::1]x/", &u), LWHTTP_ERR_URL);
  CHECK_INT(lwhttp_url_parse(NULL, &u), LWHTTP_ERR_ARG);
  CHECK_INT(lwhttp_url_parse("x", NULL), LWHTTP_ERR_ARG);
  t_end();
  t_begin("RFC 3986 normal examples");
  resolve("g:h", "g:h");
  resolve("g", "http://a/b/c/g");
  resolve("./g", "http://a/b/c/g");
  resolve("g/", "http://a/b/c/g/");
  resolve("/g", "http://a/g");
  resolve("//g", "http://g");
  resolve("?y", "http://a/b/c/d;p?y");
  resolve("g?y", "http://a/b/c/g?y");
  resolve("#s", "http://a/b/c/d;p?q#s");
  resolve("g#s", "http://a/b/c/g#s");
  resolve("g?y#s", "http://a/b/c/g?y#s");
  resolve(";x", "http://a/b/c/;x");
  resolve("g;x", "http://a/b/c/g;x");
  resolve("g;x?y#s", "http://a/b/c/g;x?y#s");
  resolve("", "http://a/b/c/d;p?q");
  resolve(".", "http://a/b/c/");
  resolve("./", "http://a/b/c/");
  resolve("..", "http://a/b/");
  resolve("../", "http://a/b/");
  resolve("../g", "http://a/b/g");
  resolve("../..", "http://a/");
  resolve("../../", "http://a/");
  resolve("../../g", "http://a/g");
  t_end();
  t_begin("RFC 3986 abnormal examples");
  resolve("../../../g", "http://a/g");
  resolve("../../../../g", "http://a/g");
  resolve("/./g", "http://a/g");
  resolve("/../g", "http://a/g");
  resolve("g.", "http://a/b/c/g.");
  resolve(".g", "http://a/b/c/.g");
  resolve("g..", "http://a/b/c/g..");
  resolve("..g", "http://a/b/c/..g");
  resolve("./../g", "http://a/b/g");
  resolve("./g/.", "http://a/b/c/g/");
  resolve("g/./h", "http://a/b/c/g/h");
  resolve("g/../h", "http://a/b/c/h");
  resolve("g;x=1/./y", "http://a/b/c/g;x=1/y");
  resolve("g;x=1/../y", "http://a/b/c/y");
  resolve("g?y/./x", "http://a/b/c/g?y/./x");
  resolve("g?y/../x", "http://a/b/c/g?y/../x");
  resolve("g#s/./x", "http://a/b/c/g#s/./x");
  resolve("g#s/../x", "http://a/b/c/g#s/../x");
  resolve("http:g", "http:g");
  t_end();
  t_begin("resolve limits");
  CHECK_INT(lwhttp_url_resolve("http://a/b", "/very/long/path", out, sizeof(out)), LWHTTP_ERR_TOO_LARGE);
  CHECK_INT(lwhttp_url_resolve("/no/scheme", "x", out, sizeof(out)), LWHTTP_ERR_URL);
  CHECK_INT(lwhttp_url_resolve("http://a", "x", out, 0), LWHTTP_ERR_ARG);
  CHECK_INT(lwhttp_url_resolve("http://a", "x", out, sizeof(out)), LWHTTP_ERR_TOO_LARGE);
  CHECK_INT(lwhttp_url_resolve("http://a", "x", wide, sizeof(wide)), LWHTTP_OK);
  CHECK_STR(wide, "http://a/x");
  CHECK_INT(lwhttp_url_resolve("http://a", "x", wide, 11), LWHTTP_OK);
  CHECK_INT(lwhttp_url_resolve("http://a", "x", wide, 10), LWHTTP_ERR_TOO_LARGE);
  CHECK_INT(lwhttp_url_resolve("http://a", "http://b/c d", wide, sizeof(wide)), LWHTTP_ERR_URL);
  CHECK_INT(lwhttp_url_resolve("x:/a/b", "..//c", wide, sizeof(wide)), LWHTTP_OK);
  CHECK_STR(wide, "x:/.//c");
  CHECK_INT(lwhttp_url_resolve("x:/a/b", "..//c", wide, 7), LWHTTP_ERR_TOO_LARGE);
  t_end();
}

void unit_tests(void)
{
  test_tokens();
  test_headers();
  test_writer();
  test_dates();
  test_urls();
  test_parser_accept();
  test_parser_framing();
  test_parser_reject();
}
