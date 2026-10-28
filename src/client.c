#include <string.h>

#include "net.h"

#define TX_SIZE LWHTTP_IO_CHUNK
#define WRITE_MAX 4096u
#define CHUNK_HEAD 5u
#define CHUNK_DATA (TX_SIZE - CHUNK_HEAD - 2u)
#define LINE_RESERVE 128u
#define HEADER_MIN 256u

enum { CS_IDLE, CS_CONNECTING, CS_ACTIVE, CS_DONE };
enum { SEND_NONE, SEND_BUFFER, SEND_LENGTH, SEND_CHUNKED };

struct lwhttp_client {
  uint8_t state;
  uint8_t send_mode;
  uint8_t redirects;
  uint8_t busy;
  lwhttp_error_t result;
  lwhttp_error_t pending;
  bool tls;
  bool sent;
  bool waiting_continue;
  bool abandon;
  bool reused;
  bool received;
  bool retried;
  bool redirecting;
  bool reusable;
  bool cross_origin;
  bool upgraded;
  bool last_chunk;
  bool tls_failed;
  bool timed_out;
  bool raw_coding;
  bool coded;
  bool doomed;
  uint16_t port;
  const lwhttp_client_handlers_t *handlers;
  void *arg;
  struct lwip_socket *sock;
  char *host;
  char *url;
  const char *method;
  uint8_t *head;
  const uint8_t *body;
  uint8_t *rx;
  uint8_t *tx;
  size_t head_len;
  size_t head_off;
  size_t tx_len;
  size_t tx_off;
  size_t rx_off;
  size_t rx_len;
  uint32_t body_left;
  uint32_t last_io;
  lwhttp_client_config_t config;
  lwhttp_request_t request;
  lwhttp_headers_t headers;
  lwhttp_parser_t parser;
  lwhttp_client_stats_t stats;
};

static void receive(lwhttp_client_t *c);

static bool active(const lwhttp_client_t *c) { return c->state == CS_CONNECTING || c->state == CS_ACTIVE; }

static void drop(lwhttp_client_t *c)
{
  if (c->sock) {
    lwhttp__bury(c->sock);
    c->sock = NULL;
  }
  if (c->host) {
    mem_free(c->host);
    c->host = NULL;
  }
  c->upgraded = false;
}

static void release(lwhttp_client_t *c)
{
  drop(c);
  if (c->head) mem_free(c->head);
  if (c->url) mem_free(c->url);
  if (c->tx) mem_free(c->tx);
  mem_free(c);
}

static void leave(lwhttp_client_t *c)
{
  if (!--c->busy && c->doomed) release(c);
}

static void finish(lwhttp_client_t *c, lwhttp_error_t e)
{
  const lwhttp_client_handlers_t *h = c->handlers;
  c->stats.final_state = c->state;
  c->stats.unsent = (uint32_t)(c->head_len - c->head_off) + (uint32_t)(c->tx_len - c->tx_off) +
                    (c->send_mode == SEND_CHUNKED && !c->last_chunk ? 1 : c->body_left);
  c->state = CS_DONE;
  c->result = e;
  if (e || (!c->reusable && !c->upgraded)) drop(c);
  if (c->head) {
    mem_free(c->head);
    c->head = NULL;
  }
  if (h && h->on_complete) h->on_complete(c, e, c->arg);
}

static lwhttp_error_t put(lwhttp_client_t *c, const uint8_t *data, size_t len, size_t *done)
{
  size_t n = WRITE_MAX;
  lwip_error_t e;
  *done = 0;
  while (*done < len) {
    if (n > len - *done) n = len - *done;
    e = lwip_socket_write(c->sock, data + *done, n);
    if (e == LWIP_OK) {
      *done += n;
      c->stats.bytes_sent += n;
    } else if (e == LWIP_ERR_MEM || e == LWIP_ERR_ARG) {
      n = n > 1024 ? 1024 : n > 256 ? 256 : 0;
      if (!n) {
        c->stats.write_stalls++;
        break;
      }
    } else {
      return e == LWIP_ERR_CLOSED ? LWHTTP_ERR_CLOSED : LWHTTP_ERR_SEND;
    }
  }
  return LWHTTP_OK;
}

