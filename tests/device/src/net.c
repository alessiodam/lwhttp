#include <stdio.h>
#include <string.h>

#include <ti/getcsc.h>

#include <lwhttp.h>
#include <lwip.h>

#include "check.h"
#include "net.h"

#define BIN "http://httpbin.org"
#define TLS "https://tls13.akamai.io/"
#define TIMEOUT_MS 90000ul
#define LWHTTP_RX_LIMIT 513

typedef struct run run_t;

struct run {
  uint16_t status;
  uint32_t bytes;
  char body[1536];
  size_t body_len;
  lwhttp_error_t result;
  int responses;
  int completes;
  bool done;
  uint32_t abort_after;
  bool reject_response;
  void (*on_done)(lwhttp_client_t *client, run_t *r);
  const lwhttp_request_t *next;
  bool destroyed;
};

static run_t runs[2];
static bool stop_network;
static char text[96];

static bool r_response(lwhttp_client_t *client, void *arg)
{
  run_t *r = arg;
  r->responses++;
  r->status = lwhttp_client_status(client);
  return !r->reject_response;
}

static bool r_body(lwhttp_client_t *client, const uint8_t *data, size_t len, void *arg)
{
  run_t *r = arg;
  size_t room = sizeof(r->body) - 1 - r->body_len;
  size_t n = len < room ? len : room;
  (void)client;
  memcpy(r->body + r->body_len, data, n);
  r->body_len += n;
  r->body[r->body_len] = '\0';
  r->bytes += len;
  return !(r->abort_after && r->bytes >= r->abort_after);
}

static void r_complete(lwhttp_client_t *client, lwhttp_error_t result, void *arg)
{
  run_t *r = arg;
  r->completes++;
  r->result = result;
  r->done = true;
  if (r->on_done) r->on_done(client, r);
}

static const lwhttp_client_handlers_t handlers = {r_response, r_body, r_complete};

static bool keep_going(void)
{
  if (os_GetCSC() == sk_Clear) stop_network = true;
  return !stop_network;
}

static run_t *fresh(int i)
{
  memset(&runs[i], 0, sizeof(runs[i]));
  return &runs[i];
}

static bool wait_until(bool (*done)(void), lwhttp_client_t *a, lwhttp_client_t *b)
{
  uint32_t start = lwip_now_ms();
  while (!done()) {
    lwhttp_service_events();
    if (!keep_going() || lwip_now_ms() - start > TIMEOUT_MS) {
      t_note(stop_network ? "  cancelled by user" : "  timed out");
      lwhttp_client_cancel(a);
      lwhttp_client_cancel(b);
      return false;
    }
  }
  return true;
}

static bool first_done(void) { return runs[0].done; }

static bool both_done(void) { return runs[0].done && runs[1].done; }

static bool two_completes(void) { return runs[0].completes >= 2; }

static void heap_note(const char *when);

static void traceback(void)
{
  uint8_t n = 0;
  uint8_t i;
  const struct lwip_traceback_entry *tb = lwip_get_traceback(&n);
  if (n > 20) n = 20;
  for (i = 0; tb && i < n; i++) {
    snprintf(text, sizeof(text), "  tb%u %s %s:%lu x%u c%u o%u r%d e%u s%u", i, lwip_debug_module_name(tb[i].module),
             lwip_debug_file_name(tb[i].file), (unsigned long)tb[i].line, tb[i].extra, tb[i].component, tb[i].operation,
             tb[i].raw_error, tb[i].mapped_error, tb[i].status);
    t_note(text);
  }
}

static lwhttp_client_stats_t before;

static void stats(lwhttp_client_t *c)
{
  const lwhttp_client_stats_t *s = lwhttp_client_stats(c);
  if (!s) return;
  snprintf(text, sizeof(text), "  conn %u reuse %u retry %u redir %u", s->connections, s->reused, s->retries,
           s->redirects);
  t_note(text);
  snprintf(text, sizeof(text), "  sent %lu recv %lu stalls %u unsent %lu state %u",
           (unsigned long)(s->bytes_sent - before.bytes_sent),
           (unsigned long)(s->bytes_received - before.bytes_received), s->write_stalls - before.write_stalls,
           (unsigned long)s->unsent, s->final_state);
  t_note(text);
}

static lwhttp_error_t perform(lwhttp_client_t *c, const lwhttp_request_t *req, run_t *r)
{
  lwhttp_error_t e;
  if (lwhttp_client_stats(c)) before = *lwhttp_client_stats(c);
  e = lwhttp_client_request(c, req, &handlers, r);
  uint32_t start = lwip_now_ms();
  if (e) return e;
  wait_until(first_done, c, NULL);
  snprintf(text, sizeof(text), "  %lu ms, status %u, %lu bytes, %s", (unsigned long)(lwip_now_ms() - start), r->status,
           (unsigned long)r->bytes, lwhttp_error_string(r->result));
  t_note(text);
  stats(c);
  if (r->result) {
    heap_note("fail");
    snprintf(text, sizeof(text), "  url %.70s", lwhttp_client_url(c) ? lwhttp_client_url(c) : "(none)");
    t_note(text);
    traceback();
  }
  return r->result;
}

