# Memory

LWHTTP takes all of its memory from the lwIP-CE heap, the same 42 KB pool that lwIP-CE uses for sockets, packet
buffers and TLS. It has no static buffers of its own beyond a few bytes.

## Per client

{c:func}`lwhttp_client_create` allocates one block:

| Part | Size |
| --- | --- |
| Client state | about 200 bytes |
| Receive buffer | 512 bytes |
| Send buffer | 512 bytes |
| Header store | {c:member}`lwhttp_client_config_t.header_size`, 4096 by default |

The default client therefore needs about 5.3 KB of contiguous heap.

## Per request

While a request runs, LWHTTP also holds a copy of the URL and host name and the serialized request head, together
usually well under 1 KB. lwIP-CE adds its own socket state, a receive ring of 512 bytes to 4 KB and, for HTTPS, its
TLS context.

## Sizing the header store

The header store holds the response head and trailers, one field after another. Each field costs its name, its
value and two bytes. 128 bytes always stay free for chunk-size and trailer lines, and the longest single header line
must also fit.

| Server | Response header section |
| --- | --- |
| Small APIs, `httpbin.org` | under 1.5 KB |
| `duckduckgo.com` | about 3 KB, with a 2.2 KB `Content-Security-Policy` line |
| `github.com` | about 5.5 KB, with a 4 KB line |

When you only talk to your own server, 1 KB is often enough. The minimum is 256 bytes.

## Recommendations

- Create one client per concurrent request and reuse it; creating and destroying clients repeatedly fragments the
  heap.
- Keep few clients alive at once, especially next to HTTPS connections.
- Stream large bodies with `on_body` instead of collecting them in memory.