static int fill(lwhttp_client_t *c)
{
  size_t n;
  size_t h;
  if (c->send_mode == SEND_LENGTH && c->body_left) {
    size_t want = c->body_left < TX_SIZE ? (size_t)c->body_left : TX_SIZE;
    n = c->request.body_reader(c->request.body_arg, c->tx, want);
    if (!n || n > want) return -1;
    c->body_left -= n;
    c->tx_off = 0;
    c->tx_len = n;
    return 1;
  }
  if (c->send_mode != SEND_CHUNKED || c->last_chunk) return 0;
  n = c->request.body_reader(c->request.body_arg, c->tx + CHUNK_HEAD, CHUNK_DATA);
  if (n > CHUNK_DATA) return -1;
  if (!n) {
    memcpy(c->tx, "0\r\n\r\n", 5);
    c->tx_off = 0;
    c->tx_len = 5;
    c->last_chunk = true;
    return 1;
  }
  c->tx[CHUNK_HEAD + n] = '\r';
  c->tx[CHUNK_HEAD + n + 1] = '\n';
  c->tx_len = CHUNK_HEAD + n + 2;
  h = CHUNK_HEAD;
  c->tx[--h] = '\n';
  c->tx[--h] = '\r';
  do {
    c->tx[--h] = (uint8_t)"0123456789ABCDEF"[n & 15];
    n >>= 4;
  } while (n);
  c->tx_off = h;
  return 1;
}

static lwhttp_error_t send_more(lwhttp_client_t *c)
{
  size_t w;
  lwhttp_error_t e;
  int r;
  if (c->sent || c->abandon) return LWHTTP_OK;
  if (c->head_off < c->head_len) {
    e = put(c, c->head + c->head_off, c->head_len - c->head_off, &w);
    c->head_off += w;
    if (e || c->head_off < c->head_len) return e;
  }
  if (c->waiting_continue) return LWHTTP_OK;
  for (;;) {
    if (c->tx_off < c->tx_len) {
      e = put(c, c->tx + c->tx_off, c->tx_len - c->tx_off, &w);
      c->tx_off += w;
      if (e || c->tx_off < c->tx_len) return e;
    }
    if (c->send_mode == SEND_BUFFER && c->body_left) {
      e = put(c, c->body, (size_t)c->body_left, &w);
      c->body += w;
      c->body_left -= w;
      if (e || c->body_left) return e;
    }
    r = fill(c);
    if (r < 0) return LWHTTP_ERR_ABORTED;
    if (!r) break;
  }
  c->sent = true;
  return LWHTTP_OK;
}

static void append_host(lwhttp_buffer_t *b, const lwhttp_url_t *u, uint16_t port, bool always_port)
{
  if (u->ipv6) lwhttp_buffer_append_str(b, "[");
  lwhttp_buffer_append(b, u->host, u->host_len);
  if (u->ipv6) lwhttp_buffer_append_str(b, "]");
  if (always_port || u->has_port) {
    lwhttp_buffer_append_str(b, ":");
    lwhttp_buffer_append_u32(b, port);
  }
}

static bool sensitive(const char *name)
{
  return lwhttp__ieq(name, "authorization") || lwhttp__ieq(name, "proxy-authorization") || lwhttp__ieq(name, "cookie");
}

