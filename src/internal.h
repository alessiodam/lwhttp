#ifndef LWHTTP_INTERNAL_H
#define LWHTTP_INTERNAL_H

#include "../lwhttp.h"

#define LWHTTP_NOINLINE __attribute__((noinline))
#define LWHTTP_IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define LWHTTP_IS_ALPHA(c) ((((c) | 0x20) >= 'a') && (((c) | 0x20) <= 'z'))
#define LWHTTP_IS_OWS(c) ((c) == ' ' || (c) == '\t')
#define LWHTTP_LOWER(c) ((char)((c) >= 'A' && (c) <= 'Z' ? (c) | 0x20 : (c)))
#define LWHTTP_TCHAR(c) ((uint8_t)(c) < 0x80 && (lwhttp__tchar_map[(uint8_t)(c) >> 3] >> ((c) & 7)) & 1)

extern const uint8_t lwhttp__tchar_map[16];

bool lwhttp__ieq(const char *a, const char *b);
bool lwhttp__casen(const char *a, const char *b, size_t len);
bool lwhttp__ieqn(const char *a, size_t len, const char *b);
bool lwhttp__list_next(const char **cursor, const char **token, size_t *len);
bool lwhttp__value_ok(const char *value, size_t len);
lwhttp_error_t lwhttp__headers_put(lwhttp_headers_t *headers, const char *name, size_t name_len, const char *value,
                                   size_t value_len);
bool lwhttp__target_ok(const char *s, size_t len);

#endif
