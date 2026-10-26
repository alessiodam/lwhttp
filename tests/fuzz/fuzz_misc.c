#include <stdint.h>
#include <string.h>

#include <lwhttp.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  static char input[256];
  static char out[512];
  char date[LWHTTP_DATE_SIZE];
  uint32_t t;
  uint32_t back;
  lwhttp_buffer_t b;
  char *value;
  if (size >= sizeof(input)) return 0;
  memcpy(input, data, size);
  input[size] = '\0';
  if (lwhttp_date_parse(input, &t) == LWHTTP_OK) {
    if (lwhttp_date_format(t, date) != LWHTTP_DATE_SIZE - 1) __builtin_trap();
    if (lwhttp_date_parse(date, &back) != LWHTTP_OK || back != t) __builtin_trap();
  }
  if (size >= 4) {
    memcpy(&t, data, 4);
    lwhttp_date_format(t, date);
    if (strlen(date) != LWHTTP_DATE_SIZE - 1) __builtin_trap();
    if (t <= 4291747199ul && (lwhttp_date_parse(date, &back) != LWHTTP_OK || back != t)) __builtin_trap();
  }
  lwhttp_header_has_token(input, "close");
  lwhttp_is_token(input);
  value = strchr(input, '=');
  if (value) {
    *value++ = '\0';
    lwhttp_buffer_init(&b, out, (size_t)data[0] % sizeof(out));
    if (lwhttp_write_header(&b, input, value)) {
      if (!lwhttp_is_token(input) || strchr(value, '\r') || strchr(value, '\n')) __builtin_trap();
      if (lwhttp_buffer_ok(&b) && memcmp(out + b.len - 2, "\r\n", 2)) __builtin_trap();
    }
  }
  return 0;
}