static lwhttp_error_t build_head(lwhttp_client_t *c, const lwhttp_url_t *u, uint16_t port)
{
  const lwhttp_request_t *r = &c->request;
  lwhttp_buffer_t b;
  bool user_host = false;
  bool user_agent = false;
  bool user_coding = false;
  bool connect = !strcmp(c->method, "CONNECT");
  size_t i;
  uint8_t pass;
  for (i = 0; i < r->header_count; i++) {
    const char *n = r->headers[i].name;
    if (!n || !r->headers[i].value || lwhttp__ieq(n, "content-length") || lwhttp__ieq(n, "transfer-encoding"))
      return LWHTTP_ERR_ARG;
    user_host |= !c->cross_origin && lwhttp__ieq(n, "host");
    user_agent |= lwhttp__ieq(n, "user-agent");
    user_coding |= lwhttp__ieq(n, "accept-encoding");
  }
  c->raw_coding = user_coding;
  if (c->head) mem_free(c->head);
  c->head = NULL;
  lwhttp_buffer_init(&b, NULL, 0);
  for (pass = 0; pass < 2; pass++) {
    if (pass) {
      if (b.len == SIZE_MAX) return LWHTTP_ERR_TOO_LARGE;
      c->head = mem_malloc(b.len);
      if (!c->head) return LWHTTP_ERR_MEM;
      c->head_len = b.len;
      lwhttp_buffer_init(&b, (char *)c->head, c->head_len);
    }
    lwhttp_buffer_append_str(&b, c->method);
    lwhttp_buffer_append_str(&b, " ");
    if (r->target) {
      lwhttp_buffer_append_str(&b, r->target);
    } else if (connect) {
      append_host(&b, u, port, true);
    } else {
      if (u->path_len) lwhttp_buffer_append(&b, u->path, u->path_len);
      else lwhttp_buffer_append_str(&b, "/");
      if (u->has_query) {
        lwhttp_buffer_append_str(&b, "?");
        lwhttp_buffer_append(&b, u->query, u->query_len);
      }
    }
    lwhttp_buffer_append_str(&b, " HTTP/1.1\r\n");
    if (!user_host) {
      lwhttp_buffer_append_str(&b, "Host: ");
      append_host(&b, u, port, false);
      lwhttp_buffer_append_str(&b, "\r\n");
    }
    if (!user_agent && c->config.user_agent && !lwhttp_write_header(&b, "User-Agent", c->config.user_agent))
      return LWHTTP_ERR_ARG;
    if (c->send_mode == SEND_CHUNKED) {
      lwhttp_buffer_append_str(&b, "Transfer-Encoding: chunked\r\n");
    } else if (c->send_mode != SEND_NONE || !strcmp(c->method, "POST") || !strcmp(c->method, "PUT") ||
               !strcmp(c->method, "PATCH")) {
      lwhttp_buffer_append_str(&b, "Content-Length: ");
      lwhttp_buffer_append_u32(&b, c->body_left);
      lwhttp_buffer_append_str(&b, "\r\n");
    }
    if (!user_coding) lwhttp_buffer_append_str(&b, "Accept-Encoding: identity\r\n");
    if (c->waiting_continue) lwhttp_buffer_append_str(&b, "Expect: 100-continue\r\n");
    if (!c->config.keep_alive) lwhttp_buffer_append_str(&b, "Connection: close\r\n");
    for (i = 0; i < r->header_count; i++) {
      if (c->cross_origin && (sensitive(r->headers[i].name) || lwhttp__ieq(r->headers[i].name, "host"))) continue;
      if (!lwhttp_write_header(&b, r->headers[i].name, r->headers[i].value)) return LWHTTP_ERR_ARG;
    }
    lwhttp_write_end(&b);
  }
  return LWHTTP_OK;
}

static void on_socket(struct lwip_socket *sock, lwip_socket_event_type_t type, const void *data, void *arg);

static void rebase(lwhttp_client_t *c)
{
  c->parser.line = c->headers.data + c->headers.len;
  c->parser.line_size = c->headers.size - c->headers.len;
}

static int c_header(lwhttp_parser_t *p, const char *name, size_t name_len, const char *value, size_t value_len);
static int c_headers_complete(lwhttp_parser_t *p);
static int c_body(lwhttp_parser_t *p, const uint8_t *data, size_t len);

static bool c_skip(lwhttp_parser_t *p, const char *name, size_t name_len);

static const lwhttp_parser_callbacks_t client_callbacks = {
    NULL, c_header, c_headers_complete, c_body, c_header, NULL, c_skip,
};

