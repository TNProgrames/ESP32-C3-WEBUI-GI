// ds3231: ساعة زمن حقيقي DS3231 عبر I2C
// cfg: addr=0x68
// الدوال: read() -> year, month, day, hour, min, sec · set(year,month,day,hour,min,sec) · temp() -> temp_c_x100
#include "drvrt.h"

typedef struct { const drv_api_t* api; uint8_t addr; } Rtc;
static const char NAME[] = "ds3231";

static uint8_t bcd2(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t tobcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Rtc* d = (Rtc*)api->alloc(sizeof(Rtc));
  if (!d) return 1;
  d->api = api;
  d->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x68);
  uint8_t reg = 0x00, t;
  int e = api->i2c_write_read(d->addr, &reg, 1, &t, 1);   // تحقق من وجود الجهاز
  if (e) { api->release(d); return e; }
  *ctx = d;
  return 0;
}
static void do_close(void* ctx) { ((Rtc*)ctx)->api->release(ctx); }

static int m_read(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  Rtc* d = (Rtc*)ctx; uint8_t reg = 0x00, b[7];
  int e = d->api->i2c_write_read(d->addr, &reg, 1, b, 7);
  if (e) return e;
  uint8_t h = b[2];
  int hour = (h & 0x40) ? (bcd2(h & 0x1F) % 12) + ((h & 0x20) ? 12 : 0) : bcd2(h & 0x3F);   // 12 أو 24 ساعة
  drv_ret_int(&r[0], 2000 + bcd2(b[6]));
  drv_ret_int(&r[1], bcd2(b[5] & 0x1F));
  drv_ret_int(&r[2], bcd2(b[4]));
  drv_ret_int(&r[3], hour);
  drv_ret_int(&r[4], bcd2(b[1]));
  drv_ret_int(&r[5], bcd2(b[0] & 0x7F));
  *nr = 6;
  return 0;
}
static int m_set(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  Rtc* d = (Rtc*)ctx;
  if (na < 6) return DRV_E_ARGS;
  int y = a[0].v.i, mo = a[1].v.i, dd = a[2].v.i, h = a[3].v.i, mi = a[4].v.i, s = a[5].v.i;
  if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || dd < 1 || dd > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) return DRV_E_ARGS;
  uint8_t buf[8] = { 0x00, tobcd(s), tobcd(mi), tobcd(h), 1, tobcd(dd), tobcd(mo), tobcd(y - 2000) };
  return d->api->i2c_write(d->addr, buf, 8);
}
static int m_temp(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  Rtc* d = (Rtc*)ctx; uint8_t reg = 0x11, b[2];
  int e = d->api->i2c_write_read(d->addr, &reg, 1, b, 2);
  if (e) return e;
  drv_ret_int(&r[0], (int32_t)(int8_t)b[0] * 100 + (b[1] >> 6) * 25);
  *nr = 1;
  return 0;
}
static const drv_method_t METHODS[] = { { "read", "", m_read }, { "set", "iiiiii", m_set }, { "temp", "", m_temp } };
static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "DS3231 RTC (I2C)", METHODS, 3, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
