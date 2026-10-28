#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <ti/getcsc.h>
#include <ti/screen.h>

#include <lwhttp.h>
#include <lwip.h>

#define URL "https://tls13.akamai.io/"

typedef struct {
  uint32_t bytes;
  bool done;
} progress_t;

static void print(const char *s)
{
  os_PutStrFull(s);
  os_NewLine();
}

static bool on_response(lwhttp_client_t *client, void *arg)
{
  char line[27];
  const char *type = lwhttp_client_header(client, "Content-Type");
  (void)arg;
  sprintf(line, "HTTP/1.%u %u", lwhttp_client_version(client), lwhttp_client_status(client));
  print(line);
  if (type) print(type);
  return true;
}

static bool on_body(lwhttp_client_t *client, const uint8_t *data, size_t len, void *arg)
{
  progress_t *progress = arg;
  (void)client;
  (void)data;
  progress->bytes += len;
  return true;
}

static void on_complete(lwhttp_client_t *client, lwhttp_error_t result, void *arg)
{
  progress_t *progress = arg;
  char line[27];
  (void)client;
  progress->done = true;
  if (result) {
    print(lwhttp_error_string(result));
    return;
  }
  sprintf(line, "%lu bytes", (unsigned long)progress->bytes);
  print(line);
}

static const lwhttp_client_handlers_t handlers = {on_response, on_body, on_complete};

int main(void)
{
  lwhttp_request_t request = {0};
  progress_t progress = {0};
  lwhttp_client_t *client;

  os_ClrHome();
  if (!lwip_start() || !lwip_network_up()) {
    print("lwIP start failed");
    while (!os_GetCSC());
    return 1;
  }

  client = lwhttp_client_create(NULL);
  if (!client) {
    print("out of memory");
    lwip_stop();
    return 1;
  }

  request.url = URL;
  print("GET " URL);
  if (lwhttp_client_request(client, &request, &handlers, &progress) == LWHTTP_OK)
    while (!progress.done && os_GetCSC() != sk_Clear) lwhttp_service_events();
  lwhttp_client_cancel(client);

  request.method = "HEAD";
  print("HEAD " URL);
  progress = (progress_t){0};
  lwhttp_client_request_sync(client, &request, &handlers, &progress);

  lwhttp_client_destroy(client);
  while (!os_GetCSC()) lwhttp_service_events();
  lwip_stop();
  return 0;
}