static lwhttp_error_t begin(lwhttp_client_t *c)
{
  lwhttp_url_t u;
  lwhttp_error_t e;
  lwip_socket_bind_descriptor_t bind;
  bool tls;
  uint16_t port;
  if (lwhttp_url_parse(c->url, &u) || !u.has_scheme || !u.host_len) return LWHTTP_ERR_URL;
  if (lwhttp__ieqn(u.scheme, u.scheme_len, "https")) tls = true;
  else if (lwhttp__ieqn(u.scheme, u.scheme_len, "http")) tls = false;
  else return LWHTTP_ERR_UNSUPPORTED;
  port = u.has_port ? u.port : (tls ? 443 : 80);
  lwhttp_headers_clear(&c->headers);
  lwhttp_parser_init(&c->parser, c->headers.data, c->headers.size, &client_callbacks, c);
  lwhttp_parser_set_request(&c->parser, c->method);
  c->body = c->request.body;
  c->body_left = 0;
  c->send_mode = SEND_NONE;
  if (c->request.body_reader) {
    c->send_mode = c->request.body_length == LWHTTP_LENGTH_UNKNOWN ? SEND_CHUNKED : SEND_LENGTH;
    if (c->send_mode == SEND_LENGTH) c->body_left = c->request.body_length;
  } else if (c->request.body) {
    c->send_mode = SEND_BUFFER;
    c->body_left = c->request.body_length;
  }
  if (c->request.body_reader && !c->tx && !(c->tx = mem_malloc(TX_SIZE))) return LWHTTP_ERR_MEM;
  c->waiting_continue = c->request.expect_continue && (c->send_mode == SEND_CHUNKED || c->body_left);
  c->tx_off = c->tx_len = 0;
  c->head_off = 0;
  c->pending = LWHTTP_OK;
  c->sent = c->abandon = c->received = c->redirecting = c->reusable = c->last_chunk = c->coded = false;
  e = build_head(c, &u, port);
  if (e) return e;
  if (c->sock && c->tls == tls && c->port == port && lwhttp__ieqn(u.host, u.host_len, c->host) &&
      lwip_socket_status(c->sock) == LWIP_STATUS_CONNECTED && !lwip_socket_available(c->sock)) {
    c->reused = true;
    c->state = CS_ACTIVE;
    c->last_io = lwip_now_ms();
    e = send_more(c);
    if (!e) {
      c->stats.reused++;
      return LWHTTP_OK;
    }
    if (c->head_off) return e;
  }
  drop(c);
  c->host = mem_malloc(u.host_len + 1);
  if (!c->host) return LWHTTP_ERR_MEM;
  memcpy(c->host, u.host, u.host_len);
  c->host[u.host_len] = '\0';
  bind = (!strcmp(c->host, "localhost") || !strncmp(c->host, "127.", 4) || !strcmp(c->host, "::1")) ? LWIP_NETIF_LOOP
                                                                                                    : LWIP_NETIF_EXT;
  c->sock = lwip_socket_create(tls ? LWIP_SOCKET_ALTCP_TLS : LWIP_SOCKET_TCP, bind, NULL, c->config.timeout_ms);
  if (!c->sock) return LWHTTP_ERR_MEM;
  lwip_socket_on_event(c->sock, LWIP_SOCKET_EVENTF_ALL, on_socket, c);
  if (lwip_socket_connect(c->sock, c->host, port) != LWIP_OK) {
    drop(c);
    return LWHTTP_ERR_CONNECT;
  }
  c->tls = tls;
  c->tls_failed = c->timed_out = false;
  c->port = port;
  c->reused = false;
  c->stats.connections++;
  c->state = CS_CONNECTING;
  return LWHTTP_OK;
}

static void restart(lwhttp_client_t *c)
{
  lwhttp_error_t e = begin(c);
  if (e) finish(c, e);
}

static bool idempotent(const char *m)
{
  return !strcmp(m, "GET") || !strcmp(m, "HEAD") || !strcmp(m, "PUT") || !strcmp(m, "DELETE") ||
         !strcmp(m, "OPTIONS") || !strcmp(m, "TRACE");
}

static bool can_retry(const lwhttp_client_t *c)
{ return c->reused && !c->received && !c->retried && !c->request.body_reader && idempotent(c->method); }

static void retry(lwhttp_client_t *c)
{
  c->retried = true;
  c->stats.retries++;
  drop(c);
  restart(c);
}

