// bme280: حساس الحرارة والضغط والرطوبة Bosch BME280 / BMP280 عبر I2C (حسابات صحيحة بلا float)
// cfg: addr=0x76
// الدوال: read() -> temp_c_x100, press_pa, hum_x100 · chip_id() -> id · info() -> model
#include "drvrt.h"

typedef struct {
  const drv_api_t* api;
  uint8_t addr, id;
  uint16_t dig_T1, dig_P1;
  int16_t  dig_T2, dig_T3;
  int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
  uint8_t  dig_H1, dig_H3;
  int16_t  dig_H2, dig_H4, dig_H5;
  int8_t   dig_H6;
} Bme;

static const char NAME[] = "bme280";

static int rd_reg(Bme* b, uint8_t reg, uint8_t* out, size_t n) {
  return b->api->i2c_write_read(b->addr, &reg, 1, out, n);
}
static int wr_reg(Bme* b, uint8_t reg, uint8_t val) {
  uint8_t buf[2] = { reg, val };
  return b->api->i2c_write(b->addr, buf, 2);
}
static uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static int16_t  s16(const uint8_t* p) { return (int16_t)u16(p); }

static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  Bme* b = (Bme*)api->alloc(sizeof(Bme));
  if (!b) return 1;
  memset(b, 0, sizeof(*b));
  b->api = api;
  b->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x76);

  int e = rd_reg(b, 0xD0, &b->id, 1);
  if (e || (b->id != 0x60 && b->id != 0x58 && b->id != 0x56 && b->id != 0x57)) {
    api->release(b);
    return e ? e : 1;
  }

  uint8_t c1[26];
  e = rd_reg(b, 0x88, c1, 26);
  if (e) { api->release(b); return e; }
  b->dig_T1 = u16(c1 + 0); b->dig_T2 = s16(c1 + 2); b->dig_T3 = s16(c1 + 4);
  b->dig_P1 = u16(c1 + 6); b->dig_P2 = s16(c1 + 8); b->dig_P3 = s16(c1 + 10);
  b->dig_P4 = s16(c1 + 12); b->dig_P5 = s16(c1 + 14); b->dig_P6 = s16(c1 + 16);
  b->dig_P7 = s16(c1 + 18); b->dig_P8 = s16(c1 + 20); b->dig_P9 = s16(c1 + 22);
  b->dig_H1 = c1[25];

  if (b->id == 0x60) {
    uint8_t c2[7];
    e = rd_reg(b, 0xE1, c2, 7);
    if (e) { api->release(b); return e; }
    b->dig_H2 = s16(c2 + 0);
    b->dig_H3 = c2[2];
    b->dig_H4 = (int16_t)(((int16_t)(int8_t)c2[3] << 4) | (c2[4] & 0x0F));
    b->dig_H5 = (int16_t)(((int16_t)(int8_t)c2[5] << 4) | (c2[4] >> 4));
    b->dig_H6 = (int8_t)c2[6];
    wr_reg(b, 0xF2, 0x01); // hum osrs x1
  }
  wr_reg(b, 0xF4, 0x27);   // temp x1, press x1, normal mode
  wr_reg(b, 0xF5, 0xA0);   // standby 1000ms
  *ctx = b;
  return 0;
}

static void do_close(void* ctx) {
  Bme* b = (Bme*)ctx;
  wr_reg(b, 0xF4, 0x00); // sleep
  b->api->release(b);
}

static int m_read(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  Bme* b = (Bme*)ctx;
  uint8_t raw[8];
  int e = rd_reg(b, 0xF7, raw, b->id == 0x60 ? 8 : 6);
  if (e) return e;

  int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | (raw[2] >> 4);
  int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | (raw[5] >> 4);

  // Bosch 32-bit fixed-point temperature compensation -> 0.01 C
  int32_t v1 = ((((adc_T >> 3) - ((int32_t)b->dig_T1 << 1))) * ((int32_t)b->dig_T2)) >> 11;
  int32_t v2 = (((((adc_T >> 4) - ((int32_t)b->dig_T1)) * ((adc_T >> 4) - ((int32_t)b->dig_T1))) >> 12) * ((int32_t)b->dig_T3)) >> 14;
  int32_t t_fine = v1 + v2;
  int32_t temp_x100 = (t_fine * 5 + 128) >> 8;

  // Bosch 64-bit integer pressure compensation -> Pa
  int64_t p1 = (int64_t)t_fine - 128000;
  int64_t p2 = p1 * p1 * (int64_t)b->dig_P6;
  p2 = p2 + ((p1 * (int64_t)b->dig_P5) << 17);
  p2 = p2 + (((int64_t)b->dig_P4) << 35);
  p1 = ((p1 * p1 * (int64_t)b->dig_P3) >> 8) + ((p1 * (int64_t)b->dig_P2) << 12);
  p1 = (((((int64_t)1) << 47) + p1)) * ((int64_t)b->dig_P1) >> 33;
  uint32_t press_pa = 0;
  if (p1 != 0) {
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - p2) * 3125) / p1;
    p1 = (((int64_t)b->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    p2 = (((int64_t)b->dig_P8) * p) >> 19;
    p = ((p + p1 + p2) >> 8) + (((int64_t)b->dig_P7) << 4);
    press_pa = (uint32_t)(p >> 8);
  }

  // Bosch humidity compensation -> 0.01 %RH
  int32_t hum_x100 = 0;
  if (b->id == 0x60) {
    int32_t adc_H = ((int32_t)raw[6] << 8) | raw[7];
    int32_t h = t_fine - 76800;
    h = (((((adc_H << 14) - (((int32_t)b->dig_H4) << 20) - (((int32_t)b->dig_H5) * h)) + 16384) >> 15) *
         (((((((h * ((int32_t)b->dig_H6)) >> 10) * (((h * ((int32_t)b->dig_H3)) >> 11) + 32768)) >> 10) + 2097152) *
           ((int32_t)b->dig_H2) + 8192) >> 14));
    h = h - (((((h >> 15) * (h >> 15)) >> 7) * ((int32_t)b->dig_H1)) >> 4);
    if (h < 0) h = 0;
    if (h > 419430400) h = 419430400;
    hum_x100 = (int32_t)(((uint32_t)(h >> 12) * 100u) >> 10);
  }

  drv_ret_int(&r[0], temp_x100);
  drv_ret_int(&r[1], (int32_t)press_pa);
  drv_ret_int(&r[2], hum_x100);
  *nr = 3;
  return 0;
}

static int m_chip_id(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  drv_ret_int(&r[0], ((Bme*)ctx)->id);
  *nr = 1;
  return 0;
}

static int m_info(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na;
  drv_ret_str(&r[0], ((Bme*)ctx)->id == 0x60 ? "BME280" : "BMP280");
  *nr = 1;
  return 0;
}

static const drv_method_t METHODS[] = {
  { "read",    "", m_read },
  { "chip_id", "", m_chip_id },
  { "info",    "", m_info },
};

static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "BME280/BMP280 temp/press/hum (I2C)", METHODS, 3, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
