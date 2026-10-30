#ifndef LWHTTP_H
#define LWHTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Major version of the library this header belongs to. */
#define LWHTTP_VERSION_MAJOR 1
/** Minor version of the library this header belongs to. */
#define LWHTTP_VERSION_MINOR 0
/** Patch version of the library this header belongs to. */
#define LWHTTP_VERSION_PATCH 0
/**
 * Version as a SemVer 2.0.0 string, ``"MAJOR.MINOR.PATCH"`` with an optional ``-prerelease`` suffix such as
 * ``"1.0.0-rc1"``.
 *
 * Also used in the default ``User-Agent`` (``"LWHTTP/" LWHTTP_VERSION_STRING``). Compare with
 * :c:func:`lwhttp_version_string` to detect a header/library mismatch.
 */
#define LWHTTP_VERSION_STRING "1.0.0"

/**
 * Body length meaning "not known in advance".
 *
 * Set :c:member:`lwhttp_request_t.body_length` to this value together with
 * :c:member:`lwhttp_request_t.body_reader` to upload the body with ``Transfer-Encoding: chunked``.
 * The parser likewise rejects a response ``Content-Length`` of exactly 4294967295 with
 * :c:enumerator:`LWHTTP_ERR_TOO_LARGE`.
 */
#define LWHTTP_LENGTH_UNKNOWN 0xFFFFFFFFul
/**
 * Return value for :c:member:`lwhttp_parser_callbacks_t.on_headers_complete` that tells the parser the
 * message has no body.
 *
 * The framing flags are replaced by :c:macro:`LWHTTP_MSG_NO_BODY` and the message completes immediately
 * after the header section. Use it when the parser was not told the request method (for example a response
 * to ``HEAD``, see :c:func:`lwhttp_parser_set_request`).
 */
#define LWHTTP_PARSER_SKIP_BODY 1

/**
 * Message flag: the body uses chunked transfer coding (``chunked`` is the final ``Transfer-Encoding``).
 *
 * The parser removes the chunk framing; :c:member:`lwhttp_parser_callbacks_t.on_body` receives only chunk
 * data. Chunk extensions are validated and ignored.
 */
#define LWHTTP_MSG_CHUNKED 0x0001u
/**
 * Message flag: the body is framed by ``Content-Length`` (stored in
 * :c:member:`lwhttp_parser.content_length`, possibly 0).
 */
#define LWHTTP_MSG_CONTENT_LENGTH 0x0002u
/**
 * Message flag: the connection may stay open for another request after this message.
 *
 * HTTP/1.1 is persistent unless ``Connection: close`` is present; HTTP/1.0 only with
 * ``Connection: keep-alive`` and no ``close``. Never set for upgrades, close-delimited bodies, or a
 * ``Transfer-Encoding`` combined with ``Content-Length`` or sent by an HTTP/1.0 server.
 */
#define LWHTTP_MSG_KEEP_ALIVE 0x0004u
/**
 * Message flag: the connection switches protocol after this response head.
 *
 * Set for ``101 Switching Protocols`` and for any 2xx answer to a ``CONNECT`` request. The message has no
 * body, and every byte after the head belongs to the new protocol: :c:func:`lwhttp_parser_execute`
 * consumes nothing further.
 */
#define LWHTTP_MSG_UPGRADE 0x0008u
/**
 * Message flag: the body runs until the connection closes.
 *
 * Used when there is neither ``Content-Length`` nor ``Transfer-Encoding``, or when the final transfer
 * coding is not ``chunked``. Call :c:func:`lwhttp_parser_finish` at end of stream to complete the message.
 */
#define LWHTTP_MSG_CLOSE_DELIMITED 0x0010u
/**
 * Message flag: ``Transfer-Encoding`` lists a coding other than ``chunked`` (for example ``gzip``).
 *
 * The parser does not decode it, so the body bytes are still encoded. Combined with
 * :c:macro:`LWHTTP_MSG_CHUNKED` or :c:macro:`LWHTTP_MSG_CLOSE_DELIMITED`. The client rejects such
 * responses with :c:enumerator:`LWHTTP_ERR_UNSUPPORTED`.
 */
#define LWHTTP_MSG_TRANSFER_CODED 0x0020u
/**
 * Message flag: the message has no body.
 *
 * Set for 1xx, 204 and 304 responses, responses to ``HEAD``, upgrades, and when
 * :c:member:`lwhttp_parser_callbacks_t.on_headers_complete` returned :c:macro:`LWHTTP_PARSER_SKIP_BODY`.
 * After the header section exactly one of :c:macro:`LWHTTP_MSG_NO_BODY`,
 * :c:macro:`LWHTTP_MSG_CONTENT_LENGTH`, :c:macro:`LWHTTP_MSG_CHUNKED` and
 * :c:macro:`LWHTTP_MSG_CLOSE_DELIMITED` is set.
 */
#define LWHTTP_MSG_NO_BODY 0x0040u

/**
 * Result codes returned by LWHTTP functions and passed to
 * :c:member:`lwhttp_client_handlers_t.on_complete`.
 *
 * :c:func:`lwhttp_error_string` gives a short English description of each.
 */