static bool same_origin(const char *a, const char *b)
{
  lwhttp_url_t x;
  lwhttp_url_t y;
  uint16_t px;
  uint16_t py;
  if (lwhttp_url_parse(a, &x) || lwhttp_url_parse(b, &y) || x.scheme_len != y.scheme_len || x.host_len != y.host_len ||
      !lwhttp__casen(x.scheme, y.scheme, x.scheme_len) || !lwhttp__casen(x.host, y.host, x.host_len))
    return false;
  px = x.has_port ? x.port : (x.scheme_len == 5 ? 443 : 80);
  py = y.has_port ? y.port : (y.scheme_len == 5 ? 443 : 80);
  return px == py;
}

static bool redirect_keeps_body(const lwhttp_client_t *c, uint16_t st)
{ return !(st == 303 || ((st == 301 || st == 302) && !strcmp(c->method, "POST"))); }

static bool want_redirect(const lwhttp_client_t *c)
{
  uint16_t st = c->parser.status;
  if (st != 301 && st != 302 && st != 303 && st != 307 && st != 308) return false;
  if (c->redirects >= c->config.max_redirects || !lwhttp_headers_get(&c->headers, "Location")) return false;
  return !(redirect_keeps_body(c, st) && c->request.body_reader);
}

static void redirect(lwhttp_client_t *c)
{
  const char *loc = lwhttp_headers_get(&c->headers, "Location");
  size_t size = strlen(c->url) + strlen(loc) + 4;
  uint16_t st = c->parser.status;
  bool had_body = c->send_mode != SEND_NONE;
  char *next = mem_malloc(size);
  if (!next) {
    finish(c, LWHTTP_ERR_MEM);
    return;
  }
  if (lwhttp_url_resolve(c->url, loc, next, size)) {
    mem_free(next);
    finish(c, LWHTTP_ERR_REDIRECT);
    return;
  }
  if (!redirect_keeps_body(c, st) && strcmp(c->method, "HEAD")) {
    c->method = "GET";
    c->request.body = NULL;
    c->request.body_reader = NULL;
    c->request.body_length = 0;
    c->request.expect_continue = false;
  }
  c->request.target = NULL;
  if (!same_origin(c->url, next)) c->cross_origin = true;
  mem_free(c->url);
  c->url = next;
  c->redirects++;
  c->stats.redirects++;
  if (!c->reusable || had_body) drop(c);
  restart(c);
}

static bool message_done(lwhttp_client_t *c, size_t off, size_t n)
{
  uint16_t st = c->parser.status;
  lwhttp_error_t e;
  if (st < 200 && st != 101) {
    c->coded = false;
    lwhttp_headers_clear(&c->headers);
    rebase(c);
    if (st == 100 && c->waiting_continue) {
      c->waiting_continue = false;
      e = send_more(c);
      if (e) {
        finish(c, e);
        return true;
      }
    }
    return false;
  }
  if (c->parser.flags & LWHTTP_MSG_UPGRADE) {
    c->upgraded = true;
    c->rx_off = off;
    c->rx_len = n - off;
    finish(c, LWHTTP_OK);
    return true;
  }
  c->reusable = c->config.keep_alive && lwhttp_parser_keep_alive(&c->parser) && off == n && c->sent &&
                lwip_socket_status(c->sock) == LWIP_STATUS_CONNECTED && !lwip_socket_available(c->sock);
  if (c->redirecting) redirect(c);
  else finish(c, LWHTTP_OK);
  return true;
}

static lwhttp_error_t parse_failure(const lwhttp_client_t *c)
{
  lwhttp_error_t e = lwhttp_parser_error(&c->parser);
  return e == LWHTTP_ERR_ABORTED && c->pending ? c->pending : e;
}

static void receive(lwhttp_client_t *c)
{
  size_t n;
  size_t off;
  while (c->state == CS_ACTIVE && (n = lwip_socket_read(c->sock, c->rx, LWHTTP_RX_SIZE)) != 0) {
    c->received = true;
    c->stats.bytes_received += n;
    for (off = 0; off < n;) {
      off += lwhttp_parser_execute(&c->parser, c->rx + off, n - off);
      if (c->state != CS_ACTIVE) return;
      if (lwhttp_parser_error(&c->parser)) {
        finish(c, parse_failure(c));
        return;
      }
      if (lwhttp_parser_is_complete(&c->parser) && message_done(c, off, n)) return;
    }
  }
}

