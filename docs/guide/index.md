# Guide

How LWHTTP works and how to use it well.

::::{grid} 1 2 2 2
:gutter: 3

:::{grid-item-card} {octicon}`sync;1.1em;sd-text-primary` Event loop and callbacks
:link: async
:link-type: doc
The asynchronous model, the blocking wrappers and the rules for callbacks.
:::

:::{grid-item-card} {octicon}`upload;1.1em;sd-text-primary` Requests
:link: requests
:link-type: doc
Methods, targets, headers and every kind of request body.
:::

:::{grid-item-card} {octicon}`download;1.1em;sd-text-primary` Responses
:link: responses
:link-type: doc
Status, headers, streamed bodies, trailers and strict content handling.
:::

:::{grid-item-card} {octicon}`git-branch;1.1em;sd-text-primary` Redirects
:link: redirects
:link-type: doc
Which redirects are followed and how methods and headers change.
:::

:::{grid-item-card} {octicon}`plug;1.1em;sd-text-primary` Connections
:link: connections
:link-type: doc
Keep-alive reuse, retries, timeouts, upgrades and statistics.
:::

:::{grid-item-card} {octicon}`lock;1.1em;sd-text-primary` HTTPS
:link: https
:link-type: doc
TLS through lwIP-CE and the limits that come with it.
:::

:::{grid-item-card} {octicon}`cpu;1.1em;sd-text-primary` Memory
:link: memory
:link-type: doc
What each client costs and how to size it.
:::

:::{grid-item-card} {octicon}`alert;1.1em;sd-text-primary` Errors
:link: errors
:link-type: doc
Every result code and what usually causes it.
:::
::::

```{toctree}
:hidden:

async
requests
responses
redirects
connections
https
memory
errors
```
