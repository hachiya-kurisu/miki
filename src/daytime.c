#include <math.h>
#include <time.h>

static int daytime(const time_t *t, double lat, double lon) {
  struct tm tms;
  struct tm *tm = localtime_r(t, &tms);
  if(!tm) return 0; // outside time and space
  int day = tm->tm_yday + 1;
  double x = sin(360.0 * (day + 284) / 365.0 * M_PI / 180);
  double y = -tan(lat * M_PI / 180) * tan(23.44 * x * M_PI / 180);
  if(y < -1) y = -1;
  if(y > 1) y = 1;
  double hours = 1 / 15.0 * acos(y) * 180 / M_PI;
  tm->tm_hour = 12, tm->tm_min = 0, tm->tm_sec = 0;
  double delta_s = hours * 60 * 60;
  time_t noon_t = mktime(tm);

  double utcoffset = (double)tm->tm_gmtoff / 3600.0;
  double m = utcoffset * 15.0;
  double offset = (m - lon) / 15.0 * 3600;
  noon_t += (long)offset;

  return fabs(difftime(*t, noon_t)) <= delta_s;
}