static void pump(lwhttp_client_t *c)
{
  lwhttp_error_t e = send_more(c);
  if (e) {
    if (can_retry(c)) retry(c);
    else finish(c, e);
    return;
  }
  receive(c);
}

static bool tls_traceback(void)
{
  uint8_t n = 0;
  uint8_t i;
  uint8_t seen = 0;
  const struct lwip_traceback_entry *tb = lwip_get_traceback(&n);
  for (i = 0; tb && i < n && seen < 3; i++) {
    if (!tb[i].line) continue;
    if (tb[i].module == LWIP_DBG_MOD_TLS) return true;
    seen++;
  }
  return false;
}

static void hangup(lwhttp_client_t *c, struct lwip_socket *sock)
{
  lwip_status_t st = lwip_socket_status(sock);
  if (c->state == CS_ACTIVE) {
    receive(c);
    if (c->sock != sock) return;
  }
  if (c->state == CS_CONNECTING) {
    finish(c, c->tls_failed || (c->tls && tls_traceback())   ? LWHTTP_ERR_TLS
              : lwip_socket_last_error(sock) == LWIP_ERR_DNS ? LWHTTP_ERR_DNS
                                                             : LWHTTP_ERR_CONNECT);
    return;
  }
  if (c->state != CS_ACTIVE) {
    if (!c->upgraded) drop(c);
    return;
  }
  if (can_retry(c)) {
    retry(c);
    return;
  }
  if (st == LWIP_STATUS_CLOSED && !lwhttp_parser_finish(&c->parser) && lwhttp_parser_is_complete(&c->parser)) {
    message_done(c, 0, 0);
    return;
  }
  if (c->tls_failed || c->timed_out) {
    finish(c, c->tls_failed ? LWHTTP_ERR_TLS : LWHTTP_ERR_TIMEOUT);
    return;
  }
  finish(c, lwhttp_parser_error(&c->parser) && lwhttp_parser_error(&c->parser) != LWHTTP_ERR_CLOSED
                ? parse_failure(c)
                : LWHTTP_ERR_CLOSED);
}

static void on_socket(struct lwip_socket *sock, lwip_socket_event_type_t type, const void *data, void *arg)
{
  lwhttp_client_t *c = arg;
  c->busy++;
  if (sock == c->sock) {
    if (type == LWIP_SOCKET_EV_ERROR && data) {
      const lwip_socket_error_data_t *err = data;
      if (err->component == LWIP_SOCKET_ERR_COMP_TLS) c->tls_failed = true;
      if (err->raw_error == ERR_TIMEOUT && c->state == CS_ACTIVE) c->timed_out = true;
    }
    if (!lwip_socket_is_active(sock)) {
      hangup(c, sock);
    } else if (type == LWIP_SOCKET_EV_STATE_CHANGE) {
      if (((const lwip_socket_state_data_t *)data)->current == LWIP_STATUS_CONNECTED && c->state == CS_CONNECTING) {
        c->state = CS_ACTIVE;
        c->last_io = lwip_now_ms();
        pump(c);
      }
    } else if (type == LWIP_SOCKET_EV_IO) {
      if (c->state == CS_ACTIVE) {
        uint8_t flags = ((const lwip_socket_io_data_t *)data)->flags;
        uint32_t now = lwip_now_ms();
        if (flags & (LWIP_SOCKET_IO_READABLE | LWIP_SOCKET_IO_WRITABLE)) c->last_io = now;
        if ((flags & LWIP_SOCKET_IO_POLL) && c->waiting_continue) c->waiting_continue = false;
        if (c->config.timeout_ms && now - c->last_io >= c->config.timeout_ms) finish(c, LWHTTP_ERR_TIMEOUT);
        else pump(c);
      } else if (!c->upgraded && lwip_socket_available(sock)) {
        drop(c);
      }
    }
  }
  leave(c);
}