static lwhttp_error_t fetch(lwhttp_client_t *c, const char *method, const char *url)
{
  lwhttp_request_t req = {0};
  req.method = method;
  req.url = url;
  return perform(c, &req, fresh(0));
}

static bool body_has(const char *needle) { return strstr(runs[0].body, needle) != NULL; }

static bool header_has(lwhttp_client_t *c, const char *name, const char *needle)
{
  const char *v = lwhttp_client_header(c, name);
  return v && strstr(v, needle);
}

static void heap_note(const char *when)
{
  struct mem_accounting_stats m;
  if (!mem_get_stats(&m)) return;
  snprintf(text, sizeof(text), "  heap %s: used %u free %u tls %u rings %u", when, (unsigned)m.heap_used,
           (unsigned)m.heap_free, (unsigned)m.tls_used, (unsigned)m.socket_ring_used);
  t_note(text);
}

static bool begin(const char *name)
{
  if (stop_network) return false;
  t_begin(name);
  heap_note("start");
  return true;
}

static void expect_get(lwhttp_client_t *c, const char *name, const char *url, uint16_t status, const char *needle)
{
  if (!begin(name)) return;
  CHECK_INT(fetch(c, NULL, url), LWHTTP_OK);
  CHECK_INT(runs[0].status, status);
  if (needle) CHECK(body_has(needle));
  t_end();
}

static const char *stream_data = "streamed-body-1234567890";
static size_t stream_off;

static size_t reader(void *arg, uint8_t *buf, size_t size)
{
  size_t left = strlen(stream_data) - stream_off;
  size_t n = left < 7 ? left : 7;
  (void)arg;
  if (n > size) n = size;
  memcpy(buf, stream_data + stream_off, n);
  stream_off += n;
  return n;
}

static lwhttp_error_t send_body(lwhttp_client_t *c, const char *method, const char *url, const char *body, bool expect)
{
  static const lwhttp_header_t type[] = {{"Content-Type", "text/plain"}};
  lwhttp_request_t req = {0};
  req.method = method;
  req.url = url;
  req.headers = type;
  req.header_count = 1;
  req.body = (const uint8_t *)body;
  req.body_length = strlen(body);
  req.expect_continue = expect;
  return perform(c, &req, fresh(0));
}

static void post_body(lwhttp_client_t *c, const char *name, const char *method, const char *url, const char *body,
                      bool expect, const char *needle)
{
  if (!begin(name)) return;
  CHECK_INT(send_body(c, method, url, body, expect), LWHTTP_OK);
  CHECK_INT(runs[0].status, 200);
  if (needle) CHECK(body_has(needle));
  t_end();
}

static lwhttp_client_t *make_client(uint32_t timeout, size_t header, uint8_t redirects, bool keep)
{
  lwhttp_client_config_t cfg;
  lwhttp_client_config_default(&cfg);
  if (timeout) cfg.timeout_ms = timeout;
  if (header) cfg.header_size = header;
  cfg.max_redirects = redirects;
  cfg.keep_alive = keep;
  return lwhttp_client_create(&cfg);
}

