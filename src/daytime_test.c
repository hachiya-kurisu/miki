#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "daytime.c"

void settz(const char *tz) {
  setenv("TZ", tz, 1);
  tzset();
}

void test(const char *id, const char *ts, double lat, double lon, int want) {
  struct tm tm = {0};
  strptime(ts, "%Y-%m-%d %H:%M", &tm);
  tm.tm_isdst = -1;
  time_t t = mktime(&tm);
  int day = daytime(&t, lat, lon);
  printf("%s %s, %s %s\n", day == want ? "🙆️" : "🙅", id, ts, day ? "☀️" : "🌙");
}

int main(void) {
  settz("Asia/Tokyo");
  test("tokyo", "2025-06-21 04:20", 35.68, 139.77, 0);
  test("tokyo", "2025-06-21 04:30", 35.68, 139.77, 1);
  test("tokyo", "2025-06-21 12:00", 35.68, 139.77, 1);
  test("tokyo", "2025-06-21 18:53", 35.68, 139.77, 1);
  test("tokyo", "2025-06-21 19:10", 35.68, 139.77, 0);
  test("tokyo", "2025-12-21 06:45", 35.68, 139.77, 0);
  test("tokyo", "2025-12-21 12:00", 35.68, 139.77, 1);
  test("tokyo", "2025-12-21 16:45", 35.68, 139.77, 0);

  settz("Europe/Oslo");
  test("ålesund", "2025-06-21 03:35", 62.47, 6.15, 0);
  test("ålesund", "2025-06-21 12:00", 62.47, 6.15, 1);
  test("ålesund", "2025-06-21 23:00", 62.47, 6.15, 1);
  test("ålesund", "2025-06-21 23:59", 62.47, 6.15, 0);
  test("ålesund", "2025-12-21 09:00", 62.47, 6.15, 0);
  test("ålesund", "2025-12-21 12:00", 62.47, 6.15, 1);
  test("ålesund", "2025-12-21 15:00", 62.47, 6.15, 0);

  test("north pole", "2025-06-21 12:00", 90.0, 0.0, 1);
  test("north pole", "2025-12-21 12:00", 90.0, 0.0, 0);
  test("south pole", "2025-06-21 12:00", -90.0, 45.0, 0);
  test("south pole", "2025-12-21 00:00", -90.0, 45.0, 1);

  return 0;
}