typedef enum {
  /** Success. */
  LWHTTP_OK = 0,
  /**
   * Invalid argument: a required pointer was ``NULL``, or a method, request-target, header name or header
   * value is malformed, or the request supplied its own ``Content-Length`` or ``Transfer-Encoding`` header.
   */
  LWHTTP_ERR_ARG,
  /** An allocation from the lwIP-CE heap failed. */
  LWHTTP_ERR_MEM,
  /** The client is busy with another request, or is being destroyed. */
  LWHTTP_ERR_STATE,
  /**
   * The URL could not be parsed, or a request URL has no scheme or no host. Also returned for a redirect
   * target without a host.
   */
  LWHTTP_ERR_URL,
  /** lwIP-CE could not resolve the host name. */
  LWHTTP_ERR_DNS,
  /**
   * The connection could not be established (refused, unreachable, or the inactivity timeout expired while
   * connecting).
   */
  LWHTTP_ERR_CONNECT,
  /**
   * The TLS handshake or TLS session failed: untrusted, expired or mismatched certificate, a server that
   * does not offer TLS 1.3, or a certificate chain that uses a curve other than P-256.
   */
  LWHTTP_ERR_TLS,
  /** lwIP-CE reported a write error other than "out of memory" or "closed". */
  LWHTTP_ERR_SEND,
  /**
   * The connection closed before the response was complete. Also returned by
   * :c:func:`lwhttp_parser_finish` for a truncated message.
   */
  LWHTTP_ERR_CLOSED,
  /** The response violates HTTP/1.1 syntax or framing rules. */
  LWHTTP_ERR_PROTOCOL,
  /** The response is not HTTP/1.x. */
  LWHTTP_ERR_VERSION,
  /**
   * The URL scheme is not ``http`` or ``https``, or the response body uses a transfer coding other than
   * ``chunked`` or a content coding other than ``identity``.
   */
  LWHTTP_ERR_UNSUPPORTED,
  /**
   * A length does not fit: ``Content-Length`` or a chunk size exceeds 32 bits, a chunk-size line does not
   * fit the line buffer, or a resolved URL does not fit the output buffer.
   */
  LWHTTP_ERR_TOO_LARGE,
  /**
   * The response header section (or trailer section) does not fit the header buffer, or a single line is
   * longer than the parser's line buffer, or a :c:type:`lwhttp_headers_t` store is full.
   */
  LWHTTP_ERR_HEADERS_TOO_LARGE,
  /** A redirect ``Location`` could not be resolved against the current URL. */
  LWHTTP_ERR_REDIRECT,
  /**
   * The request or parse was stopped: a callback asked to stop, :c:func:`lwhttp_client_cancel` was
   * called, or a body reader returned an invalid length.
   */
  LWHTTP_ERR_ABORTED,
  /**
   * The connection was open but nothing was received or acknowledged for
   * :c:member:`lwhttp_client_config_t.timeout_ms`.
   */
  LWHTTP_ERR_TIMEOUT
} lwhttp_error_t;

/** One request header field, used in :c:member:`lwhttp_request_t.headers`. */
typedef struct {
  /** Field name; must be a valid token (see :c:func:`lwhttp_is_token`). Must not be ``NULL``. */
  const char *name;
  /**
   * Field value; must not be ``NULL`` and must not contain control characters other than horizontal tab
   * (so no CR or LF). Bytes 0x80 and above are allowed.
   */
  const char *value;
} lwhttp_header_t;

/** Response parser state. See :c:struct:`lwhttp_parser`. */
typedef struct lwhttp_parser lwhttp_parser_t;

/**
 * Callbacks invoked by :c:func:`lwhttp_parser_execute` and :c:func:`lwhttp_parser_finish`.
 *
 * Every member may be ``NULL``. Except where noted, a callback returns 0 to continue; any other value stops
 * parsing and puts the parser in the error state with :c:enumerator:`LWHTTP_ERR_ABORTED`. Callbacks run
 * for every message, including interim (1xx) responses. All pointers passed to callbacks point into the
 * parser's line buffer or the caller's input and are valid only for the duration of the call.
 */
typedef struct {
  /**
   * Called once the status line has been parsed.
   *
   * :param parser: The parser.
   * :param status: Three-digit status code (100 to 999).
   * :param reason: NUL-terminated reason phrase, possibly empty.
   * :return: 0 to continue, nonzero to abort.
   */
  int (*on_status_line)(lwhttp_parser_t *parser, uint16_t status, const char *reason);
  /**
   * Called for each header field, in order. Obsolete folded continuation lines are joined to the field
   * with a space.
   *
   * :param parser: The parser.
   * :param name: Field name, NUL-terminated.
   * :param name_len: Length of ``name``.
   * :param value: Field value with surrounding whitespace removed, NUL-terminated.
   * :param value_len: Length of ``value``.
   * :return: 0 to continue, nonzero to abort.
   */
  int (*on_header)(lwhttp_parser_t *parser, const char *name, size_t name_len, const char *value, size_t value_len);
  /**
   * Called after the empty line that ends the header section.
   *
   * :c:member:`lwhttp_parser.flags` already holds the final framing, keep-alive and upgrade flags.
   *
   * :param parser: The parser.
   * :return: 0 to continue, :c:macro:`LWHTTP_PARSER_SKIP_BODY` to treat the message as bodiless, any other
   *     value to abort.
   */
  int (*on_headers_complete)(lwhttp_parser_t *parser);
  /**
   * Called with body data as it arrives, possibly many times per message. Chunk framing is already removed.
   *
   * :param parser: The parser.
   * :param data: Body bytes; valid only during the call.
   * :param len: Number of bytes in ``data``, never 0.
   * :return: 0 to continue, nonzero to abort.
   */
  int (*on_body)(lwhttp_parser_t *parser, const uint8_t *data, size_t len);
  /**
   * Called for each trailer field after the last chunk. Same arguments as
   * :c:member:`lwhttp_parser_callbacks_t.on_header`; trailer fields are not interpreted by the parser.
   *
   * :return: 0 to continue, nonzero to abort.
   */
  int (*on_trailer)(lwhttp_parser_t *parser, const char *name, size_t name_len, const char *value, size_t value_len);
  /**
   * Called when a message is complete. :c:func:`lwhttp_parser_is_complete` already returns true.
   *
   * :param parser: The parser.
   * :return: 0 to continue, nonzero to abort (the parser then enters the error state).
   */
  int (*on_message_complete)(lwhttp_parser_t *parser);
  /**
   * Called when a header or trailer line does not fit in the line buffer. Return true to discard the field,
   * including any folded continuation lines, instead of failing with
   * :c:enumerator:`LWHTTP_ERR_HEADERS_TOO_LARGE`. ``Content-Length``, ``Transfer-Encoding`` and
   * ``Connection`` are never offered: the parser needs them.
   *
   * :param parser: The parser.
   * :param name: Field name, NUL-terminated.
   * :param name_len: Length of ``name``.
   * :return: true to skip the field, false to fail.
   */
  bool (*skip_field)(lwhttp_parser_t *parser, const char *name, size_t name_len);
} lwhttp_parser_callbacks_t;

/**
 * Incremental HTTP/1.x response parser.
 *
 * The parser does not allocate. It copies one line at a time (status line, header, trailer, chunk size)
 * into a caller-supplied buffer and streams body bytes straight from the input. Initialise it with
 * :c:func:`lwhttp_parser_init`. Fields documented as internal must not be modified; the others may be read
 * at any time, typically from callbacks.
 */