static void test_client_api(void)
{
  lwhttp_client_config_t cfg;
  lwhttp_client_t *c;
  lwhttp_request_t req = {0};
  static const lwhttp_header_t length[] = {{"Content-Length", "5"}};
  static const lwhttp_header_t te[] = {{"transfer-encoding", "chunked"}};
  static const lwhttp_header_t inject[] = {{"X-Test", "a\r\nEvil: 1"}};
  static const lwhttp_header_t badname[] = {{"Bad Name", "a"}};
  static const lwhttp_header_t nullname[] = {{NULL, "a"}};

  t_section("client api");
  t_begin("config defaults");
  lwhttp_client_config_default(&cfg);
  CHECK_INT(cfg.timeout_ms, 30000);
  CHECK_INT(cfg.header_size, 4096);
  CHECK_INT(cfg.max_redirects, 5);
  CHECK(cfg.keep_alive);
  CHECK_STR(cfg.user_agent, "LWHTTP/" LWHTTP_VERSION_STRING);
  CHECK(!cfg.keep_headers);
  t_end();

  t_begin("null safety");
  CHECK(!lwhttp_client_is_done(NULL));
  CHECK_INT(lwhttp_client_result(NULL), LWHTTP_ERR_ARG);
  CHECK_INT(lwhttp_client_status(NULL), 0);
  CHECK_INT(lwhttp_client_version(NULL), 0);
  CHECK(!lwhttp_client_header(NULL, "x"));
  CHECK(!lwhttp_client_headers(NULL));
  CHECK(!lwhttp_client_url(NULL));
  CHECK(!lwhttp_client_detach(NULL, NULL, NULL));
  CHECK(!lwhttp_client_stats(NULL));
  CHECK_INT(lwhttp_client_wait(NULL), LWHTTP_ERR_ARG);
  CHECK_INT(lwhttp_client_request(NULL, &req, NULL, NULL), LWHTTP_ERR_ARG);
  lwhttp_client_destroy(NULL);
  lwhttp_client_cancel(NULL);
  lwhttp_client_close(NULL);
  t_end();

  t_begin("fresh client");
  c = lwhttp_client_create(NULL);
  if (CHECK(c)) {
    CHECK(!lwhttp_client_is_done(c));
    CHECK_INT(lwhttp_client_result(c), LWHTTP_OK);
    CHECK_INT(lwhttp_client_status(c), 0);
    CHECK(!lwhttp_client_url(c));
    CHECK(!lwhttp_client_header(c, "x"));
    CHECK_INT(lwhttp_headers_count(lwhttp_client_headers(c)), 0);
    CHECK(!lwhttp_client_detach(c, NULL, NULL));
    CHECK(lwhttp_client_stats(c) && lwhttp_client_stats(c)->connections == 0);
    CHECK_INT(lwhttp_client_wait(c), LWHTTP_OK);
    lwhttp_client_cancel(c);
    lwhttp_client_close(c);
  }
  t_end();

  if (!c) return;
  t_begin("request validation");
  CHECK_INT(lwhttp_client_request(c, NULL, NULL, NULL), LWHTTP_ERR_ARG);
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_ARG);
  req.url = "http://example.com/";
  req.method = "G T";
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_ARG);
  req.method = "";
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_ARG);
  req.method = NULL;
  req.target = "/a b";
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_ARG);
  req.target = NULL;
  req.url = "ftp://example.com/";
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_UNSUPPORTED);
  CHECK(lwhttp_client_is_done(c));
  CHECK_INT(lwhttp_client_result(c), LWHTTP_ERR_UNSUPPORTED);
  req.url = "example.com/";
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_URL);
  req.url = "http:///nohost";
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_URL);
  req.url = "http://exa mple.com/";
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_URL);
  req.url = "http://example.com/";
  req.header_count = 1;
  req.headers = length;
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_ARG);
  req.headers = te;
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_ARG);
  req.headers = inject;
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_ARG);
  req.headers = badname;
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_ARG);
  req.headers = nullname;
  CHECK_INT(lwhttp_client_request(c, &req, NULL, NULL), LWHTTP_ERR_ARG);
  CHECK_STR(lwhttp_client_url(c), "http://example.com/");
  t_end();
  lwhttp_client_destroy(c);

  t_begin("tiny config clamps");
  c = make_client(1, 1, 0, false);
  CHECK(c);
  lwhttp_client_destroy(c);
  t_end();
}

static void chain_next(lwhttp_client_t *client, run_t *r)
{
  const lwhttp_request_t *next = r->next;
  if (!next) return;
  r->next = NULL;
  r->done = false;
  if (lwhttp_client_request(client, next, &handlers, r)) r->done = true;
}

static void destroy_self(lwhttp_client_t *client, run_t *r)
{
  lwhttp_client_destroy(client);
  r->destroyed = true;
}