static bool identity_only(const char *value)
{
  const char *cursor = value;
  const char *tok;
  size_t n;
  while (lwhttp__list_next(&cursor, &tok, &n))
    if (!lwhttp__ieqn(tok, n, "identity")) return false;
  return true;
}

static bool kept(const lwhttp_client_t *c, const char *name)
{
  const char *const *k = c->config.keep_headers;
  if (!k || lwhttp__ieq(name, "location")) return true;
  for (; *k; k++)
    if (lwhttp__ieq(name, *k)) return true;
  return false;
}

static bool c_skip(lwhttp_parser_t *p, const char *name, size_t name_len)
{
  (void)name_len;
  return !kept(p->user, name);
}

static int c_header(lwhttp_parser_t *p, const char *name, size_t name_len, const char *value, size_t value_len)
{
  lwhttp_client_t *c = p->user;
  lwhttp_headers_t *h = &c->headers;
  if (lwhttp__ieq(name, "content-encoding") && !identity_only(value)) c->coded = true;
  if (!kept(c, name)) return 0;
  if (name_len + value_len + 2 + LINE_RESERVE > h->size - h->len) {
    c->pending = LWHTTP_ERR_HEADERS_TOO_LARGE;
    return 1;
  }
  lwhttp__headers_put(h, name, name_len, value, value_len);
  rebase(c);
  return 0;
}

static bool body_follows(const lwhttp_parser_t *p)
{
  if (p->flags & LWHTTP_MSG_NO_BODY) return false;
  return !(p->flags & LWHTTP_MSG_CONTENT_LENGTH) || p->content_length;
}

static int c_headers_complete(lwhttp_parser_t *p)
{
  lwhttp_client_t *c = p->user;
  const lwhttp_client_handlers_t *h = c->handlers;
  if (p->status < 200 && p->status != 101) return 0;
  if (!c->sent) c->abandon = true;
  c->waiting_continue = false;
  if (want_redirect(c)) {
    c->redirecting = true;
    return 0;
  }
  if (body_follows(p) && ((p->flags & LWHTTP_MSG_TRANSFER_CODED) || (!c->raw_coding && c->coded))) {
    c->pending = LWHTTP_ERR_UNSUPPORTED;
    return -1;
  }
  if (h && h->on_response && !h->on_response(c, c->arg)) return -1;
  return c->state != CS_ACTIVE ? -1 : 0;
}

static int c_body(lwhttp_parser_t *p, const uint8_t *data, size_t len)
{
  lwhttp_client_t *c = p->user;
  const lwhttp_client_handlers_t *h = c->handlers;
  if (c->state != CS_ACTIVE) return 1;
  if (c->redirecting || !h || !h->on_body) return 0;
  return !h->on_body(c, data, len, c->arg) || c->state != CS_ACTIVE;
}

void lwhttp_client_config_default(lwhttp_client_config_t *config)
{
  config->timeout_ms = 30000ul;
  config->header_size = 4096;
  config->max_redirects = 5;
  config->keep_alive = true;
  config->user_agent = "LWHTTP/" LWHTTP_VERSION_STRING;
  config->keep_headers = NULL;
}

lwhttp_client_t *lwhttp_client_create(const lwhttp_client_config_t *config)
{
  lwhttp_client_config_t cfg;
  lwhttp_client_t *c;
  if (config) cfg = *config;
  else lwhttp_client_config_default(&cfg);
  if (cfg.header_size < HEADER_MIN) cfg.header_size = HEADER_MIN;
  if (cfg.header_size > SIZE_MAX - sizeof(*c) - LWHTTP_RX_SIZE) return NULL;
  c = mem_malloc(sizeof(*c) + LWHTTP_RX_SIZE + cfg.header_size);
  if (!c) return NULL;
  memset(c, 0, sizeof(*c));
  c->config = cfg;
  c->rx = (uint8_t *)(c + 1);
  lwhttp_headers_init(&c->headers, (char *)(c->rx + LWHTTP_RX_SIZE), cfg.header_size);
  return c;
}

