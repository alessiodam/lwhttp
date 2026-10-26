#ifndef LWHTTP_NET_H
#define LWHTTP_NET_H

#include <lwip.h>

#include "internal.h"

#define LWHTTP_IO_CHUNK 512u
#define LWHTTP_RX_SIZE 512u

void lwhttp__bury(struct lwip_socket *sock);

#endif
