# Connections

## Keep-alive

Each client keeps its last connection open when the server allows it. The next request to the same origin
(scheme, host and port) reuses it and skips the TCP and TLS handshakes, which on a calculator is the difference
between a few hundred milliseconds and several seconds.

A connection is kept only when the response was complete, correctly framed, and the server did not ask to close.
Turn reuse off with {c:member}`lwhttp_client_config_t.keep_alive`, or close an idle connection explicitly with
{c:func}`lwhttp_client_close`.

## Automatic retry

Servers close idle connections whenever they like, so a reused connection can turn out to be dead. When that
happens before any byte of the response arrived, LWHTTP retries the request once on a fresh connection, provided
the method is idempotent (`GET`, `HEAD`, `PUT`, `DELETE`, `OPTIONS`, `TRACE`) and the body is not streamed from a
reader. Other requests fail with `LWHTTP_ERR_CLOSED`, because they may already have been applied.

## Timeouts

{c:member}`lwhttp_client_config_t.timeout_ms` (30 seconds by default) is an inactivity timeout, enforced by
lwIP-CE on each connection. Any traffic in either direction restarts it, so a slow but steady download never times
out. A timeout while connecting reports `LWHTTP_ERR_CONNECT`; later it reports `LWHTTP_ERR_CLOSED`.

## Cancelling

{c:func}`lwhttp_client_cancel` stops the current request immediately, closes its connection and calls `on_complete`
with `LWHTTP_ERR_ABORTED`. It does nothing when the client is idle.

## Upgrades

A `101 Switching Protocols` response, or a `2xx` response to `CONNECT`, completes the request successfully and
leaves the connection open. Take it over with {c:func}`lwhttp_client_detach`, which also returns any bytes already
read past the response head:

```c
const uint8_t *pending;
size_t pending_len;
struct lwip_socket *sock = lwhttp_client_detach(client, &pending, &pending_len);
```

From then on the socket belongs to you; LWHTTP no longer touches it.

## Statistics

{c:func}`lwhttp_client_stats` returns counters for the lifetime of a client: connections opened, requests sent on a
reused connection, automatic retries, redirects followed, bytes sent and received, and write stalls. They make it
easy to confirm that keep-alive works or to see where time goes.