struct lwhttp_parser {
  /** Callback table given to :c:func:`lwhttp_parser_init`; may be ``NULL``. */
  const lwhttp_parser_callbacks_t *callbacks;
  /** Opaque pointer for the application, given to :c:func:`lwhttp_parser_init`. */
  void *user;
  /** Line buffer given to :c:func:`lwhttp_parser_init`. */
  char *line;
  /** Size of :c:member:`lwhttp_parser.line` in bytes. */
  size_t line_size;
  /** Internal: bytes currently held in the line buffer. */
  size_t line_len;
  /**
   * Value of the last ``Content-Length`` header parsed, or 0. Only defines the body length when
   * :c:macro:`LWHTTP_MSG_CONTENT_LENGTH` is set (``Transfer-Encoding`` takes precedence).
   */
  uint32_t content_length;
  /** Internal: body bytes left in the current length-framed body or chunk. */
  uint32_t remaining;
  /** Status code of the current message, or 0 before its status line has been parsed. */
  uint16_t status;
  /** ``LWHTTP_MSG_*`` flags of the current message, valid from ``on_headers_complete`` onward. */
  uint8_t flags;
  /** Internal: header bookkeeping bits. */
  uint8_t internal;
  /** Internal: parser state. */
  uint8_t state;
  /** Internal: request method mode set by :c:func:`lwhttp_parser_set_request`. */
  uint8_t request;
  /** Major HTTP version from the status line (always 1 for an accepted message). */
  uint8_t version_major;
  /** Minor HTTP version from the status line (0 for HTTP/1.0, 1 for HTTP/1.1). */
  uint8_t version_minor;
  /** Internal: :c:type:`lwhttp_error_t` of the failure, read it with :c:func:`lwhttp_parser_error`. */
  uint8_t error;
};

/**
 * Header store: name/value pairs packed into a caller-supplied buffer.
 *
 * Each field takes ``strlen(name) + strlen(value) + 2`` bytes. Lookups are case-insensitive and
 * duplicate names are kept as separate entries in insertion order. Initialise with
 * :c:func:`lwhttp_headers_init`; the fields are read-only for applications.
 */
typedef struct {
  /** Storage buffer. */
  char *data;
  /** Size of :c:member:`lwhttp_headers_t.data` in bytes. */
  size_t size;
  /** Bytes of :c:member:`lwhttp_headers_t.data` in use. */
  size_t len;
  /** Number of stored fields. */
  size_t count;
} lwhttp_headers_t;

/**
 * Append-only output buffer that counts overflow instead of failing.
 *
 * Appends that do not fit are not written but still advance :c:member:`lwhttp_buffer_t.len`, so a pass
 * with a ``NULL``/0-sized buffer measures the size needed, and :c:func:`lwhttp_buffer_ok` tells whether
 * everything fit. The buffer is never NUL-terminated.
 */
typedef struct {
  /** Output storage; may be ``NULL`` when :c:member:`lwhttp_buffer_t.size` is 0. */
  char *data;
  /** Capacity of :c:member:`lwhttp_buffer_t.data` in bytes. */
  size_t size;
  /** Total bytes appended so far, including any that did not fit; stops at ``SIZE_MAX`` instead of wrapping. */
  size_t len;
} lwhttp_buffer_t;

/**
 * Components of a URI reference split by :c:func:`lwhttp_url_parse`.
 *
 * All pointers point into the parsed string and are not NUL-terminated; use the matching ``_len`` field.
 * Components are not decoded or case-normalised. A pointer is meaningful only when its ``has_`` flag is set
 * (``path`` is always set but may be empty; without an authority ``host`` is ``NULL`` with length 0).
 */
typedef struct {
  /** Scheme without the ``:``. */
  const char *scheme;
  /** Authority without the leading ``//``: userinfo, host and port. */
  const char *authority;
  /** Host, possibly empty; for an IP literal, the address without the brackets. */
  const char *host;
  /** Path, possibly empty. */
  const char *path;
  /** Query without the ``?``. */
  const char *query;
  /** Fragment without the ``#``. */
  const char *fragment;
  /** Length of :c:member:`lwhttp_url_t.scheme`. */
  size_t scheme_len;
  /** Length of :c:member:`lwhttp_url_t.authority`. */
  size_t authority_len;
  /** Length of :c:member:`lwhttp_url_t.host`. */
  size_t host_len;
  /** Length of :c:member:`lwhttp_url_t.path`. */
  size_t path_len;
  /** Length of :c:member:`lwhttp_url_t.query`. */
  size_t query_len;
  /** Length of :c:member:`lwhttp_url_t.fragment`. */
  size_t fragment_len;
  /** Port number; valid only when :c:member:`lwhttp_url_t.has_port` is set. */
  uint16_t port;
  /** A scheme is present (the reference is absolute). */
  bool has_scheme;
  /** An authority (``//...``) is present. */
  bool has_authority;
  /** The authority contains a non-empty port (``host:`` with nothing after the colon does not count). */
  bool has_port;
  /** A query is present, possibly empty. */
  bool has_query;
  /** A fragment is present, possibly empty. */
  bool has_fragment;
  /** The host was written as a bracketed IP literal (``[...]``). */
  bool ipv6;
} lwhttp_url_t;

/**
 * lwIP-CE socket handle, defined in ``lwip.h``. Only used by :c:func:`lwhttp_client_detach`.
 */
struct lwip_socket;

/**
 * Opaque HTTP client.
 *
 * A client runs one request at a time and keeps the connection open between requests when possible.
 * Several clients can run concurrently. Create with :c:func:`lwhttp_client_create`, free with
 * :c:func:`lwhttp_client_destroy`.
 */
typedef struct lwhttp_client lwhttp_client_t;

/**
 * Supplies streamed request body data.
 *
 * Called from :c:func:`lwhttp_client_request` or from socket event processing whenever the client can
 * send more. The reader must produce data immediately; it cannot signal "not ready".
 *
 * With a known :c:member:`lwhttp_request_t.body_length` the reader is asked for at most the bytes still
 * owed and must return between 1 and ``size``; returning 0 early or more than ``size`` aborts the request
 * with :c:enumerator:`LWHTTP_ERR_ABORTED`. With :c:macro:`LWHTTP_LENGTH_UNKNOWN` each return value
 * becomes one chunk, and returning 0 ends the body.
 *
 * :param arg: :c:member:`lwhttp_request_t.body_arg`.
 * :param buf: Where to write the data.
 * :param size: Maximum number of bytes to write (at most 512).
 * :return: Number of bytes written to ``buf``.
 */
typedef size_t (*lwhttp_body_reader_t)(void *arg, uint8_t *buf, size_t size);

/**
 * Client configuration, copied by :c:func:`lwhttp_client_create`.
 *
 * Fill it with :c:func:`lwhttp_client_config_default` first, then change what you need.
 */