void lwhttp_client_destroy(lwhttp_client_t *client)
{
  if (!client) return;
  if (client->busy) {
    client->handlers = NULL;
    client->state = CS_DONE;
    client->doomed = true;
    return;
  }
  release(client);
}

lwhttp_error_t lwhttp_client_request(lwhttp_client_t *client, const lwhttp_request_t *request,
                                     const lwhttp_client_handlers_t *handlers, void *arg)
{
  lwhttp_client_t *c = client;
  const char *method;
  lwhttp_error_t e;
  size_t n;
  char *url;
  if (!c || !request || !request->url) return LWHTTP_ERR_ARG;
  if (active(c) || c->doomed) return LWHTTP_ERR_STATE;
  method = request->method ? request->method : "GET";
  if (!lwhttp_is_token(method) || (request->target && !lwhttp__target_ok(request->target, strlen(request->target))))
    return LWHTTP_ERR_ARG;
  n = strlen(request->url) + 1;
  url = mem_malloc(n);
  if (!url) return LWHTTP_ERR_MEM;
  memcpy(url, request->url, n);
  if (c->url) mem_free(c->url);
  if (c->upgraded) drop(c);
  c->url = url;
  c->request = *request;
  c->method = method;
  c->handlers = handlers;
  c->arg = arg;
  c->redirects = 0;
  c->retried = false;
  c->cross_origin = false;
  c->result = LWHTTP_OK;
  c->busy++;
  e = begin(c);
  if (e) {
    drop(c);
    c->state = CS_DONE;
    c->result = e;
  }
  leave(c);
  return e;
}

lwhttp_error_t lwhttp_client_wait(lwhttp_client_t *client)
{
  lwhttp_error_t e;
  if (!client) return LWHTTP_ERR_ARG;
  client->busy++;
  while (active(client)) lwhttp_service_events();
  e = client->result;
  leave(client);
  return e;
}

lwhttp_error_t lwhttp_client_request_sync(lwhttp_client_t *client, const lwhttp_request_t *request,
                                          const lwhttp_client_handlers_t *handlers, void *arg)
{
  lwhttp_error_t e = lwhttp_client_request(client, request, handlers, arg);
  return e ? e : lwhttp_client_wait(client);
}

void lwhttp_client_cancel(lwhttp_client_t *client)
{
  if (!client || !active(client)) return;
  client->busy++;
  client->reusable = false;
  finish(client, LWHTTP_ERR_ABORTED);
  leave(client);
}

void lwhttp_client_close(lwhttp_client_t *client)
{
  if (client && !active(client)) drop(client);
}

bool lwhttp_client_is_done(const lwhttp_client_t *client) { return client && client->state == CS_DONE; }

lwhttp_error_t lwhttp_client_result(const lwhttp_client_t *client) { return client ? client->result : LWHTTP_ERR_ARG; }

uint16_t lwhttp_client_status(const lwhttp_client_t *client) { return client ? client->parser.status : 0; }

uint8_t lwhttp_client_version(const lwhttp_client_t *client) { return client ? client->parser.version_minor : 0; }

const char *lwhttp_client_header(const lwhttp_client_t *client, const char *name)
{ return client ? lwhttp_headers_get(&client->headers, name) : NULL; }

const lwhttp_headers_t *lwhttp_client_headers(const lwhttp_client_t *client)
{ return client ? &client->headers : NULL; }

const char *lwhttp_client_url(const lwhttp_client_t *client) { return client ? client->url : NULL; }

const lwhttp_client_stats_t *lwhttp_client_stats(const lwhttp_client_t *client)
{ return client ? &client->stats : NULL; }

struct lwip_socket *lwhttp_client_detach(lwhttp_client_t *client, const uint8_t **pending, size_t *pending_len)
{
  struct lwip_socket *sock;
  if (!client || !client->upgraded || !client->sock) return NULL;
  sock = client->sock;
  lwip_socket_on_event(sock, 0, NULL, NULL);
  client->sock = NULL;
  if (pending) *pending = client->rx + client->rx_off;
  if (pending_len) *pending_len = client->rx_len;
  drop(client);
  return sock;
}
