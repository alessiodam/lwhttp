#include "net.h"

#define GRAVE_SLOTS 16

static struct lwip_socket *graves[GRAVE_SLOTS];
static uint8_t grave_count;

void lwhttp__bury(struct lwip_socket *sock)
{
  lwip_socket_on_event(sock, 0, NULL, NULL);
  if (grave_count < GRAVE_SLOTS) graves[grave_count++] = sock;
  else lwip_socket_destroy(sock);
}

void lwhttp_service_events(void)
{
  lwip_service_events();
  while (grave_count) lwip_socket_destroy(graves[--grave_count]);
}
