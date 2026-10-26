#ifndef HTTPTEST_CHECK_H
#define HTTPTEST_CHECK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { T_PASS, T_FAIL, T_DETAIL, T_INFO, T_SECTION };

extern unsigned long t_passed;
extern unsigned long t_failed;

void t_emit(int kind, const char *text);
void t_section(const char *name);
void t_info(const char *text);
void t_note(const char *text);
void t_begin(const char *name);
bool t_end(void);
bool t_check(bool ok, const char *file, int line);
bool t_check_long(long got, long want, const char *file, int line);
bool t_check_str(const char *got, const char *want, const char *file, int line);
void unit_tests(void);

#define CHECK(x) t_check((x) ? true : false, __FILE__, __LINE__)
#define CHECK_INT(got, want) t_check_long((long)(got), (long)(want), __FILE__, __LINE__)
#define CHECK_STR(got, want) t_check_str((got), (want), __FILE__, __LINE__)

#endif
