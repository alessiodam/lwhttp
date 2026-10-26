# Event loop and callbacks

LWHTTP never blocks unless you ask it to. A request is started with {c:func}`lwhttp_client_request` and then moves
forward whenever lwIP-CE reports socket activity. Those reports are delivered from {c:func}`lwhttp_service_events`,
which your program calls in its main loop.

## The main loop

Call {c:func}`lwhttp_service_events` instead of `lwip_service_events()`. It runs lwIP-CE's own event processing and
then frees sockets that LWHTTP retired while it was inside a callback.

```c
while (running) {
  lwhttp_service_events();
  draw_frame();
  handle_keys();
}
```

:::{important}
lwIP-CE delivers at most one queued event per socket per call, so call {c:func}`lwhttp_service_events` often. A
loop that does a lot of other work between calls makes transfers slower, not incorrect.
:::

## Lifecycle of a request


1. {c:func}`lwhttp_client_request` validates the request, builds the request head and either reuses a kept-alive
   connection or opens a new one. Problems found here (bad arguments, a bad URL, an unsupported scheme, no memory)
   are returned directly and **no callback is called**.
2. Once connected, the request head and body are written.
3. When the response head has been parsed, `on_response` runs. Status and headers are available through
   {c:func}`lwhttp_client_status` and {c:func}`lwhttp_client_header`.
4. `on_body` runs for every piece of the body, already de-chunked.
5. `on_complete` runs **exactly once** with the final result.

Redirects, interim `1xx` responses and automatic retries happen inside this flow without extra callbacks.

## Callback rules

| You may | Details |
| --- | --- |
| Start the next request from `on_complete` | The client is idle again by then; a kept-alive connection is reused. |
| Destroy the client from any callback | {c:func}`lwhttp_client_destroy` is deferred until the callback returns. |
| Cancel from any callback | {c:func}`lwhttp_client_cancel` finishes the request with `LWHTTP_ERR_ABORTED`. |
| Abort from `on_response` or `on_body` | Return `false`; the request finishes with `LWHTTP_ERR_ABORTED`. |

| You must not | Why |
| --- | --- |
| Keep `data` pointers from `on_body` | They point into the client's receive buffer and are reused immediately. |
| Call {c:func}`lwhttp_client_wait` or {c:func}`lwhttp_client_request_sync` from a callback | They run the event loop themselves. |
| Free request data before `on_complete` | The {c:type}`lwhttp_request_t` itself and its URL are copied, but `method`, `headers`, `body`, `target` and `body_arg` are used until the request finishes. The configured `user_agent` string must outlive the client. |

## Blocking wrappers

{c:func}`lwhttp_client_wait` loops on {c:func}`lwhttp_service_events` until the current request has finished and
returns its result. {c:func}`lwhttp_client_request_sync` is {c:func}`lwhttp_client_request` followed by
{c:func}`lwhttp_client_wait`. Handlers are optional with both; pass `NULL` when the status and headers are all you
need.

```c
if (lwhttp_client_request_sync(client, &request, NULL, NULL) == LWHTTP_OK) {
  const char *type = lwhttp_client_header(client, "Content-Type");
}
```

## Several clients

Clients are independent. Each one runs a single request at a time, so run requests in parallel by creating several
clients and driving them all from the same loop:

```c
lwhttp_client_request(a, &first, &handlers, &state_a);
lwhttp_client_request(b, &second, &handlers, &state_b);
while (!state_a.done || !state_b.done) lwhttp_service_events();
```