typedef struct {
  /**
   * Inactivity timeout in milliseconds; it bounds silence, not total request time. While connecting it is
   * lwIP-CE's socket timeout (:c:enumerator:`LWHTTP_ERR_CONNECT`), which any frame on the interface extends.
   * Once connected the client also tracks its own connection: received data or acknowledged sent data restarts
   * it, checked on lwIP-CE's poll tick of about 2 seconds. Expiry after connecting, by either check, ends the
   * request with :c:enumerator:`LWHTTP_ERR_TIMEOUT`. 0 disables it. Default 30000.
   */
  uint32_t timeout_ms;
  /**
   * Size in bytes of the buffer that holds the response header section, the line being parsed and any
   * trailers. Each stored field takes name + value + 2 bytes and must leave 128 bytes free for chunk-size
   * and trailer lines; responses that do not fit fail with :c:enumerator:`LWHTTP_ERR_HEADERS_TOO_LARGE`.
   * Values below 256 are raised to 256. Default 4096.
   */
  size_t header_size;
  /** Maximum number of redirects followed per request; 0 delivers 3xx responses unchanged. Default 5. */
  uint8_t max_redirects;
  /**
   * Reuse connections between requests to the same scheme, host and port. When false the client sends
   * ``Connection: close`` and opens a new connection for every request. Default true.
   */
  bool keep_alive;
  /**
   * ``User-Agent`` sent when the request has none, or ``NULL`` to send none. The pointer is stored, not
   * copied, so the string must outlive the client. Default ``"LWHTTP/" LWHTTP_VERSION_STRING``.
   */
  const char *user_agent;
  /**
   * ``NULL``-terminated list of response header and trailer names to store (case-insensitive), or ``NULL``
   * to store every field. ``Location`` is always stored for redirects. Fields not listed take no space in
   * the header store, and lines too long for the store are skipped instead of failing the request, so a
   * small :c:member:`lwhttp_client_config_t.header_size` is enough. The array and strings are used by
   * pointer and must outlive the client. Default ``NULL``.
   */
  const char *const *keep_headers;
} lwhttp_client_config_t;

/**
 * A request for :c:func:`lwhttp_client_request`.
 *
 * Zero-initialise it and set at least :c:member:`lwhttp_request_t.url`. The struct itself and the URL
 * string are copied; every other pointer (method, target, headers and their strings, body, body_arg) is
 * used until ``on_complete`` and must stay valid until then.
 *
 * The client adds ``Host``, ``User-Agent`` (unless supplied here or disabled), ``Content-Length`` or
 * ``Transfer-Encoding: chunked``, ``Accept-Encoding: identity`` (unless supplied here),
 * ``Expect: 100-continue`` and ``Connection: close`` as needed. ``Content-Length: 0`` is sent for
 * ``POST``, ``PUT`` and ``PATCH`` without a body.
 */
typedef struct {
  /** Method token, case-sensitive; ``NULL`` means ``"GET"``. */
  const char *method;
  /**
   * Absolute ``http://`` or ``https://`` URL. Userinfo and fragment are ignored; ``localhost``,
   * ``127.x.x.x`` and ``::1`` use lwIP-CE's loopback interface.
   */
  const char *url;
  /**
   * Request-target sent instead of the URL's path and query (for example ``"*"`` or an absolute URL for a
   * proxy), or ``NULL``. Must not contain spaces or control characters. Only the first request uses it;
   * redirect hops send the path and query of the redirect URL. For ``CONNECT`` without a target,
   * ``host:port`` from the URL is sent.
   */
  const char *target;
  /**
   * Extra header fields, sent after the client's own. A ``Host``, ``User-Agent`` or ``Accept-Encoding``
   * field here replaces the client's. A supplied ``Host`` is dropped, and the client's own sent instead,
   * once a redirect leaves the original scheme, host and port. ``Content-Length`` and ``Transfer-Encoding`` are
   * rejected with :c:enumerator:`LWHTTP_ERR_ARG`; the client sets them from the body fields. Supplying
   * ``Accept-Encoding`` disables the content-coding check, so compressed bodies are delivered undecoded.
   */
  const lwhttp_header_t *headers;
  /** Number of entries in :c:member:`lwhttp_request_t.headers`. */
  size_t header_count;
  /**
   * In-memory body of :c:member:`lwhttp_request_t.body_length` bytes, or ``NULL``. Ignored when
   * :c:member:`lwhttp_request_t.body_reader` is set. Kept for redirects that preserve the body and for
   * automatic retries.
   */
  const uint8_t *body;
  /**
   * Body length in bytes, sent as ``Content-Length``. With
   * :c:member:`lwhttp_request_t.body_reader`, :c:macro:`LWHTTP_LENGTH_UNKNOWN` selects a chunked upload.
   * Ignored when neither ``body`` nor ``body_reader`` is set.
   */
  uint32_t body_length;
  /**
   * Streams the body instead of :c:member:`lwhttp_request_t.body`, or ``NULL``. A streamed body cannot be
   * replayed, so such requests are never retried, and a 307/308 redirect (or a 301/302 for a method other
   * than ``POST``) is delivered to the application instead of followed.
   */
  lwhttp_body_reader_t body_reader;
  /** Passed to :c:member:`lwhttp_request_t.body_reader`. */
  void *body_arg;
  /**
   * Send ``Expect: 100-continue`` and hold the body until the server answers ``100 Continue``. If the
   * server stays silent the body is sent on the next lwIP-CE poll tick; if a final response arrives first
   * the body is not sent and the connection is not reused. Ignored when the body is empty.
   */
  bool expect_continue;
} lwhttp_request_t;

/**
 * Counters kept by a client, read with :c:func:`lwhttp_client_stats`.
 *
 * All counters accumulate over the client's lifetime except :c:member:`lwhttp_client_stats_t.unsent` and
 * :c:member:`lwhttp_client_stats_t.final_state`, which describe the most recently finished request.
 */