static void test_basic(lwhttp_client_t *c)
{
  t_section("network: basics");
  if (begin("http get example.com")) {
    CHECK_INT(fetch(c, NULL, "http://example.com/"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK_INT(lwhttp_client_version(c), 1);
    CHECK(body_has("Example Domain"));
    CHECK(header_has(c, "content-type", "text/html"));
    CHECK(lwhttp_client_is_done(c));
    CHECK(!lwhttp_client_detach(c, NULL, NULL));
    t_end();
  }
  if (begin("https get")) {
    CHECK_INT(fetch(c, NULL, TLS), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(body_has("<html"));
    CHECK(header_has(c, "content-type", "text/html"));
    t_end();
  }
  if (begin("https keep-alive reuse")) {
    uint16_t reused = lwhttp_client_stats(c)->reused;
    CHECK_INT(fetch(c, NULL, TLS), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(lwhttp_client_stats(c)->reused == reused + 1);
    t_end();
  }
  if (begin("https head")) {
    CHECK_INT(fetch(c, "HEAD", TLS), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK_INT(runs[0].bytes, 0);
    CHECK(lwhttp_client_header(c, "Content-Length") != NULL);
    t_end();
  }
  if (begin("keep-alive reuse")) {
    uint16_t reused = lwhttp_client_stats(c)->reused;
    CHECK_INT(fetch(c, NULL, BIN "/get?n=1"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK_INT(fetch(c, NULL, BIN "/get?n=2"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(body_has("n=2") || body_has("\"2\""));
    CHECK_INT(fetch(c, NULL, BIN "/get?n=3"), LWHTTP_OK);
    CHECK(lwhttp_client_stats(c)->reused >= reused + 2);
    t_end();
  }
  if (begin("keep-alive disabled")) {
    lwhttp_client_t *k = make_client(0, 0, 5, false);
    if (CHECK(k)) {
      CHECK_INT(fetch(k, NULL, BIN "/get"), LWHTTP_OK);
      CHECK_INT(runs[0].status, 200);
      CHECK_INT(fetch(k, NULL, BIN "/get"), LWHTTP_OK);
      CHECK_INT(runs[0].status, 200);
      lwhttp_client_destroy(k);
    }
    t_end();
  }
  expect_get(c, "query string", BIN "/anything?a=1&b=two", 200, "two");
  if (begin("status codes")) {
    CHECK_INT(fetch(c, NULL, BIN "/status/404"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 404);
    CHECK_INT(fetch(c, NULL, BIN "/status/500"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 500);
    CHECK_INT(fetch(c, NULL, BIN "/status/418"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 418);
    CHECK_INT(fetch(c, NULL, BIN "/status/204"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 204);
    CHECK_INT(runs[0].bytes, 0);
    t_end();
  }
  if (begin("methods")) {
    CHECK_INT(fetch(c, "PUT", BIN "/put"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK_INT(fetch(c, "PATCH", BIN "/patch"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK_INT(fetch(c, "DELETE", BIN "/delete"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK_INT(fetch(c, "OPTIONS", BIN "/get"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK_INT(fetch(c, "POST", BIN "/post"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    t_end();
  }
}

static void test_bodies(lwhttp_client_t *c)
{
  t_section("network: bodies");
  if (begin("chunked response")) {
    int lines = 0;
    const char *p;
    CHECK_INT(fetch(c, NULL, BIN "/stream/5"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    for (p = runs[0].body; *p; p++) lines += *p == '\n';
    CHECK_INT(lines, 5);
    t_end();
  }
  if (begin("large content-length body")) {
    CHECK_INT(fetch(c, NULL, BIN "/bytes/20000"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK_INT(runs[0].bytes, 20000);
    t_end();
  }
  if (begin("large chunked body")) {
    CHECK_INT(fetch(c, NULL, BIN "/stream-bytes/20000"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK_INT(runs[0].bytes, 20000);
    t_end();
  }
  if (begin("identity requested")) {
    CHECK_INT(fetch(c, NULL, BIN "/headers"), LWHTTP_OK);
    CHECK(body_has("identity"));
    t_end();
  }
  if (begin("gzip rejected")) {
    CHECK_INT(fetch(c, NULL, BIN "/gzip"), LWHTTP_ERR_UNSUPPORTED);
    CHECK_INT(runs[0].responses, 0);
    CHECK_INT(runs[0].bytes, 0);
    t_end();
  }
  if (begin("deflate rejected")) {
    CHECK_INT(fetch(c, NULL, BIN "/deflate"), LWHTTP_ERR_UNSUPPORTED);
    CHECK_INT(runs[0].bytes, 0);
    t_end();
  }
  if (begin("brotli rejected")) {
    CHECK_INT(fetch(c, NULL, BIN "/brotli"), LWHTTP_ERR_UNSUPPORTED);
    CHECK_INT(runs[0].bytes, 0);
    t_end();
  }
  if (begin("client still usable after reject")) {
    CHECK_INT(fetch(c, NULL, BIN "/get"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    t_end();
  }
  if (begin("gzip opt-in passes raw")) {
    static const lwhttp_header_t enc[] = {{"Accept-Encoding", "gzip"}};
    lwhttp_request_t req = {0};
    req.url = BIN "/gzip";
    req.headers = enc;
    req.header_count = 1;
    CHECK_INT(perform(c, &req, fresh(0)), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(header_has(c, "Content-Encoding", "gzip"));
    CHECK((uint8_t)runs[0].body[0] == 0x1f && (uint8_t)runs[0].body[1] == 0x8b);
    t_end();
  }
  post_body(c, "post buffer", "POST", BIN "/post", "lwhttp=1&x=2", false, "lwhttp=1&x=2");
  post_body(c, "put buffer", "PUT", BIN "/put", "put-body", false, "put-body");
  post_body(c, "expect 100-continue", "POST", BIN "/post", "continue-body", true, "continue-body");
  if (begin("https post")) {
    CHECK_INT(send_body(c, "POST", TLS, "secure-body", false), LWHTTP_OK);
    CHECK(runs[0].status >= 200);
    t_end();
  }
  if (begin("post reader with length")) {
    lwhttp_request_t req = {0};
    stream_off = 0;
    req.method = "POST";
    req.url = BIN "/post";
    req.body_reader = reader;
    req.body_length = strlen(stream_data);
    CHECK_INT(perform(c, &req, fresh(0)), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(body_has(stream_data));
    CHECK_INT(stream_off, strlen(stream_data));
    t_end();
  }
  if (begin("post reader chunked")) {
    lwhttp_request_t req = {0};
    stream_off = 0;
    req.method = "POST";
    req.url = BIN "/post";
    req.body_reader = reader;
    req.body_length = LWHTTP_LENGTH_UNKNOWN;
    CHECK_INT(perform(c, &req, fresh(0)), LWHTTP_OK);
    CHECK(runs[0].status == 200 || runs[0].status == 411);
    t_note(body_has(stream_data) ? "  chunked upload echoed" : "  chunked upload not echoed by server");
    t_end();
  }
  if (begin("reader short body fails")) {
    lwhttp_request_t req = {0};
    stream_off = 0;
    req.method = "POST";
    req.url = BIN "/post";
    req.body_reader = reader;
    req.body_length = 1000;
    CHECK(perform(c, &req, fresh(0)) != LWHTTP_OK);
    t_end();
  }
}

static void test_headers_net(lwhttp_client_t *c)
{
  static const lwhttp_header_t custom[] = {{"X-Lwhttp-Test", "hello123"}};
  static const lwhttp_header_t agent[] = {{"User-Agent", "CalcBot/1"}};
  lwhttp_request_t req = {0};
  t_section("network: headers");
  if (begin("request headers sent")) {
    req.url = BIN "/headers";
    req.headers = custom;
    req.header_count = 1;
    CHECK_INT(perform(c, &req, fresh(0)), LWHTTP_OK);
    CHECK(body_has("hello123"));
    t_end();
  }
  expect_get(c, "default user agent", BIN "/user-agent", 200, "LWHTTP/");
  if (begin("custom user agent")) {
    req.url = BIN "/user-agent";
    req.headers = agent;
    CHECK_INT(perform(c, &req, fresh(0)), LWHTTP_OK);
    CHECK(body_has("CalcBot/1"));
    CHECK(!body_has("LWHTTP/"));
    t_end();
  }
  if (begin("response header lookup")) {
    CHECK_INT(fetch(c, NULL, BIN "/response-headers?X-Foo=bar"), LWHTTP_OK);
    CHECK_STR(lwhttp_client_header(c, "x-foo"), "bar");
    CHECK_STR(lwhttp_client_header(c, "X-FOO"), "bar");
    CHECK(lwhttp_headers_count(lwhttp_client_headers(c)) > 2);
    t_end();
  }
  if (begin("header section too large")) {
    lwhttp_client_t *s = make_client(0, 256, 5, true);
    if (CHECK(s)) {
      CHECK_INT(fetch(s, NULL,
                      BIN "/response-headers?A=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa&B="
                          "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb&C="
                          "ccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc"),
                LWHTTP_ERR_HEADERS_TOO_LARGE);
      lwhttp_client_destroy(s);
    }
    t_end();
  }
  if (begin("header line too large")) {
    static char url[400];
    lwhttp_client_t *s = make_client(0, 256, 5, true);
    size_t n = strlen(BIN "/response-headers?X-Long=");
    memcpy(url, BIN "/response-headers?X-Long=", n);
    memset(url + n, 'a', 300);
    url[n + 300] = '\0';
    if (CHECK(s)) {
      CHECK_INT(fetch(s, NULL, url), LWHTTP_ERR_HEADERS_TOO_LARGE);
      lwhttp_client_destroy(s);
    }
    t_end();
  }
  if (begin("kept headers only")) {
    static const char *const keep[] = {"X-Foo", NULL};
    static char url[400];
    lwhttp_client_config_t cfg;
    lwhttp_client_t *s;
    size_t n = strlen(BIN "/response-headers?X-Foo=bar&X-Long=");
    lwhttp_client_config_default(&cfg);
    cfg.header_size = 256;
    cfg.keep_headers = keep;
    s = lwhttp_client_create(&cfg);
    memcpy(url, BIN "/response-headers?X-Foo=bar&X-Long=", n);
    memset(url + n, 'a', 300);
    url[n + 300] = '\0';
    if (CHECK(s)) {
      CHECK_INT(fetch(s, NULL, url), LWHTTP_OK);
      CHECK_INT(runs[0].status, 200);
      CHECK_STR(lwhttp_client_header(s, "x-foo"), "bar");
      CHECK(!lwhttp_client_header(s, "X-Long"));
      CHECK_INT(lwhttp_headers_count(lwhttp_client_headers(s)), 1);
      CHECK_INT(fetch(s, NULL, BIN "/gzip"), LWHTTP_ERR_UNSUPPORTED);
      CHECK_INT(fetch(s, NULL, BIN "/redirect/1"), LWHTTP_OK);
      CHECK_INT(runs[0].status, 200);
      CHECK_INT(fetch(s, "HEAD", "https://duckduckgo.com/"), LWHTTP_OK);
      lwhttp_client_destroy(s);
    }
    t_end();
  }
}

static void test_redirects(lwhttp_client_t *c)
{
  static const lwhttp_header_t auth[] = {{"Authorization", "Bearer lwhttp-secret"}};
  lwhttp_request_t req = {0};
  t_section("network: redirects");
  if (begin("redirect chain")) {
    CHECK_INT(fetch(c, NULL, BIN "/redirect/3"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK_INT(runs[0].responses, 1);
    CHECK(strstr(lwhttp_client_url(c), "/get") != NULL);
    t_end();
  }
  expect_get(c, "relative redirects", BIN "/relative-redirect/2", 200, NULL);
  expect_get(c, "absolute redirects", BIN "/absolute-redirect/2", 200, NULL);
  if (begin("redirect limit")) {
    lwhttp_client_t *r = make_client(0, 0, 1, true);
    if (CHECK(r)) {
      CHECK_INT(fetch(r, NULL, BIN "/redirect/3"), LWHTTP_OK);
      CHECK_INT(runs[0].status, 302);
      lwhttp_client_destroy(r);
    }
    t_end();
  }
  if (begin("redirects disabled")) {
    lwhttp_client_t *r = make_client(0, 0, 0, true);
    if (CHECK(r)) {
      CHECK_INT(fetch(r, NULL, BIN "/redirect/1"), LWHTTP_OK);
      CHECK_INT(runs[0].status, 302);
      CHECK(lwhttp_client_header(r, "Location") != NULL);
      lwhttp_client_destroy(r);
    }
    t_end();
  }
  if (begin("303 turns POST into GET")) {
    CHECK_INT(send_body(c, "POST", BIN "/redirect-to?url=%2Fget&status_code=303", "x", false), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(strstr(lwhttp_client_url(c), "/get") != NULL);
    t_end();
  }
  if (begin("302 turns POST into GET")) {
    CHECK_INT(send_body(c, "POST", BIN "/redirect-to?url=%2Fget&status_code=302", "x", false), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(strstr(lwhttp_client_url(c), "/get") != NULL);
    t_end();
  }
  if (begin("307 POST without keep-alive")) {
    lwhttp_client_t *k = make_client(0, 0, 5, false);
    if (CHECK(k)) {
      CHECK_INT(send_body(k, "POST", BIN "/redirect-to?url=%2Fpost&status_code=307", "nokeep307", false), LWHTTP_OK);
      CHECK_INT(runs[0].status, 200);
      CHECK(body_has("nokeep307"));
      lwhttp_client_destroy(k);
    }
    t_end();
  }
  post_body(c, "307 keeps POST body", "POST", BIN "/redirect-to?url=%2Fpost&status_code=307", "lwhttp307", false,
            "lwhttp307");
  if (begin("308 PUT steps on one connection")) {
    lwhttp_client_t *r = make_client(0, 0, 0, true);
    if (CHECK(r)) {
      CHECK_INT(send_body(r, "PUT", BIN "/redirect-to?url=%2Fput&status_code=308", "step308", false), LWHTTP_OK);
      CHECK_INT(runs[0].status, 308);
      CHECK_INT(send_body(r, "PUT", BIN "/put", "step308", false), LWHTTP_OK);
      CHECK_INT(runs[0].status, 200);
      CHECK(body_has("step308"));
      CHECK_INT(lwhttp_client_stats(r)->reused, 1);
      lwhttp_client_destroy(r);
    }
    t_end();
  }
  if (begin("308 PUT on fresh client")) {
    lwhttp_client_t *r = make_client(0, 0, 5, true);
    if (CHECK(r)) {
      CHECK_INT(send_body(r, "PUT", BIN "/redirect-to?url=%2Fput&status_code=308", "fresh308", false), LWHTTP_OK);
      CHECK_INT(runs[0].status, 200);
      CHECK(body_has("fresh308"));
      lwhttp_client_destroy(r);
    }
    t_end();
  }
  post_body(c, "308 keeps PUT body", "PUT", BIN "/redirect-to?url=%2Fput&status_code=308", "lwhttp308", false,
            "lwhttp308");
  if (begin("http to https redirect")) {
    CHECK_INT(fetch(c, NULL, "http://raw.githubusercontent.com/github/gitignore/main/C.gitignore"), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(!strcmp(lwhttp_client_url(c), "https://raw.githubusercontent.com/github/gitignore/main/C.gitignore"));
    t_end();
  }
  if (begin("auth kept on same origin")) {
    req.url = BIN "/redirect-to?url=%2Fheaders";
    req.headers = auth;
    req.header_count = 1;
    CHECK_INT(perform(c, &req, fresh(0)), LWHTTP_OK);
    CHECK(body_has("lwhttp-secret"));
    t_end();
  }
  if (begin("custom Host dropped cross origin")) {
    static const lwhttp_header_t host[] = {{"Host", "eu.httpbin.org:80"}};
    lwhttp_request_t hreq = {0};
    hreq.url = "http://eu.httpbin.org/redirect-to?url=http%3A%2F%2Fhttpbin.org%2Fheaders";
    hreq.headers = host;
    hreq.header_count = 1;
    CHECK_INT(perform(c, &hreq, fresh(0)), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(body_has("httpbin.org"));
    CHECK(!body_has("eu.httpbin.org"));
    t_end();
  }
  if (begin("custom target only on first hop")) {
    lwhttp_request_t treq = {0};
    treq.url = BIN "/redirect-to?url=%2Fget%3Fhop%3D2";
    treq.target = "/redirect-to?url=%2Fget%3Fhop%3D2";
    CHECK_INT(perform(c, &treq, fresh(0)), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(strstr(lwhttp_client_url(c), "hop=2") != NULL);
    CHECK(body_has("hop"));
    t_end();
  }
  if (begin("auth dropped cross origin")) {
    req.url = "http://eu.httpbin.org/redirect-to?url=http%3A%2F%2Fhttpbin.org%2Fheaders";
    req.headers = auth;
    req.header_count = 1;
    CHECK_INT(perform(c, &req, fresh(0)), LWHTTP_OK);
    CHECK_INT(runs[0].status, 200);
    CHECK(!body_has("lwhttp-secret"));
    t_end();
  }
}

static void test_control(lwhttp_client_t *c)
{
  lwhttp_request_t req = {0};
  lwhttp_request_t second = {0};
  t_section("network: control flow");
  if (begin("abort in on_body")) {
    run_t *r = fresh(0);
    req.url = BIN "/bytes/5000";
    r->abort_after = 100;
    CHECK_INT(perform(c, &req, r), LWHTTP_ERR_ABORTED);
    CHECK_INT(r->completes, 1);
    t_end();
  }
  if (begin("abort in on_response")) {
    run_t *r = fresh(0);
    req.url = BIN "/get";
    r->reject_response = true;
    CHECK_INT(perform(c, &req, r), LWHTTP_ERR_ABORTED);
    CHECK_INT(r->completes, 1);
    t_end();
  }
  if (begin("cancel in flight")) {
    run_t *r = fresh(0);
    uint32_t start = lwip_now_ms();
    req.url = BIN "/delay/5";
    if (CHECK_INT(lwhttp_client_request(c, &req, &handlers, r), LWHTTP_OK)) {
      while (lwip_now_ms() - start < 1500) lwhttp_service_events();
      lwhttp_client_cancel(c);
      CHECK_INT(r->result, LWHTTP_ERR_ABORTED);
      CHECK_INT(r->completes, 1);
      CHECK(lwhttp_client_is_done(c));
      lwhttp_client_cancel(c);
      CHECK_INT(r->completes, 1);
    }
    t_end();
  }
  if (begin("sync wrapper")) {
    run_t *r = fresh(0);
    req.url = BIN "/get";
    CHECK_INT(lwhttp_client_request_sync(c, &req, &handlers, r), LWHTTP_OK);
    CHECK_INT(lwhttp_client_status(c), 200);
    CHECK_INT(r->completes, 1);
    CHECK_INT(lwhttp_client_request_sync(c, &req, NULL, NULL), LWHTTP_OK);
    CHECK_INT(lwhttp_client_status(c), 200);
    CHECK_INT(lwhttp_client_wait(c), LWHTTP_OK);
    t_end();
  }
  if (begin("request while busy")) {
    run_t *r = fresh(0);
    req.url = BIN "/get";
    if (CHECK_INT(lwhttp_client_request(c, &req, &handlers, r), LWHTTP_OK)) {
      CHECK_INT(lwhttp_client_request(c, &req, &handlers, r), LWHTTP_ERR_STATE);
      wait_until(first_done, c, NULL);
      CHECK_INT(r->result, LWHTTP_OK);
    }
    t_end();
  }
  if (begin("chain from on_complete")) {
    run_t *r = fresh(0);
    req.url = BIN "/get?first=1";
    second.url = BIN "/get?second=1";
    r->on_done = chain_next;
    r->next = &second;
    if (CHECK_INT(lwhttp_client_request(c, &req, &handlers, r), LWHTTP_OK)) {
      wait_until(two_completes, c, NULL);
      wait_until(first_done, c, NULL);
      CHECK_INT(r->completes, 2);
      CHECK_INT(r->result, LWHTTP_OK);
      CHECK(strstr(r->body, "second") != NULL);
    }
    t_end();
  }
  if (begin("destroy in on_complete")) {
    lwhttp_client_t *d = lwhttp_client_create(NULL);
    run_t *r = fresh(0);
    if (CHECK(d)) {
      req.url = BIN "/get";
      r->on_done = destroy_self;
      if (CHECK_INT(lwhttp_client_request(d, &req, &handlers, r), LWHTTP_OK)) {
        wait_until(first_done, NULL, NULL);
        CHECK(r->destroyed);
        lwhttp_service_events();
      } else {
        lwhttp_client_destroy(d);
      }
    }
    t_end();
  }
  if (begin("parallel clients")) {
    lwhttp_client_t *a = lwhttp_client_create(NULL);
    lwhttp_client_t *b = lwhttp_client_create(NULL);
    lwhttp_request_t ra = {0};
    lwhttp_request_t rb = {0};
    if (CHECK(a && b)) {
      ra.url = BIN "/get?client=a";
      rb.url = "http://example.com/";
      CHECK_INT(lwhttp_client_request(a, &ra, &handlers, fresh(0)), LWHTTP_OK);
      CHECK_INT(lwhttp_client_request(b, &rb, &handlers, fresh(1)), LWHTTP_OK);
      wait_until(both_done, a, b);
      CHECK_INT(runs[0].result, LWHTTP_OK);
      CHECK_INT(runs[1].result, LWHTTP_OK);
      CHECK_INT(runs[0].status, 200);
      CHECK_INT(runs[1].status, 200);
      CHECK(strstr(runs[1].body, "Example Domain") != NULL);
    }
    lwhttp_client_destroy(a);
    lwhttp_client_destroy(b);
    t_end();
  }
}

static void test_failures(void)
{
  lwhttp_client_t *c;
  lwhttp_error_t e;
  t_section("network: failures");
  if (begin("dns failure")) {
    c = lwhttp_client_create(NULL);
    if (CHECK(c)) {
      e = fetch(c, NULL, "http://lwhttp-test.invalid/");
      CHECK(e == LWHTTP_ERR_DNS || e == LWHTTP_ERR_CONNECT);
      t_note(lwhttp_error_string(e));
      lwhttp_client_destroy(c);
    }
    t_end();
  }
  if (begin("connect refused")) {
    c = make_client(10000, 0, 5, true);
    if (CHECK(c)) {
      e = fetch(c, NULL, "http://127.0.0.1:1/");
      CHECK(e != LWHTTP_OK);
      t_note(lwhttp_error_string(e));
      lwhttp_client_destroy(c);
    }
    t_end();
  }
  if (begin("inactivity timeout")) {
    c = make_client(3000, 0, 5, true);
    if (CHECK(c)) {
      uint32_t start = lwip_now_ms();
      e = fetch(c, NULL, BIN "/delay/10");
      CHECK_INT(e, LWHTTP_ERR_TIMEOUT);
      CHECK(lwip_now_ms() - start < 9500);
      t_note(lwhttp_error_string(e));
      lwhttp_client_destroy(c);
    }
    t_end();
  }
  if (begin("tls expired certificate")) {
    c = lwhttp_client_create(NULL);
    if (CHECK(c)) {
      CHECK_INT(fetch(c, NULL, "https://expired.badssl.com/"), LWHTTP_ERR_TLS);
      lwhttp_client_destroy(c);
    }
    t_end();
  }
  if (begin("tls wrong host")) {
    c = lwhttp_client_create(NULL);
    if (CHECK(c)) {
      CHECK_INT(fetch(c, NULL, "https://wrong.host.badssl.com/"), LWHTTP_ERR_TLS);
      lwhttp_client_destroy(c);
    }
    t_end();
  }
  if (begin("tls self-signed")) {
    c = lwhttp_client_create(NULL);
    if (CHECK(c)) {
      CHECK_INT(fetch(c, NULL, "https://self-signed.badssl.com/"), LWHTTP_ERR_TLS);
      lwhttp_client_destroy(c);
    }
    t_end();
  }
  if (begin("tls untrusted root")) {
    c = lwhttp_client_create(NULL);
    if (CHECK(c)) {
      CHECK_INT(fetch(c, NULL, "https://untrusted-root.badssl.com/"), LWHTTP_ERR_TLS);
      lwhttp_client_destroy(c);
    }
    t_end();
  }
}

static void tls_probe(const char *name, const char *url, lwhttp_error_t want)
{
  lwhttp_client_t *c;
  if (!begin(name)) return;
  c = lwhttp_client_create(NULL);
  if (CHECK(c)) {
    CHECK_INT(fetch(c, "HEAD", url), want);
    lwhttp_client_destroy(c);
  }
  t_end();
}

static void test_tls_probes(void)
{
  t_section("network: tls supported chains");
  tls_probe("tls13.akamai.io (ISRG RSA)", TLS, LWHTTP_OK);
  tls_probe("raw.githubusercontent (ISRG)", "https://raw.githubusercontent.com/github/gitignore/main/C.gitignore",
            LWHTTP_OK);
  tls_probe("duckduckgo.com (DigiCert G2)", "https://duckduckgo.com/", LWHTTP_OK);
  tls_probe("www.amazon.com (Starfield)", "https://www.amazon.com/", LWHTTP_OK);
  t_section("network: lwIP-CE tls limits");
  tls_probe("P-384 chain rejected", "https://www.google.com/", LWHTTP_ERR_TLS);
  tls_probe("Let's Encrypt YE1 rejected", "https://httpbin.org/get", LWHTTP_ERR_TLS);
  tls_probe("TLS 1.2 only rejected", "https://tls-v1-2.badssl.com:1012/", LWHTTP_ERR_TLS);
}

static void test_upgrade(void)
{
  static const lwhttp_header_t ws[] = {
      {"Connection", "Upgrade"},
      {"Upgrade", "websocket"},
      {"Sec-WebSocket-Version", "13"},
      {"Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ=="},
  };
  lwhttp_request_t req = {0};
  lwhttp_client_t *c;
  t_section("network: upgrade");
  if (!begin("websocket upgrade and detach")) return;
  c = lwhttp_client_create(NULL);
  if (CHECK(c)) {
    const uint8_t *pending = NULL;
    size_t pending_len = 99;
    struct lwip_socket *sock;
    req.url = "https://echo.websocket.in/";
    req.headers = ws;
    req.header_count = 4;
    CHECK_INT(perform(c, &req, fresh(0)), LWHTTP_OK);
    CHECK_INT(runs[0].status, 101);
    CHECK(header_has(c, "Sec-WebSocket-Accept", "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="));
    sock = lwhttp_client_detach(c, &pending, &pending_len);
    CHECK(sock != NULL);
    CHECK(pending != NULL);
    CHECK(pending_len < LWHTTP_RX_LIMIT);
    snprintf(text, sizeof(text), "  pending %u bytes after 101", (unsigned)pending_len);
    t_note(text);
    CHECK(!lwhttp_client_detach(c, NULL, NULL));
    if (sock) {
      lwip_socket_close(sock);
      lwip_socket_destroy(sock);
    }
    lwhttp_client_destroy(c);
  }
  t_end();
}

void net_offline_tests(void) { test_client_api(); }

void net_tests(bool extended)
{
  lwhttp_client_t *c = lwhttp_client_create(NULL);
  stop_network = false;
  t_section("network");
  t_begin("shared client");
  if (!CHECK(c)) {
    t_end();
    return;
  }
  t_end();
  test_basic(c);
  test_bodies(c);
  test_headers_net(c);
  test_redirects(c);
  test_control(c);
  lwhttp_client_destroy(c);
  test_failures();
  if (extended) {
    test_tls_probes();
    test_upgrade();
  }
  if (stop_network) t_info("network tests stopped by user");
}
