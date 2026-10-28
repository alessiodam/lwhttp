#include <stdlib.h>

#include <ti/getcsc.h>
#include <ti/screen.h>

#include <lwhttp.h>
#include <lwip.h>

int main(void)
{
  static const lwhttp_request_t request = {.url = "https://tls13.akamai.io"};
  static char line[] = "HTTP 000";
  lwhttp_client_t *client;
  lwhttp_error_t result;
  uint16_t status = 0;

  os_ClrHome();
  if (!lwip_start()) {
    os_PutStrFull(lwip_get_start_errstring());
    while (!os_GetCSC());
    return 1;
  }
  if (!lwip_network_up()) result = LWHTTP_ERR_CONNECT;
  else if (!(client = lwhttp_client_create(NULL))) result = LWHTTP_ERR_MEM;
  else {
    result = lwhttp_client_request_sync(client, &request, NULL, NULL);
    status = lwhttp_client_status(client);
    lwhttp_client_destroy(client);
  }
  lwip_stop();
  if (result) os_PutStrFull(lwhttp_error_string(result));
  else {
    line[5] += status / 100;
    line[6] += status / 10 % 10;
    line[7] += status % 10;
    os_PutStrFull(line);
  }
  while (!os_GetCSC());
  return 0;
}
