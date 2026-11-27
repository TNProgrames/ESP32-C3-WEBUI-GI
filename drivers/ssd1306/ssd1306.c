// ssd1306: شاشة OLED I2C (SSD1306 / SH1106) 128x64 أو 128x32
// cfg: addr=0x3C, width=128, height=64, sh1106=false
// الدوال: clear() · show() · pixel(x,y[,on]) · line(x0,y0,x1,y1[,on]) · rect(x,y,w,h[,fill,on])
//         circle(cx,cy,r[,fill,on]) · bar(x,y,w,h,pct) · text(x,y,str) · invert(on) · contrast(val)
#include "drvrt.h"

typedef struct {
  const drv_api_t* api;
  uint8_t* fb;
  uint16_t fb_len;
  uint8_t addr, w, h, pages, sh1106;
} Oled;

static const char NAME[] = "ssd1306";

// خط ASCII 5x7 مضغوط (0x20..0x7E)
static const uint8_t FONT5X7[95][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},
  {0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08}
};

// حدّ الإحداثيات: بدونه تُنتج قيم ضخمة حلقة لا تنتهي داخل الدرايفر (والحارس لا يقاطع الكود الأصلي)
#define LIM 512
static int far_(int v) { return v < -LIM || v > LIM; }

static int cmd(Oled* o, uint8_t c) {
  uint8_t b[2] = { 0x00, c };
  return o->api->i2c_write(o->addr, b, 2);
}

static void put_px(Oled* o, int x, int y, int on) {
  if (x < 0 || x >= o->w || y < 0 || y >= o->h) return;
  uint16_t idx = (uint16_t)x + (uint16_t)(y >> 3) * o->w;
  uint8_t mask = (uint8_t)(1u << (y & 7));
  if (on) o->fb[idx] |= mask; else o->fb[idx] &= (uint8_t)~mask;
}

static void draw_line(Oled* o, int x0, int y0, int x1, int y1, int on) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? -(y1 - y0) : -(y0 - y1), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    put_px(o, x0, y0, on);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static int do_show(Oled* o) {
  uint8_t pkt[17];
  pkt[0] = 0x40;
  for (uint8_t p = 0; p < o->pages; p++) {
    int e = cmd(o, (uint8_t)(0xB0 | p)); if (e) return e;
    uint8_t col0 = o->sh1106 ? 2 : 0;
    e = cmd(o, (uint8_t)(col0 & 0x0F)); if (e) return e;
    e = cmd(o, (uint8_t)(0x10 | (col0 >> 4))); if (e) return e;
    const uint8_t* row = o->fb + (uint16_t)p * o->w;
    for (uint8_t x = 0; x < o->w; x += 16) {
      uint8_t n = (uint8_t)(o->w - x > 16 ? 16 : o->w - x);
      memcpy(pkt + 1, row + x, n);
      e = o->api->i2c_write(o->addr, pkt, (size_t)n + 1);
      if (e) return e;
    }
  }
  return 0;
}

static int do_open(void** ctx, const drv_api_t* api, const drv_kv_t* cfg, int n) {
  int w = drv_cfg_int(cfg, n, "width", 128);
  int h = drv_cfg_int(cfg, n, "height", 64);
  if ((w != 128 && w != 64) || (h != 64 && h != 32)) return DRV_E_ARGS;
  Oled* o = (Oled*)api->alloc(sizeof(Oled));
  if (!o) return 1;
  uint16_t fb_len = (uint16_t)(w * (h / 8));
  o->fb = (uint8_t*)api->alloc(fb_len);
  if (!o->fb) { api->release(o); return 1; }
  memset(o->fb, 0, fb_len);
  o->api = api; o->fb_len = fb_len;
  o->addr = (uint8_t)drv_cfg_int(cfg, n, "addr", 0x3C);
  o->w = (uint8_t)w; o->h = (uint8_t)h; o->pages = (uint8_t)(h / 8);
  o->sh1106 = (uint8_t)drv_cfg_bool(cfg, n, "sh1106", 0);

  const uint8_t init_cmds[] = {
    0xAE, 0xD5, 0x80, 0xA8, (uint8_t)(h - 1), 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, (uint8_t)(h == 32 ? 0x02 : 0x12),
    0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
  };
  for (size_t i = 0; i < sizeof(init_cmds); i++) {
    int e = cmd(o, init_cmds[i]);
    if (e) { api->release(o->fb); api->release(o); return e; }
  }
  do_show(o);
  *ctx = o;
  return 0;
}

static void do_close(void* ctx) {
  Oled* o = (Oled*)ctx;
  cmd(o, 0xAE);
  o->api->release(o->fb);
  o->api->release(o);
}

