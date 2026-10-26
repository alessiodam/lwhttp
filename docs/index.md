---
og:description: HTTP/1.1 and HTTPS client library for the TI-84 Plus CE, built on lwIP-CE.
---

# LWHTTP

An asynchronous HTTP/1.1 and HTTPS client library for the TI-84 Plus CE, built on
[lwIP-CE](https://github.com/cagscalclabs/lwip-ce) and shipped as a LibLoad library.

[Get started](getting-started.md) · [API reference](api/index.md) · [GitHub](https://github.com/alessiodam/lwhttp)

::::{grid} 1 2 2 3
:gutter: 3

:::{grid-item-card} {octicon}`zap;1.1em;sd-text-primary` Event driven
:link: guide/async
:link-type: doc
Requests run on lwIP-CE socket events and report through callbacks. One call in your main loop drives everything.
:::

:::{grid-item-card} {octicon}`clock;1.1em;sd-text-primary` Blocking when you want it
:link: guide/async
:link-type: doc
`lwhttp_client_request_sync` and `lwhttp_client_wait` wait for you when a program just needs an answer.
:::

:::{grid-item-card} {octicon}`lock;1.1em;sd-text-primary` HTTPS
:link: guide/https
:link-type: doc
`https://` URLs use lwIP-CE's TLS 1.3 stack with SNI and certificate checks against its truststore.
:::

:::{grid-item-card} {octicon}`checklist;1.1em;sd-text-primary` Real HTTP/1.1
:link: spec
:link-type: doc
Chunked transfer, trailers, interim responses, keep-alive reuse, redirects, `Expect: 100-continue` and strict framing rules.
:::

:::{grid-item-card} {octicon}`cpu;1.1em;sd-text-primary` Built for the eZ80
:link: guide/memory
:link-type: doc
One heap block per client, zero-copy body delivery, no division or 64-bit math on hot paths.
:::

:::{grid-item-card} {octicon}`beaker;1.1em;sd-text-primary` Tested everywhere
:link: testing
:link-type: doc
Host unit tests with sanitizers, fuzzing, coverage, CEmu emulator runs and a hardware suite.
:::
::::

## At a glance

::::{tab-set}
:::{tab-item} Asynchronous
```c
static bool on_body(lwhttp_client_t *client, const uint8_t *data, size_t len, void *arg)
{
  fwrite(data, 1, len, stdout);
  return true;
}

static void on_complete(lwhttp_client_t *client, lwhttp_error_t result, void *arg)
{
  *(bool *)arg = true;
}

static const lwhttp_client_handlers_t handlers = {NULL, on_body, on_complete};

lwhttp_client_t *client = lwhttp_client_create(NULL);
lwhttp_request_t request = {0};
bool done = false;

request.url = "https://tls13.akamai.io/";
if (lwhttp_client_request(client, &request, &handlers, &done) == LWHTTP_OK)
  while (!done) lwhttp_service_events();
```
:::
:::{tab-item} Blocking
```c
lwhttp_client_t *client = lwhttp_client_create(NULL);
lwhttp_request_t request = {0};

request.url = "https://tls13.akamai.io/";
if (lwhttp_client_request_sync(client, &request, NULL, NULL) == LWHTTP_OK)
  printf("HTTP %u\n", lwhttp_client_status(client));
```
:::
::::

```{toctree}
:hidden:
:caption: Start

getting-started
```

```{toctree}
:hidden:
:caption: Guide

guide/index
```

```{toctree}
:hidden:
:caption: Reference

api/index
spec
```

```{toctree}
:hidden:
:caption: Project

testing
changelog
```