typedef struct {
  /** Connections opened (TCP or TLS), including those for redirect hops and retries. */
  uint16_t connections;
  /** Requests, including redirect hops, started on an already open kept-alive connection. */
  uint16_t reused;
  /**
   * Automatic retries. A request is retried once on a new connection when a reused connection fails
   * before any response byte arrives, if its method is idempotent (``GET``, ``HEAD``, ``PUT``,
   * ``DELETE``, ``OPTIONS``, ``TRACE``) and it has no body reader.
   */
  uint16_t retries;
  /** Redirects followed. */
  uint16_t redirects;
  /** Times lwIP-CE refused even a small write for lack of memory; sending resumes on the next event. */
  uint16_t write_stalls;
  /** Bytes handed to lwIP-CE (request heads and bodies, before TLS). */
  uint32_t bytes_sent;
  /** Bytes read from lwIP-CE (response heads and bodies, after TLS). */
  uint32_t bytes_received;
  /**
   * Nonzero when the last request finished before its whole request was written: request-head and body
   * bytes not yet sent, plus 1 if a chunked upload had not sent its last chunk.
   */
  uint32_t unsent;
  /**
   * Client state when the last request finished: 1 while connecting, 2 while exchanging data
   * (0 if no request has finished yet).
   */
  uint8_t final_state;
} lwhttp_client_stats_t;

/**
 * Application callbacks for one request.
 *
 * The table is used by pointer and must stay valid until ``on_complete``. Every member may be ``NULL``, and
 * the table pointer itself may be ``NULL``. Callbacks run from inside :c:func:`lwhttp_service_events`
 * (or :c:func:`lwhttp_client_cancel`), never concurrently. Inside any of them the application may call
 * :c:func:`lwhttp_client_cancel` or :c:func:`lwhttp_client_destroy`; destruction is deferred until the
 * callback returns and no further callbacks are made.
 */
typedef struct {
  /**
   * Called once per request when the final response head has arrived: after redirects, not for interim
   * 1xx responses (a 101 is final). Status, version and headers are available through the client
   * accessors.
   *
   * :param client: The client.
   * :param arg: The ``arg`` given to :c:func:`lwhttp_client_request`.
   * :return: true to continue, false to abort with :c:enumerator:`LWHTTP_ERR_ABORTED`.
   */
  bool (*on_response)(lwhttp_client_t *client, void *arg);
  /**
   * Called with response body data as it arrives, after ``on_response``. Bodies of followed redirects are
   * discarded and not reported.
   *
   * :param client: The client.
   * :param data: Body bytes; valid only during the call.
   * :param len: Number of bytes in ``data``.
   * :param arg: The ``arg`` given to :c:func:`lwhttp_client_request`.
   * :return: true to continue, false to abort with :c:enumerator:`LWHTTP_ERR_ABORTED`.
   */
  bool (*on_body)(lwhttp_client_t *client, const uint8_t *data, size_t len, void *arg);
  /**
   * Called exactly once for every request that :c:func:`lwhttp_client_request` accepted, unless the client
   * is destroyed first. It may start a new request on the same client or destroy it.
   *
   * :param client: The client.
   * :param result: :c:enumerator:`LWHTTP_OK` when a complete response was received (any status code), or
   *     the error that ended the request.
   * :param arg: The ``arg`` given to :c:func:`lwhttp_client_request`.
   */
  void (*on_complete)(lwhttp_client_t *client, lwhttp_error_t result, void *arg);
} lwhttp_client_handlers_t;

/**
 * Process lwIP-CE events and run client callbacks. Call this in the main loop instead of
 * ``lwip_service_events()``.
 *
 * It calls ``lwip_service_events()`` and then destroys sockets the clients have closed since the last call
 * (sockets cannot be destroyed safely from inside their own callbacks). Keep calling it for a while after
 * the last request or :c:func:`lwhttp_client_destroy` so retired sockets are freed.
 *
 * Do not call it from inside an LWHTTP callback.
 */
void lwhttp_service_events(void);
/**
 * Version of the compiled library.
 *
 * :return: Static string equal to the library's :c:macro:`LWHTTP_VERSION_STRING`.
 */
const char *lwhttp_version_string(void);
/**
 * Short English description of a result code.
 *
 * :param error: A result code.
 * :return: Static lower-case string such as ``"tls failure"``, or ``"unknown error"`` for an unknown value.
 */
const char *lwhttp_error_string(lwhttp_error_t error);
/**
 * Check whether a string is an HTTP token (RFC 9110 ``tchar``), as required for method and field names.
 *
 * :param s: NUL-terminated string, may be ``NULL``.
 * :return: true if ``s`` is non-empty and consists only of token characters; false for ``NULL`` or ``""``.
 */
bool lwhttp_is_token(const char *s);
/**
 * Check whether a comma-separated header value contains a token, ignoring case.
 *
 * Each list element is compared up to its first ``;`` or whitespace, so parameters are ignored; quoted
 * strings are skipped. For example ``"keep-alive, Upgrade"`` contains ``"upgrade"``, but ``"closed"``
 * does not contain ``"close"``.
 *
 * :param value: Header value, may be ``NULL``.
 * :param token: Token to look for, may be ``NULL``.
 * :return: true if found; false if not or if either argument is ``NULL``.
 */
bool lwhttp_header_has_token(const char *value, const char *token);

/**
 * Initialise a parser for a new stream of responses.
 *
 * Clears the request mode (see :c:func:`lwhttp_parser_set_request`) and resets all message state.
 *
 * :param parser: Parser to initialise.
 * :param line: Buffer for one protocol line at a time; must stay valid while the parser is used. Callback
 *     strings point into it.
 * :param line_size: Size of ``line``. Each status, header, trailer or chunk-size line (including its CR,
 *     and with folded continuation lines joined) must be shorter than this, or parsing fails with
 *     :c:enumerator:`LWHTTP_ERR_HEADERS_TOO_LARGE` (:c:enumerator:`LWHTTP_ERR_TOO_LARGE` for chunk-size
 *     lines).
 * :param callbacks: Callback table, or ``NULL``; must stay valid while the parser is used.
 * :param user: Stored in :c:member:`lwhttp_parser.user` for the callbacks.
 */
void lwhttp_parser_init(lwhttp_parser_t *parser, char *line, size_t line_size,
                        const lwhttp_parser_callbacks_t *callbacks, void *user);
/**
 * Prepare the parser for a new message, clearing status, flags, version and any error.
 *
 * Keeps the callbacks, user pointer, line buffer and request mode. Not needed between consecutive messages
 * on one stream: :c:func:`lwhttp_parser_execute` resets automatically after a complete message.
 *
 * :param parser: The parser.
 */
