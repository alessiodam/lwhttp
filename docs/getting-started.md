# Getting started

## Requirements

| What | Version |
| --- | --- |
| [CE C toolchain](https://github.com/CE-Programming/toolchain) | v15.0 or newer |
| [lwIP-CE](https://github.com/cagscalclabs/lwip-ce) | 3.0, installed into CEdev |
| [ce-libify](https://github.com/alessiodam/toolchain_libify) | v1.1.2, only to build LWHTTP itself |
| Calculator | TI-84 Plus CE with a USB Ethernet adapter or the lwIP-CE Wi-Fi bridge |

## Install the library

Build LWHTTP and copy its stub and header into CEdev:

```sh
git clone https://github.com/alessiodam/lwhttp
cd lwhttp
make install
```

This produces `bin/LWHTTP.8xv` (the LibLoad library) and `bin/LWHTTP.lib` (the link stub), and installs
`LWHTTP.lib` plus `lwhttp.h` into `$CEDEV/lib/libload` and `$CEDEV/include`.

Prebuilt files are attached to every [release](https://github.com/alessiodam/lwhttp/releases).

## Set up a program

Programs link LWHTTP like any other LibLoad library. Like every lwIP-CE program, the makefile must keep the heap
below lwIP-CE's memory:

```make
NAME = MYAPP
BSSHEAP_LOW = 0xD072C6

include $(shell cedev-config --makefile)
```

Then include both headers:

```c
#include <lwip.h>
#include <lwhttp.h>
```

## Your first request

```c
#include <stdio.h>
#include <ti/getcsc.h>
#include <ti/screen.h>
#include <lwip.h>
#include <lwhttp.h>

int main(void)
{
  lwhttp_request_t request = {0};
  lwhttp_client_t *client;
  char line[27];

  os_ClrHome();
  if (!lwip_start() || !lwip_network_up()) return 1;

  client = lwhttp_client_create(NULL);
  request.url = "https://tls13.akamai.io/";
  if (lwhttp_client_request_sync(client, &request, NULL, NULL) == LWHTTP_OK) {
    sprintf(line, "HTTP %u", lwhttp_client_status(client));
    os_PutStrFull(line);
  }

  lwhttp_client_destroy(client);
  while (!os_GetCSC()) lwhttp_service_events();
  lwip_stop();
  return 0;
}
```

`lwip_start()` brings up lwIP-CE and `lwip_network_up()` starts the network interface. After that, every LWHTTP
call is non-blocking except the explicit wrappers {c:func}`lwhttp_client_request_sync` and
{c:func}`lwhttp_client_wait`.

## Send it to the calculator

| File | Purpose |
| --- | --- |
| `LWHTTP.8xv` | LWHTTP |
| lwIP-CE files | the lwIP-CE stub (`lwip.8xv`), its app installer and the truststore `lwIPCERT.8xv` |
| CE libraries | `clibs.8xg` (graphx, fileioc, usbdrvce and friends) |
| your program | `MYAPP.8xp` |

Run the lwIP-CE installer once, then your program. The
[`examples/http_get`](https://github.com/alessiodam/lwhttp/tree/main/examples/http_get) program in the
repository is a complete, buildable version of the code above with progress callbacks.

## Next steps

::::{grid} 1 2 2 2
:gutter: 2

:::{grid-item-card} Event loop and callbacks
:link: guide/async
:link-type: doc
How requests progress and what you may do inside callbacks.
:::

:::{grid-item-card} Sending requests
:link: guide/requests
:link-type: doc
Methods, headers, in-memory and streamed bodies.
:::
::::
