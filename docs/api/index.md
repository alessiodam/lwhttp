# API reference

Everything is declared in a single header, `lwhttp.h`. Most programs only need the client API; the lower layers are
exported for programs that bring their own transport or parse HTTP from other sources.

::::{grid} 1 2 2 3
:gutter: 3

:::{grid-item-card} Client
:link: client
:link-type: doc
Create clients, send requests, read responses.
:::

:::{grid-item-card} Errors and utilities
:link: util
:link-type: doc
Result codes, the event loop, tokens and HTTP dates.
:::

:::{grid-item-card} Parser
:link: parser
:link-type: doc
The incremental HTTP/1.1 response parser.
:::

:::{grid-item-card} Header store
:link: headers
:link-type: doc
Compact storage and lookup for header fields.
:::

:::{grid-item-card} Writer
:link: writer
:link-type: doc
Bounded buffers and header serialization.
:::

:::{grid-item-card} URLs
:link: url
:link-type: doc
RFC 3986 parsing and reference resolution.
:::
::::

```{toctree}
:hidden:

client
util
parser
headers
writer
url
```