void lwhttp_parser_reset(lwhttp_parser_t *parser);
/**
 * Tell the parser which request method the responses answer, which affects body framing.
 *
 * ``"HEAD"`` makes every response bodiless; ``"CONNECT"`` makes a 2xx response an upgrade
 * (:c:macro:`LWHTTP_MSG_UPGRADE`). Any other method, or ``NULL``, selects normal framing. The comparison
 * is case-sensitive. The mode survives :c:func:`lwhttp_parser_reset`; call this before the response
 * arrives.
 *
 * :param parser: The parser.
 * :param method: Request method, or ``NULL``.
 */
void lwhttp_parser_set_request(lwhttp_parser_t *parser, const char *method);
/**
 * Feed response bytes to the parser, invoking callbacks as elements are recognised.
 *
 * Input can be split anywhere. Parsing stops after each complete message, so with pipelined or interim
 * (1xx) responses the caller must call again with the remaining bytes; that call starts the next message.
 * Empty lines before a status line are skipped. After an upgrade message the parser consumes nothing more,
 * and after an error every call returns 0.
 *
 * :param parser: The parser.
 * :param data: Input bytes.
 * :param len: Number of input bytes.
 * :return: Bytes consumed. Less than ``len`` when a message completed or an error occurred; check
 *     :c:func:`lwhttp_parser_is_complete` and :c:func:`lwhttp_parser_error`.
 */
size_t lwhttp_parser_execute(lwhttp_parser_t *parser, const uint8_t *data, size_t len);
/**
 * Tell the parser the stream has ended.
 *
 * Completes a close-delimited body (calling ``on_message_complete``). A message that is otherwise
 * unfinished puts the parser in the error state.
 *
 * :param parser: The parser.
 * :return: :c:enumerator:`LWHTTP_OK` if no message was in progress or the message is now complete,
 *     :c:enumerator:`LWHTTP_ERR_CLOSED` for a truncated message, :c:enumerator:`LWHTTP_ERR_ABORTED` if
 *     ``on_message_complete`` aborted, or the earlier error if the parser had already failed.
 */
lwhttp_error_t lwhttp_parser_finish(lwhttp_parser_t *parser);
/**
 * Check whether the last :c:func:`lwhttp_parser_execute` or :c:func:`lwhttp_parser_finish` call ended a
 * message. Stays true until the next message starts.
 *
 * :param parser: The parser.
 * :return: true if a message is complete.
 */
bool lwhttp_parser_is_complete(const lwhttp_parser_t *parser);
/**
 * Check :c:macro:`LWHTTP_MSG_KEEP_ALIVE` for the current message. Meaningful once the header section has
 * been parsed.
 *
 * :param parser: The parser.
 * :return: true if the connection may be reused after this message.
 */
bool lwhttp_parser_keep_alive(const lwhttp_parser_t *parser);
/**
 * Get the error that stopped the parser.
 *
 * :param parser: The parser.
 * :return: :c:enumerator:`LWHTTP_OK` if the parser has not failed, otherwise
 *     :c:enumerator:`LWHTTP_ERR_PROTOCOL`, :c:enumerator:`LWHTTP_ERR_VERSION`,
 *     :c:enumerator:`LWHTTP_ERR_TOO_LARGE`, :c:enumerator:`LWHTTP_ERR_HEADERS_TOO_LARGE`,
 *     :c:enumerator:`LWHTTP_ERR_ABORTED` or :c:enumerator:`LWHTTP_ERR_CLOSED`.
 */
lwhttp_error_t lwhttp_parser_error(const lwhttp_parser_t *parser);

/**
 * Initialise an empty header store over a caller-supplied buffer.
 *
 * :param headers: Store to initialise.
 * :param data: Storage buffer; must stay valid while the store is used.
 * :param size: Size of ``data`` in bytes.
 */
void lwhttp_headers_init(lwhttp_headers_t *headers, char *data, size_t size);
/**
 * Remove all fields. The buffer is kept and pointers previously returned become stale.
 *
 * :param headers: The store.
 */
void lwhttp_headers_clear(lwhttp_headers_t *headers);
/**
 * Append a copy of a field. Names and values are not validated, and existing fields with the same name are
 * kept.
 *
 * :param headers: The store.
 * :param name: NUL-terminated field name.
 * :param value: NUL-terminated field value.
 * :return: :c:enumerator:`LWHTTP_OK`, or :c:enumerator:`LWHTTP_ERR_HEADERS_TOO_LARGE` if it does not fit
 *     (the store is unchanged).
 */
lwhttp_error_t lwhttp_headers_add(lwhttp_headers_t *headers, const char *name, const char *value);
/**
 * Find the first field with a given name, ignoring case.
 *
 * :param headers: The store, may be ``NULL``.
 * :param name: Field name, may be ``NULL``.
 * :return: NUL-terminated value inside the store, or ``NULL`` if absent or an argument is ``NULL``. Valid
 *     until the store is cleared or its buffer reused.
 */
const char *lwhttp_headers_get(const lwhttp_headers_t *headers, const char *name);
/**
 * Get a field by position, in insertion order. Use it to read repeated fields such as ``Set-Cookie``.
 *
 * :param headers: The store, may be ``NULL``.
 * :param index: Zero-based position.
 * :param name: Receives the NUL-terminated name, or ``NULL`` if not wanted.
 * :param value: Receives the NUL-terminated value, or ``NULL`` if not wanted.
 * :return: true on success; false if ``index`` is out of range or ``headers`` is ``NULL``.
 */
bool lwhttp_headers_at(const lwhttp_headers_t *headers, size_t index, const char **name, const char **value);
/**
 * Number of stored fields.
 *
 * :param headers: The store, may be ``NULL``.
 * :return: Field count, 0 for ``NULL``.
 */
size_t lwhttp_headers_count(const lwhttp_headers_t *headers);

/**
 * Initialise an empty output buffer.
 *
 * :param buffer: Buffer to initialise.
 * :param data: Output storage, or ``NULL`` with ``size`` 0 to only measure.
 * :param size: Capacity of ``data`` in bytes.
 */
void lwhttp_buffer_init(lwhttp_buffer_t *buffer, char *data, size_t size);
/**
 * Append bytes. If they do not fit nothing is written, but the length still grows.
 *
 * :param buffer: The buffer.
 * :param data: Bytes to append.
 * :param len: Number of bytes.
 */
void lwhttp_buffer_append(lwhttp_buffer_t *buffer, const void *data, size_t len);
/**
 * Append a NUL-terminated string without its terminator.
 *
 * :param buffer: The buffer.
 * :param s: String to append; must not be ``NULL``.
 */
