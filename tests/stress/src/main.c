#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fileioc.h>
#include <ti/getcsc.h>
#include <ti/screen.h>

#include <lwhttp.h>
#include <lwip.h>

#define LOG_NAME "STRESLOG"
#define HOG_SLOTS 256
#define MAX_CLIENTS 64

static uint8_t log_handle;
static unsigned long passed;
static unsigned long failed;
static char text[64];
static void *hog[HOG_SLOTS];
static size_t hog_count;
static lwhttp_client_t *clients[MAX_CLIENTS];
static size_t base_used;

static void say(const char *s)
{
  os_PutStrFull(s);
  os_NewLine();
  if (!log_handle) return;
  ti_Write(s, strlen(s), 1, log_handle);
  ti_Write("\n", 1, 1, log_handle);
}

static bool check(bool ok, const char *what)
{
  if (ok) {
    passed++;
  } else {
    failed++;
    snprintf(text, sizeof(text), "FAIL %s", what);
    say(text);
  }
  return ok;
}

static struct mem_accounting_stats heap(void)
{
  struct mem_accounting_stats m = {0};
  mem_get_stats(&m);
  return m;
}

static void settle(void)
{
  uint8_t i;
  for (i = 0; i < 20; i++) lwhttp_service_events();
}

static void leak_check(const char *what)
{
  size_t used;
  settle();
  used = heap().heap_used;
  if (!check(used == base_used, what)) {
    snprintf(text, sizeof(text), "  used %u base %u", (unsigned)used, (unsigned)base_used);
    say(text);
  }
}

static void hog_to(size_t level)
{
  size_t size = 4096;
  while (hog_count < HOG_SLOTS && size >= 8) {
    size_t free_bytes = heap().heap_free;
    void *p;
    if (free_bytes <= level) break;
    if (size > free_bytes - level) size = free_bytes - level;
    p = mem_malloc(size);
    if (p) hog[hog_count++] = p;
    else size /= 2;
  }
}

static void unhog(void)
{
  while (hog_count) mem_free(hog[--hog_count]);
}

static size_t make_clients(const lwhttp_client_config_t *cfg)
{
  size_t n = 0;
  while (n < MAX_CLIENTS && (clients[n] = lwhttp_client_create(cfg))) n++;
  return n;
}

static void destroy_clients(size_t n)
{
  while (n) lwhttp_client_destroy(clients[--n]);
}

static void test_size_wrap(void)
{
  lwhttp_client_config_t cfg;
  lwhttp_client_t *c;
  size_t k;
  lwhttp_client_config_default(&cfg);
  say("size wrap scan");
  for (k = 0; k < 4096; k++) {
    cfg.header_size = SIZE_MAX - k;
    c = lwhttp_client_create(&cfg);
    if (c) {
      char what[32];
      snprintf(what, sizeof(what), "header_size max-%u", (unsigned)k);
      check(false, what);
      lwhttp_client_destroy(c);
      break;
    }
  }
  cfg.header_size = 0x10000;
  c = lwhttp_client_create(&cfg);
  check(!c, "header_size 64K");
  lwhttp_client_destroy(c);
  cfg.header_size = 0x7FFFFF;
  c = lwhttp_client_create(&cfg);
  check(!c, "header_size 8M");
  lwhttp_client_destroy(c);
  leak_check("size wrap leak");
}

static void test_exhaust(void)
{
  lwhttp_client_config_t cfg;
  size_t n;
  say("client exhaustion");
  n = make_clients(NULL);
  check(n > 0 && n < MAX_CLIENTS, "default clients");
  destroy_clients(n);
  leak_check("default clients leak");

  lwhttp_client_config_default(&cfg);
  cfg.header_size = 0;
  n = make_clients(&cfg);
  check(n > 0, "tiny clients");
  snprintf(text, sizeof(text), "  %u tiny clients", (unsigned)n);
  say(text);
  destroy_clients(n);
  leak_check("tiny clients leak");
}

