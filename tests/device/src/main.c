#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fileioc.h>
#include <graphx.h>
#include <ti/getcsc.h>

#include <lwhttp.h>
#include <lwip.h>

#include "check.h"
#include "net.h"

#define LOG_NAME "HTTPLOG"
#define CI_NAME "HTTPCI"
#define TOP 14
#define LINE_H 10
#define LINES 22
#define COLS 40

enum { C_BLACK, C_WHITE, C_GREEN, C_RED, C_YELLOW, C_CYAN, C_BAR, C_GRAY };

static uint8_t log_handle;
static char text[96];

static void bar(void)
{
  gfx_SetColor(C_BAR);
  gfx_FillRectangle_NoClip(0, 0, GFX_LCD_WIDTH, TOP - 2);
  gfx_SetTextBGColor(C_BAR);
  gfx_SetTextFGColor(C_WHITE);
  snprintf(text, sizeof(text), "HTTPTEST %s", LWHTTP_VERSION_STRING);
  gfx_PrintStringXY(text, 2, 2);
  snprintf(text, sizeof(text), "PASS %lu", t_passed);
  gfx_SetTextFGColor(C_GREEN);
  gfx_PrintStringXY(text, 170, 2);
  snprintf(text, sizeof(text), "FAIL %lu", t_failed);
  gfx_SetTextFGColor(t_failed ? C_RED : C_WHITE);
  gfx_PrintStringXY(text, 250, 2);
  gfx_SetTextBGColor(C_BLACK);
}

static void screen_line(const char *s, size_t len, uint8_t color)
{
  char row[COLS + 1];
  int y = TOP + (LINES - 1) * LINE_H;
  memcpy(row, s, len);
  row[len] = '\0';
  gfx_ShiftUp(LINE_H);
  gfx_SetColor(C_BLACK);
  gfx_FillRectangle_NoClip(0, y, GFX_LCD_WIDTH, LINE_H);
  gfx_SetTextFGColor(color);
  gfx_PrintStringXY(row, 0, y + 1);
}

static void screen(const char *s, uint8_t color)
{
  size_t len = strlen(s);
  bool first = true;
  do {
    size_t width = first ? COLS : COLS - 4;
    size_t n = len < width ? len : width;
    if (first) {
      screen_line(s, n, color);
    } else {
      char row[COLS + 1];
      memcpy(row, "    ", 4);
      memcpy(row + 4, s, n);
      screen_line(row, n + 4, color);
    }
    s += n;
    len -= n;
    first = false;
  } while (len);
}

static void log_line(const char *s)
{
  if (!log_handle) return;
  ti_Write(s, strlen(s), 1, log_handle);
  ti_Write("\n", 1, 1, log_handle);
}

void t_emit(int kind, const char *s)
{
  static const uint8_t colors[] = {C_GREEN, C_RED, C_YELLOW, C_GRAY, C_CYAN};
  screen(s, colors[kind]);
  if (kind != T_PASS) log_line(s);
  if (kind == T_PASS || kind == T_FAIL) bar();
}

static void palette(void)
{
  gfx_palette[C_BLACK] = gfx_RGBTo1555(0, 0, 0);
  gfx_palette[C_WHITE] = gfx_RGBTo1555(255, 255, 255);
  gfx_palette[C_GREEN] = gfx_RGBTo1555(64, 220, 96);
  gfx_palette[C_RED] = gfx_RGBTo1555(255, 72, 72);
  gfx_palette[C_YELLOW] = gfx_RGBTo1555(255, 210, 64);
  gfx_palette[C_CYAN] = gfx_RGBTo1555(96, 200, 255);
  gfx_palette[C_BAR] = gfx_RGBTo1555(40, 48, 72);
  gfx_palette[C_GRAY] = gfx_RGBTo1555(150, 150, 150);
}

static void archive_log(void)
{
  uint8_t h;
  if (log_handle) ti_Close(log_handle);
  log_handle = 0;
  h = ti_Open(LOG_NAME, "r");
  if (!h) return;
  ti_SetArchiveStatus(true, h);
  ti_Close(h);
}

static char ci_mode(void)
{
  char mode = 0;
  uint8_t h = ti_Open(CI_NAME, "r");
  if (!h) return 0;
  ti_Read(&mode, 1, 1, h);
  ti_Close(h);
  return mode;
}

int main(void)
{
  bool started = false;
  bool up = false;
  char ci = ci_mode();

  gfx_Begin();
  palette();
  gfx_FillScreen(C_BLACK);
  gfx_SetMonospaceFont(8);
  gfx_SetTextTransparentColor(255);
  gfx_SetTextBGColor(C_BLACK);
  gfx_SetClipRegion(0, TOP, GFX_LCD_WIDTH, GFX_LCD_HEIGHT);
  bar();

  ti_Delete(LOG_NAME);
  log_handle = ti_Open(LOG_NAME, "w");
  snprintf(text, sizeof(text), "HTTPTEST LWHTTP %s", lwhttp_version_string());
  t_info(text);
  t_info(ci == 'o' ? "emulator mode: network tests skipped" : "press [clear] to stop network tests");

  unit_tests();

  t_section("lwip");
  t_begin("lwip_start");
  started = CHECK(lwip_start());
  if (!started) {
    snprintf(text, sizeof(text), "  errno %u: %s", lwip_get_start_errno(), lwip_get_start_errstring());
    t_emit(T_DETAIL, text);
  }
  t_end();
  if (started) net_offline_tests();
  if (started && ci != 'o') {
    t_section("network up");
    t_begin("lwip_network_up");
    up = CHECK(lwip_network_up());
    if (!up) {
      snprintf(text, sizeof(text), "  last error %u", lwip_start_last_error());
      t_emit(T_DETAIL, text);
    }
    t_end();
    if (up) net_tests(ci != 'f');
  }

  snprintf(text, sizeof(text), "DONE pass %lu fail %lu", t_passed, t_failed);
  t_section(text);
  t_info(t_failed ? "failures saved in appvar HTTPLOG" : "all passed, log in HTTPLOG");
  t_info("press any key to exit");
  archive_log();
  if (ci) gfx_FillScreen(t_failed ? C_RED : C_GREEN);

  while (!os_GetCSC())
    if (up) lwhttp_service_events();
  if (started) lwip_stop();
  gfx_End();
  return t_failed ? 1 : 0;
}