static int m_clear(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na; (void)r; (void)nr;
  Oled* o = (Oled*)ctx;
  memset(o->fb, 0, o->fb_len);
  return 0;
}
static int m_show(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)a; (void)na; (void)r; (void)nr;
  return do_show((Oled*)ctx);
}
static int m_pixel(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  Oled* o = (Oled*)ctx;
  int x = a[0].v.i, y = a[1].v.i, on = na >= 3 ? (a[2].v.i ? 1 : 0) : 1;
  if (x < 0 || x >= o->w || y < 0 || y >= o->h) return DRV_E_ARGS;
  put_px(o, x, y, on);
  return 0;
}
static int m_line(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  if (far_(a[0].v.i) || far_(a[1].v.i) || far_(a[2].v.i) || far_(a[3].v.i)) return DRV_E_ARGS;
  draw_line((Oled*)ctx, a[0].v.i, a[1].v.i, a[2].v.i, a[3].v.i, na >= 5 ? (a[4].v.i ? 1 : 0) : 1);
  return 0;
}
static int m_rect(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  Oled* o = (Oled*)ctx;
  int x = a[0].v.i, y = a[1].v.i, w = a[2].v.i, h = a[3].v.i;
  int fill = na >= 5 ? (a[4].v.i ? 1 : 0) : 0;
  int on = na >= 6 ? (a[5].v.i ? 1 : 0) : 1;
  if (w <= 0 || h <= 0 || w > LIM || h > LIM || far_(x) || far_(y)) return DRV_E_ARGS;
  if (fill) {
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put_px(o, x + i, y + j, on);
  } else {
    draw_line(o, x, y, x + w - 1, y, on);
    draw_line(o, x, y + h - 1, x + w - 1, y + h - 1, on);
    draw_line(o, x, y, x, y + h - 1, on);
    draw_line(o, x + w - 1, y, x + w - 1, y + h - 1, on);
  }
  return 0;
}
static int m_circle(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)r; (void)nr;
  Oled* o = (Oled*)ctx;
  int cx = a[0].v.i, cy = a[1].v.i, rad = a[2].v.i;
  int fill = na >= 4 ? (a[3].v.i ? 1 : 0) : 0;
  int on = na >= 5 ? (a[4].v.i ? 1 : 0) : 1;
  if (rad < 0 || rad > 64 || far_(cx) || far_(cy)) return DRV_E_ARGS;
  int x = rad, y = 0, err = 1 - x;
  while (x >= y) {
    if (fill) {
      draw_line(o, cx - x, cy + y, cx + x, cy + y, on);
      draw_line(o, cx - x, cy - y, cx + x, cy - y, on);
      draw_line(o, cx - y, cy + x, cx + y, cy + x, on);
      draw_line(o, cx - y, cy - x, cx + y, cy - x, on);
    } else {
      put_px(o, cx + x, cy + y, on); put_px(o, cx - x, cy + y, on);
      put_px(o, cx + x, cy - y, on); put_px(o, cx - x, cy - y, on);
      put_px(o, cx + y, cy + x, on); put_px(o, cx - y, cy + x, on);
      put_px(o, cx + y, cy - x, on); put_px(o, cx - y, cy - x, on);
    }
    y++;
    if (err < 0) err += 2 * y + 1; else { x--; err += 2 * (y - x + 1); }
  }
  return 0;
}
static int m_bar(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)na; (void)r; (void)nr;
  Oled* o = (Oled*)ctx;
  int x = a[0].v.i, y = a[1].v.i, w = a[2].v.i, h = a[3].v.i, pct = a[4].v.i;
  if (w < 4 || h < 4 || w > LIM || h > LIM || far_(x) || far_(y)) return DRV_E_ARGS;
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  draw_line(o, x, y, x + w - 1, y, 1);
  draw_line(o, x, y + h - 1, x + w - 1, y + h - 1, 1);
  draw_line(o, x, y, x, y + h - 1, 1);
  draw_line(o, x + w - 1, y, x + w - 1, y + h - 1, 1);
  int fw = ((w - 4) * pct) / 100;
  for (int j = 2; j < h - 2; j++)
    for (int i = 2; i < w - 2; i++) put_px(o, x + i, y + j, i - 2 < fw ? 1 : 0);
  return 0;
}
static int m_text(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)na; (void)r; (void)nr;
  Oled* o = (Oled*)ctx;
  int x = a[0].v.i, y = a[1].v.i;
  if (far_(x) || far_(y)) return DRV_E_ARGS;
  const char* s = a[2].v.s;
  for (int k = 0; k < a[2].len; k++) {
    unsigned char ch = (unsigned char)s[k];
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t* g = FONT5X7[ch - 32];
    for (int col = 0; col < 5; col++) {
      uint8_t bits = g[col];
      for (int row = 0; row < 7; row++) put_px(o, x + col, y + row, (bits >> row) & 1);
    }
    x += 6;
    if (x >= o->w) break;
  }
  return 0;
}
static int m_invert(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)na; (void)r; (void)nr;
  return cmd((Oled*)ctx, a[0].v.i ? 0xA7 : 0xA6);
}
static int m_contrast(void* ctx, const drv_val_t* a, int na, drv_val_t* r, int* nr) {
  (void)na; (void)r; (void)nr;
  int v = a[0].v.i;
  if (v < 0 || v > 255) return DRV_E_ARGS;
  Oled* o = (Oled*)ctx;
  int e = cmd(o, 0x81);
  return e ? e : cmd(o, (uint8_t)v);
}

static const drv_method_t METHODS[] = {
  { "clear",    "",       m_clear },
  { "show",     "",       m_show },
  { "pixel",    "iiB",    m_pixel },
  { "line",     "iiiiB",  m_line },
  { "rect",     "iiiiBB", m_rect },
  { "circle",   "iiiBB",  m_circle },
  { "bar",      "iiiii",  m_bar },
  { "text",     "iis",    m_text },
  { "invert",   "b",      m_invert },
  { "contrast", "i",      m_contrast },
};

static const drv_ops_t OPS = { DRV_API_VERSION, NAME, "SSD1306/SH1106 OLED 128x64 (I2C)", METHODS, 10, do_open, do_close };
DRV_EXPORT const drv_ops_t* drv_entry(void) { return &OPS; }
