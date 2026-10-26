#include <stdio.h>
#include <string.h>

#include "check.h"

unsigned long t_passed;
unsigned long t_failed;

static const char *current;
static unsigned int failures;
static char text[160];
static char notes[2048];
static size_t notes_len;

static const char *base(const char *file)
{
  const char *slash = strrchr(file, '/');
  return slash ? slash + 1 : file;
}

static void fail_header(void)
{
  if (failures++) return;
  snprintf(text, sizeof(text), "FAIL %s", current ? current : "?");
  t_emit(T_FAIL, text);
}

void t_section(const char *name)
{
  snprintf(text, sizeof(text), "== %s ==", name);
  t_emit(T_SECTION, text);
}

void t_info(const char *s) { t_emit(T_INFO, s); }

void t_note(const char *s)
{
  size_t n = strlen(s) + 1;
  if (!current) {
    t_emit(T_INFO, s);
    return;
  }
  if (n > sizeof(notes) - notes_len) return;
  memcpy(notes + notes_len, s, n);
  notes_len += n;
}

static void flush_notes(void)
{
  size_t off = 0;
  while (off < notes_len) {
    t_emit(T_INFO, notes + off);
    off += strlen(notes + off) + 1;
  }
}

void t_begin(const char *name)
{
  current = name;
  failures = 0;
  notes_len = 0;
}

bool t_end(void)
{
  bool ok = !failures;
  if (ok) {
    t_passed++;
    snprintf(text, sizeof(text), "PASS %s", current ? current : "?");
    t_emit(T_PASS, text);
  } else {
    t_failed++;
    flush_notes();
  }
  notes_len = 0;
  current = NULL;
  return ok;
}

bool t_check(bool ok, const char *file, int line)
{
  if (ok) return true;
  fail_header();
  snprintf(text, sizeof(text), "  %s:%d false", base(file), line);
  t_emit(T_DETAIL, text);
  return false;
}

bool t_check_long(long got, long want, const char *file, int line)
{
  if (got == want) return true;
  fail_header();
  snprintf(text, sizeof(text), "  %s:%d got %ld want %ld", base(file), line, got, want);
  t_emit(T_DETAIL, text);
  return false;
}

bool t_check_str(const char *got, const char *want, const char *file, int line)
{
  if (got && want && !strcmp(got, want)) return true;
  if (!got && !want) return true;
  fail_header();
  snprintf(text, sizeof(text), "  %s:%d got \"%.48s\" want \"%.48s\"", base(file), line, got ? got : "(null)",
           want ? want : "(null)");
  t_emit(T_DETAIL, text);
  return false;
}
