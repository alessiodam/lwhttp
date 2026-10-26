#ifndef LWHTTP_H
#define LWHTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LWHTTP_VERSION_MAJOR 1
#define LWHTTP_VERSION_MINOR 0
#define LWHTTP_VERSION_PATCH 0
#define LWHTTP_VERSION_STRING "1.0.0"

#define LWHTTP_LENGTH_UNKNOWN 0xFFFFFFFFul
#define LWHTTP_PARSER_SKIP_BODY 1
#define LWHTTP_DATE_SIZE 30

#define LWHTTP_MSG_CHUNKED 0x0001u
#define LWHTTP_MSG_CONTENT_LENGTH 0x0002u
#define LWHTTP_MSG_KEEP_ALIVE 0x0004u
#define LWHTTP_MSG_UPGRADE 0x0008u
#define LWHTTP_MSG_CLOSE_DELIMITED 0x0010u
#define LWHTTP_MSG_TRANSFER_CODED 0x0020u
#define LWHTTP_MSG_NO_BODY 0x0040u

typedef enum {
  LWHTTP_OK = 0,
  LWHTTP_ERR_ARG,
  LWHTTP_ERR_MEM,
  LWHTTP_ERR_STATE,
  LWHTTP_ERR_URL,
  LWHTTP_ERR_DNS,
  LWHTTP_ERR_CONNECT,
  LWHTTP_ERR_TLS,
  LWHTTP_ERR_SEND,
  LWHTTP_ERR_CLOSED,
  LWHTTP_ERR_PROTOCOL,
  LWHTTP_ERR_VERSION,
  LWHTTP_ERR_UNSUPPORTED,
  LWHTTP_ERR_TOO_LARGE,
  LWHTTP_ERR_HEADERS_TOO_LARGE,
  LWHTTP_ERR_REDIRECT,
  LWHTTP_ERR_ABORTED
} lwhttp_error_t;

typedef struct {
  const char *name;
  const char *value;
} lwhttp_header_t;

typedef struct lwhttp_parser lwhttp_parser_t;

typedef struct {
  int (*on_status_line)(lwhttp_parser_t *parser, uint16_t status, const char *reason);
  int (*on_header)(lwhttp_parser_t *parser, const char *name, size_t name_len, const char *value, size_t value_len);
  int (*on_headers_complete)(lwhttp_parser_t *parser);
  int (*on_body)(lwhttp_parser_t *parser, const uint8_t *data, size_t len);
  int (*on_trailer)(lwhttp_parser_t *parser, const char *name, size_t name_len, const char *value, size_t value_len);
  int (*on_message_complete)(lwhttp_parser_t *parser);
} lwhttp_parser_callbacks_t;

struct lwhttp_parser {
  const lwhttp_parser_callbacks_t *callbacks;
  void *user;
  char *line;
  size_t line_size;
  size_t line_len;
  uint32_t content_length;
  uint32_t remaining;
  uint16_t status;
  uint16_t flags;
  uint16_t internal;
  uint8_t state;
  uint8_t request;
  uint8_t version_major;
  uint8_t version_minor;
  uint8_t error;
};

typedef struct {
  char *data;
  size_t size;
  size_t len;
  size_t count;
} lwhttp_headers_t;

typedef struct {
  char *data;
  size_t size;
  size_t len;
} lwhttp_buffer_t;

typedef struct {
  const char *scheme;
  const char *authority;
  const char *userinfo;
  const char *host;
  const char *path;
  const char *query;
  const char *fragment;
  size_t scheme_len;
  size_t authority_len;
  size_t userinfo_len;
  size_t host_len;
  size_t path_len;
  size_t query_len;
  size_t fragment_len;
  uint16_t port;
  bool has_scheme;
  bool has_authority;
  bool has_userinfo;
  bool has_port;
  bool has_query;
  bool has_fragment;
  bool ipv6;
} lwhttp_url_t;

struct lwip_socket;

typedef struct lwhttp_client lwhttp_client_t;

typedef size_t (*lwhttp_body_reader_t)(void *arg, uint8_t *buf, size_t size);

typedef struct {
  uint32_t timeout_ms;
  size_t header_size;
  uint8_t max_redirects;
  bool keep_alive;
  const char *user_agent;
} lwhttp_client_config_t;

typedef struct {
  const char *method;
  const char *url;
  const char *target;
  const lwhttp_header_t *headers;
  size_t header_count;
  const uint8_t *body;
  uint32_t body_length;
  lwhttp_body_reader_t body_reader;
  void *body_arg;
  bool expect_continue;
} lwhttp_request_t;

