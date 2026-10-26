#include <stdio.h>

#include "../device/src/check.h"

void t_emit(int kind, const char *text)
{
  if (kind == T_PASS) return;
  printf("%s\n", text);
}

int main(void)
{
  unit_tests();
  printf("passed %lu, failed %lu\n", t_passed, t_failed);
  return t_failed ? 1 : 0;
}