static void test_huge_requests(void)
{
  static lwhttp_header_t headers[100];
  lwhttp_request_t req = {0};
  lwhttp_client_t *c;
  char *url;
  char *value;
  size_t i;
  say("huge requests");
  c = lwhttp_client_create(NULL);
  if (!check(c, "client for huge requests")) return;

  url = malloc(40000);
  if (url) {
    memset(url, 'a', 40000);
    memcpy(url, "http://example.com/", 19);
    url[39999] = '\0';
    req.url = url;
    check(lwhttp_client_request(c, &req, NULL, NULL) != LWHTTP_OK, "40K url");
    url[20000] = '\0';
    check(lwhttp_client_request(c, &req, NULL, NULL) != LWHTTP_OK, "20K url");
    free(url);
  }

  value = malloc(400);
  if (value) {
    memset(value, 'v', 399);
    value[399] = '\0';
    for (i = 0; i < 100; i++) {
      headers[i].name = "X-Big";
      headers[i].value = value;
    }
    req.url = "http://example.com/";
    req.headers = headers;
    req.header_count = 100;
    check(lwhttp_client_request(c, &req, NULL, NULL) != LWHTTP_OK, "40K of headers");
    req.method = "POST";
    req.body = (const uint8_t *)value;
    req.body_length = 0xFFFFFFFFul;
    req.header_count = 0;
    check(lwhttp_client_request(c, &req, NULL, NULL) != LWHTTP_OK, "4G body length");
    free(value);
  }

  lwhttp_client_destroy(c);
  leak_check("huge requests leak");
}

static void test_low_heap(void)
{
  static const lwhttp_request_t req = {.url = "http://example.com/"};
  lwhttp_client_t *c;
  size_t level;
  say("low heap requests");
  for (level = 0; level <= 8192; level += 64) {
    hog_to(level);
    c = lwhttp_client_create(NULL);
    if (c) lwhttp_client_request(c, &req, NULL, NULL);
    lwhttp_client_destroy(c);
    unhog();
  }
  leak_check("low heap leak");
}

static void test_churn(void)
{
  static const lwhttp_request_t req = {.url = "http://example.com/"};
  lwhttp_client_t *c;
  unsigned i;
  say("churn x500");
  for (i = 0; i < 500; i++) {
    c = lwhttp_client_create(NULL);
    if (!c) break;
    lwhttp_client_request(c, &req, NULL, NULL);
    lwhttp_client_cancel(c);
    lwhttp_client_destroy(c);
  }
  check(i == 500, "churn create");
  leak_check("churn leak");
}

static void net_level(const char *url, size_t level)
{
  lwhttp_request_t req = {0};
  lwhttp_client_t *c = lwhttp_client_create(NULL);
  lwhttp_error_t e;
  if (!c) {
    check(false, "net client");
    return;
  }
  req.url = url;
  hog_to(level);
  e = lwhttp_client_request_sync(c, &req, NULL, NULL);
  unhog();
  snprintf(text, sizeof(text), "  %5u: %s %u", (unsigned)level, lwhttp_error_string(e),
           (unsigned)lwhttp_client_status(c));
  say(text);
  lwhttp_client_destroy(c);
  settle();
}

static void test_network(void)
{
  static const size_t levels[] = {0, 512, 1024, 2048, 4096, 8192, 12288, 16384, 24576};
  static const char *const urls[] = {"http://example.com/", "https://tls13.akamai.io/"};
  static const lwhttp_request_t req = {.url = "http://example.com/"};
  size_t used;
  size_t n;
  size_t i;
  uint8_t u;
  for (u = 0; u < 2; u++) {
    say(urls[u]);
    for (i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) net_level(urls[u], levels[i]);
  }

  say("parallel clients");
  n = make_clients(NULL);
  for (i = 0; i < n; i++)
    if (lwhttp_client_request(clients[i], &req, NULL, NULL)) break;
  snprintf(text, sizeof(text), "  %u clients, %u started", (unsigned)n, (unsigned)i);
  say(text);
  for (i = 0; i < n; i++) lwhttp_client_wait(clients[i]);
  destroy_clients(n);
  settle();
  used = heap().heap_used;
  snprintf(text, sizeof(text), "net used %u base %u", (unsigned)used, (unsigned)base_used);
  say(text);
  check(used <= base_used + 1024, "net leak");
}

int main(void)
{
  uint8_t key;
  os_ClrHome();
  ti_Delete(LOG_NAME);
  log_handle = ti_Open(LOG_NAME, "w");
  say("STRESS LWHTTP");
  if (!check(lwip_start(), "lwip_start")) goto done;
  settle();
  base_used = heap().heap_used;

  test_size_wrap();
  test_exhaust();
  test_huge_requests();
  test_low_heap();
  test_churn();

  say("[enter] net, [clear] skip");
  while (!(key = os_GetCSC()));
  if (key == sk_Enter) {
    if (check(lwip_network_up(), "lwip_network_up")) {
      settle();
      base_used = heap().heap_used;
      test_network();
    }
  }
  lwip_stop();

done:
  snprintf(text, sizeof(text), "DONE pass %lu fail %lu", passed, failed);
  say(text);
  if (log_handle) ti_Close(log_handle);
  while (!os_GetCSC());
  return 0;
}