typedef struct {
  uint16_t connections;
  uint16_t reused;
  uint16_t retries;
  uint16_t redirects;
} lwhttp_client_stats_t;

typedef struct {
  bool (*on_response)(lwhttp_client_t *client, void *arg);
  bool (*on_body)(lwhttp_client_t *client, const uint8_t *data, size_t len, void *arg);
  void (*on_complete)(lwhttp_client_t *client, lwhttp_error_t result, void *arg);
} lwhttp_client_handlers_t;

void lwhttp_service_events(void);
const char *lwhttp_version_string(void);
const char *lwhttp_error_string(lwhttp_error_t error);
bool lwhttp_is_token(const char *s);
bool lwhttp_header_has_token(const char *value, const char *token);
size_t lwhttp_date_format(uint32_t timestamp, char *out);
lwhttp_error_t lwhttp_date_parse(const char *s, uint32_t *timestamp);

void lwhttp_parser_init(lwhttp_parser_t *parser, char *line, size_t line_size,
                        const lwhttp_parser_callbacks_t *callbacks, void *user);
void lwhttp_parser_reset(lwhttp_parser_t *parser);
void lwhttp_parser_set_request(lwhttp_parser_t *parser, const char *method);
size_t lwhttp_parser_execute(lwhttp_parser_t *parser, const uint8_t *data, size_t len);
lwhttp_error_t lwhttp_parser_finish(lwhttp_parser_t *parser);
bool lwhttp_parser_is_complete(const lwhttp_parser_t *parser);
bool lwhttp_parser_keep_alive(const lwhttp_parser_t *parser);
lwhttp_error_t lwhttp_parser_error(const lwhttp_parser_t *parser);

void lwhttp_headers_init(lwhttp_headers_t *headers, char *data, size_t size);
void lwhttp_headers_clear(lwhttp_headers_t *headers);
lwhttp_error_t lwhttp_headers_add(lwhttp_headers_t *headers, const char *name, const char *value);
const char *lwhttp_headers_get(const lwhttp_headers_t *headers, const char *name);
bool lwhttp_headers_at(const lwhttp_headers_t *headers, size_t index, const char **name, const char **value);
size_t lwhttp_headers_count(const lwhttp_headers_t *headers);

void lwhttp_buffer_init(lwhttp_buffer_t *buffer, char *data, size_t size);
void lwhttp_buffer_append(lwhttp_buffer_t *buffer, const void *data, size_t len);
void lwhttp_buffer_append_str(lwhttp_buffer_t *buffer, const char *s);
void lwhttp_buffer_append_u32(lwhttp_buffer_t *buffer, uint32_t value);
bool lwhttp_buffer_ok(const lwhttp_buffer_t *buffer);
bool lwhttp_write_header(lwhttp_buffer_t *buffer, const char *name, const char *value);
void lwhttp_write_end(lwhttp_buffer_t *buffer);

lwhttp_error_t lwhttp_url_parse(const char *s, lwhttp_url_t *url);
lwhttp_error_t lwhttp_url_resolve(const char *base, const char *reference, char *out, size_t size);

void lwhttp_client_config_default(lwhttp_client_config_t *config);
lwhttp_client_t *lwhttp_client_create(const lwhttp_client_config_t *config);
void lwhttp_client_destroy(lwhttp_client_t *client);
lwhttp_error_t lwhttp_client_request(lwhttp_client_t *client, const lwhttp_request_t *request,
                                     const lwhttp_client_handlers_t *handlers, void *arg);
lwhttp_error_t lwhttp_client_wait(lwhttp_client_t *client);
lwhttp_error_t lwhttp_client_request_sync(lwhttp_client_t *client, const lwhttp_request_t *request,
                                          const lwhttp_client_handlers_t *handlers, void *arg);
void lwhttp_client_cancel(lwhttp_client_t *client);
void lwhttp_client_close(lwhttp_client_t *client);
bool lwhttp_client_is_done(const lwhttp_client_t *client);
lwhttp_error_t lwhttp_client_result(const lwhttp_client_t *client);
uint16_t lwhttp_client_status(const lwhttp_client_t *client);
uint8_t lwhttp_client_version(const lwhttp_client_t *client);
const char *lwhttp_client_header(const lwhttp_client_t *client, const char *name);
const lwhttp_headers_t *lwhttp_client_headers(const lwhttp_client_t *client);
const char *lwhttp_client_url(const lwhttp_client_t *client);
const lwhttp_client_stats_t *lwhttp_client_stats(const lwhttp_client_t *client);
struct lwip_socket *lwhttp_client_detach(lwhttp_client_t *client, const uint8_t **pending, size_t *pending_len);

#ifdef __cplusplus
}
#endif

#endif