void lwhttp_buffer_append_str(lwhttp_buffer_t *buffer, const char *s);
/**
 * Append an unsigned decimal number without leading zeros.
 *
 * :param buffer: The buffer.
 * :param value: Number to append.
 */
void lwhttp_buffer_append_u32(lwhttp_buffer_t *buffer, uint32_t value);
/**
 * Check whether everything appended so far fit.
 *
 * :param buffer: The buffer.
 * :return: true if :c:member:`lwhttp_buffer_t.len` does not exceed :c:member:`lwhttp_buffer_t.size`.
 */
bool lwhttp_buffer_ok(const lwhttp_buffer_t *buffer);
/**
 * Append a validated ``Name: value`` line followed by CRLF.
 *
 * Validation protects against header injection; capacity is not checked here, use
 * :c:func:`lwhttp_buffer_ok`.
 *
 * :param buffer: The buffer.
 * :param name: Field name; must be a token (see :c:func:`lwhttp_is_token`).
 * :param value: Field value; must not contain control characters other than horizontal tab.
 * :return: true if the field was appended; false, with nothing appended, if ``name`` or ``value`` is
 *     invalid or ``NULL``.
 */
bool lwhttp_write_header(lwhttp_buffer_t *buffer, const char *name, const char *value);
/**
 * Append the empty line (CRLF) that ends a header section.
 *
 * :param buffer: The buffer.
 */
void lwhttp_write_end(lwhttp_buffer_t *buffer);

/**
 * Split a URI reference (absolute URL or relative reference) into its components, following RFC 3986.
 *
 * Nothing is copied or decoded; the result points into ``s``, which must outlive it. A reference without
 * ``//`` has no authority, so ``"example.com/"`` is parsed as a path.
 *
 * :param s: NUL-terminated URL.
 * :param url: Receives the components.
 * :return: :c:enumerator:`LWHTTP_OK`, :c:enumerator:`LWHTTP_ERR_ARG` if an argument is ``NULL``, or
 *     :c:enumerator:`LWHTTP_ERR_URL` if ``s`` contains spaces or control characters, the port is not a
 *     number up to 65535, or an IP literal is malformed.
 */
lwhttp_error_t lwhttp_url_parse(const char *s, lwhttp_url_t *url);
/**
 * Resolve a reference against a base URL (RFC 3986 section 5), removing dot segments.
 *
 * The fragment of the result comes from ``reference`` only. This is how the client resolves redirect
 * ``Location`` values.
 *
 * :param base: Absolute base URL (must have a scheme).
 * :param reference: Absolute URL or relative reference.
 * :param out: Receives the NUL-terminated result.
 * :param size: Size of ``out`` including the terminator.
 * :return: :c:enumerator:`LWHTTP_OK`, :c:enumerator:`LWHTTP_ERR_ARG` if a pointer is ``NULL`` or ``size``
 *     is 0, :c:enumerator:`LWHTTP_ERR_URL` if either input fails :c:func:`lwhttp_url_parse` or ``base``
 *     has no scheme, or :c:enumerator:`LWHTTP_ERR_TOO_LARGE` if the result does not fit ``out``.
 */
lwhttp_error_t lwhttp_url_resolve(const char *base, const char *reference, char *out, size_t size);

/**
 * Fill a configuration with the defaults: 30000 ms timeout, 4096-byte header buffer, 5 redirects,
 * keep-alive on, ``User-Agent: LWHTTP/<version>``, every response header kept.
 *
 * :param config: Configuration to fill.
 */
void lwhttp_client_config_default(lwhttp_client_config_t *config);
/**
 * Create a client.
 *
 * Allocates one block from the lwIP-CE heap of ``header_size`` + 512 bytes + the client struct. Each
 * request also makes small allocations (URL copy, host name, request head) while it runs. No connection is
 * opened until the first request.
 *
 * :param config: Configuration to copy, or ``NULL`` for the defaults.
 * :return: The new client, or ``NULL`` if memory is exhausted.
 */
lwhttp_client_t *lwhttp_client_create(const lwhttp_client_config_t *config);
/**
 * Destroy a client, closing its connection.
 *
 * Safe to call at any time. A request in progress is dropped without calling ``on_complete``. Called from
 * inside one of the client's callbacks, the destruction is deferred until the callback returns and no
 * further callbacks are made. The socket itself is freed by a later :c:func:`lwhttp_service_events`.
 *
 * :param client: The client, or ``NULL`` (no effect).
 */
void lwhttp_client_destroy(lwhttp_client_t *client);
/**
 * Start a request. Returns immediately; the request progresses inside :c:func:`lwhttp_service_events`.
 *
 * Requests that cannot start fail synchronously and never call ``on_complete``. An accepted request calls
 * ``on_complete`` exactly once (unless the client is destroyed first). The client reuses its kept-alive
 * connection when the scheme, host and port match, otherwise it connects; ``https`` uses lwIP-CE TLS 1.3
 * with SNI and certificate verification against lwIP-CE's trust store.
 *
 * Redirects 301, 302, 303, 307 and 308 with a ``Location`` are followed up to
 * :c:member:`lwhttp_client_config_t.max_redirects`. 303 becomes ``GET`` (unless the method is ``HEAD``),
 * as do 301 and 302 for ``POST``; the others keep the method and body. ``Authorization``,
 * ``Proxy-Authorization``, ``Cookie`` and a supplied ``Host`` are no longer sent once a redirect leaves
 * the original scheme, host and port.
 *
 * Responses with a body that uses a non-identity ``Content-Encoding`` (unless the request supplied
 * ``Accept-Encoding``) or a transfer coding other than ``chunked`` fail with
 * :c:enumerator:`LWHTTP_ERR_UNSUPPORTED` before ``on_response``. A ``101`` response or a 2xx answer to
 * ``CONNECT`` completes the request successfully and keeps the socket for
 * :c:func:`lwhttp_client_detach`.
 *
 * :param client: The client; must not have a request in progress. May be called from ``on_complete``.
 * :param request: The request; copied, but see :c:type:`lwhttp_request_t` for what must stay valid.
 * :param handlers: Callbacks, or ``NULL``; must stay valid until ``on_complete``.
 * :param arg: Passed to every handler.
 * :return: :c:enumerator:`LWHTTP_OK` if the request was accepted. Otherwise, without calling
 *     ``on_complete``: :c:enumerator:`LWHTTP_ERR_ARG` (``NULL`` client, request or URL; invalid method,
 *     target, header or ``User-Agent``; user ``Content-Length``/``Transfer-Encoding``),
 *     :c:enumerator:`LWHTTP_ERR_STATE` (a request is in progress or the client is being destroyed),
 *     :c:enumerator:`LWHTTP_ERR_URL` (unparsable URL, or no scheme or host),
 *     :c:enumerator:`LWHTTP_ERR_UNSUPPORTED` (scheme not ``http``/``https``),
 *     :c:enumerator:`LWHTTP_ERR_TOO_LARGE` (request head longer than ``SIZE_MAX``), :c:enumerator:`LWHTTP_ERR_MEM`,
 *     or :c:enumerator:`LWHTTP_ERR_CONNECT` (lwIP-CE refused to start connecting). Failures detected after
 *     the URL was copied (URL, scheme, header and connection errors) also mark the client done with that
 *     result for :c:func:`lwhttp_client_result`.
 */
lwhttp_error_t lwhttp_client_request(lwhttp_client_t *client, const lwhttp_request_t *request,
                                     const lwhttp_client_handlers_t *handlers, void *arg);
/**
 * Block until the current request finishes, calling :c:func:`lwhttp_service_events` in a loop. Handlers
 * still run as usual. Must not be called from a client callback.
 *
 * :param client: The client.
 * :return: The request's result (see :c:func:`lwhttp_client_result`); immediately the last result if no
 *     request is in progress; :c:enumerator:`LWHTTP_ERR_ARG` if ``client`` is ``NULL``.
 */
lwhttp_error_t lwhttp_client_wait(lwhttp_client_t *client);
/**
 * Start a request and wait for it: :c:func:`lwhttp_client_request` followed by
 * :c:func:`lwhttp_client_wait`. Must not be called from a client callback.
 *
 * :param client: The client.
 * :param request: The request.
 * :param handlers: Callbacks, or ``NULL`` when the status and headers are enough.
 * :param arg: Passed to every handler.
 * :return: The synchronous error from :c:func:`lwhttp_client_request`, or the request's final result.
 */
lwhttp_error_t lwhttp_client_request_sync(lwhttp_client_t *client, const lwhttp_request_t *request,
                                          const lwhttp_client_handlers_t *handlers, void *arg);
/**
 * Abort the request in progress and close its connection.
 *
 * ``on_complete`` is called with :c:enumerator:`LWHTTP_ERR_ABORTED` before this function returns. Safe to
 * call from callbacks. No effect if no request is in progress.
 *
 * :param client: The client, or ``NULL``.
 */
void lwhttp_client_cancel(lwhttp_client_t *client);
/**
 * Close the idle kept-alive connection (or an upgraded connection that was not detached).
 *
 * No effect while a request is in progress. The next request opens a new connection.
 *
 * :param client: The client, or ``NULL``.
 */
void lwhttp_client_close(lwhttp_client_t *client);
/**
 * Check whether the most recent request has finished, successfully or not.
 *
 * :param client: The client.
 * :return: true once a request has finished and until the next one starts; false before the first request,
 *     while one is in progress, or for ``NULL``.
 */
bool lwhttp_client_is_done(const lwhttp_client_t *client);
/**
 * Result of the most recent request.
 *
 * :param client: The client.
 * :return: The value passed to ``on_complete`` (or the synchronous failure after the URL was accepted);
 *     :c:enumerator:`LWHTTP_OK` before the first request or while one is in progress;
 *     :c:enumerator:`LWHTTP_ERR_ARG` for ``NULL``.
 */
lwhttp_error_t lwhttp_client_result(const lwhttp_client_t *client);
/**
 * Status code of the current or most recent response (the final one after redirects).
 *
 * :param client: The client.
 * :return: Status code, or 0 if no status line has been received for this request or ``client`` is
 *     ``NULL``.
 */
uint16_t lwhttp_client_status(const lwhttp_client_t *client);
/**
 * Minor HTTP version of the current or most recent response.
 *
 * :param client: The client.
 * :return: 1 for HTTP/1.1, 0 for HTTP/1.0 or if no response was received or ``client`` is ``NULL``.
 */
uint8_t lwhttp_client_version(const lwhttp_client_t *client);
/**
 * Look up a response header, ignoring case.
 *
 * The store holds the fields of the final response and, after a chunked body, its trailers. Values remain
 * valid until the next request starts or the client is destroyed.
 *
 * :param client: The client, may be ``NULL``.
 * :param name: Field name.
 * :return: Value of the first matching field, or ``NULL``.
 */
const char *lwhttp_client_header(const lwhttp_client_t *client, const char *name);
/**
 * Access the response header store, for example to iterate with :c:func:`lwhttp_headers_at`.
 *
 * Same contents and lifetime as for :c:func:`lwhttp_client_header`. Do not modify it.
 *
 * :param client: The client.
 * :return: The store, or ``NULL`` if ``client`` is ``NULL``.
 */
const lwhttp_headers_t *lwhttp_client_headers(const lwhttp_client_t *client);
/**
 * URL of the current or most recent request; after redirects, the final URL.
 *
 * :param client: The client.
 * :return: Client-owned copy, valid until the next request or redirect; ``NULL`` before the first request
 *     or for ``NULL``.
 */
const char *lwhttp_client_url(const lwhttp_client_t *client);
/**
 * Access the client's counters.
 *
 * :param client: The client.
 * :return: Pointer into the client, updated live; ``NULL`` for ``NULL``.
 */
const lwhttp_client_stats_t *lwhttp_client_stats(const lwhttp_client_t *client);
/**
 * Take over the socket of an upgraded connection (``101`` or a 2xx answer to ``CONNECT``).
 *
 * Call after the request completed with :c:enumerator:`LWHTTP_OK`, for example from ``on_complete``. The
 * client unregisters its event handler and forgets the socket; the caller owns it from then on and must
 * register its own handler and eventually ``lwip_socket_close()`` and ``lwip_socket_destroy()`` it.
 *
 * :param client: The client.
 * :param pending: Receives a pointer to bytes the client already read past the response head (the start of
 *     the new protocol), or ``NULL`` if not wanted. The data lives in the client and is valid until its next
 *     request or destruction.
 * :param pending_len: Receives the number of pending bytes (at most 512), or ``NULL`` if not wanted.
 * :return: The socket, or ``NULL`` if the last request did not upgrade, the socket was already detached or
 *     closed, or ``client`` is ``NULL``.
 */
struct lwip_socket *lwhttp_client_detach(lwhttp_client_t *client, const uint8_t **pending, size_t *pending_len);

#ifdef __cplusplus
}
#endif

#endif
