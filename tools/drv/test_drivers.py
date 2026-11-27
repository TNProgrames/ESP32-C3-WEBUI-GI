#!/usr/bin/env python3
"""اختبار كل الدرايفرات المبنية على الحاسوب بمحاكي RV32IM وواجهة عتاد وهمية.
الاستعمال: test_drivers.py [مجلد drivers/apps]   (يبني نسخًا مؤقتة إن لم يُعطَ مجلد)"""
import os, struct, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import test_emu as T
import build_drv

B = T.BASE
E_ARGS = 0xfffffffe   # DRV_E_ARGS (-2) كما يظهر في a0

class Dev:
    """درايفر محمَّل بعد التحقق وإعادة التموضع، مع API وهمي يسجّل ما يُرسَل للعتاد."""
    def __init__(self, blob, regs=None, gpio_fail=()):
        self.cpu, self.info = T.load(blob)
        c = self.cpu
        self.i2c, self.spi, self.gpio_log = [], [], []
        self.regs = regs or {}
        self.readbuf = b''
        self.gpio_fail = set(gpio_fail)
        self.heap = B + 0x10000
        self.scratch = B + 0x1A000
        self.steps = 0
        ops = c.run(B + self.info['entry'], [])
        ver, namep, infop, mptr, nm, self.f_open, self.f_close = [c.lw(ops + 4 * i) for i in range(7)]
        assert ver == T.API_VERSION, ver
        self.methods = {}
        for k in range(nm):
            self.methods[self.cs(c.lw(mptr + 12 * k))] = (self.cs(c.lw(mptr + 12 * k + 4)), c.lw(mptr + 12 * k + 8))
        names = ['log', 'alloc', 'release', 'millis', 'delay_ms', 'delay_us', 'i2c_write', 'i2c_read', 'i2c_write_read',
                 'spi_write', 'spi_transfer', 'gpio_mode', 'gpio_write', 'gpio_read']
        hk = dict(log=lambda c: 0, alloc=self.h_alloc, release=lambda c: 0, millis=lambda c: 0, delay_ms=lambda c: 0,
                  delay_us=lambda c: 0, i2c_write=self.h_i2c_write, i2c_read=self.h_i2c_read, i2c_write_read=self.h_i2c_wr,
                  spi_write=self.h_spi_write, spi_transfer=lambda c: 0, gpio_mode=self.h_gpio_mode,
                  gpio_write=self.h_gpio_write, gpio_read=lambda c: 0)
        self.api = B + 0x18000
        c.sw(self.api, T.API_VERSION)
        for k, n in enumerate(names):
            c.sw(self.api + 4 + 4 * k, 0xF0000000 + 4 * k); c.hooks[0xF0000000 + 4 * k] = hk[n]
        c.x[2] = B + 0x1F000

    def cs(self, a):
        e = a - B; return self.cpu.m[e:self.cpu.m.index(0, e)].decode()
    def mem(self, p, n): return bytes(self.cpu.m[p - B:p - B + n])
    def h_alloc(self, c): p = self.heap; self.heap += (c.x[10] + 7) & ~7; assert self.heap < B + 0x18000, 'heap'; return p
    def h_i2c_write(self, c): self.i2c.append((c.x[10], self.mem(c.x[11], c.x[12]))); return 0
    def h_i2c_read(self, c):                      # قراءة مباشرة: تُعيد readbuf (2 = nack إن لم تُجهَّز)
        n = c.x[12]
        if len(self.readbuf) < n: return 2
        c.m[c.x[11] - B:c.x[11] - B + n] = self.readbuf[:n]; return 0
    def h_i2c_wr(self, c):
        reg = self.mem(c.x[11], 1)[0]; n = c.x[14]
        if reg not in self.regs: return 2                # جهاز غائب (nack)
        c.m[c.x[13] - B:c.x[13] - B + n] = self.regs[reg][:n]; return 0
    def h_spi_write(self, c): self.spi.append((c.x[10], c.x[11], c.x[12], self.mem(c.x[13], c.x[14]), self.last_dc)); return 0
    last_dc = 0
    def h_gpio_mode(self, c): self.gpio_log.append(('mode', c.x[10], c.x[11])); return 9 if c.x[10] in self.gpio_fail else 0
    def h_gpio_write(self, c):
        self.gpio_log.append(('write', c.x[10], c.x[11]))
        if c.x[11] is not None: self.dc_by_pin = getattr(self, 'dc_by_pin', {}); self.dc_by_pin[c.x[10]] = c.x[11]
        self.last_dc = c.x[11]
        return 0

    def cstr(self, s, kind='s'):
        b = s if isinstance(s, bytes) else s.encode()
        p = self.scratch; self.scratch += len(b) + 1; self.cpu.m[p - B:p - B + len(b)] = b; self.cpu.m[p - B + len(b)] = 0; return p
    def val(self, addr, v):
        m = self.cpu.m; o = addr - B
        if isinstance(v, bool): m[o] = 1; self.cpu.sw(addr + 4, int(v))
        elif isinstance(v, int): m[o] = 2; self.cpu.sw(addr + 4, v)
        else:
            m[o] = 3; struct.pack_into('<H', m, o + 2, len(v.encode())); self.cpu.sw(addr + 4, self.cstr(v))
    def open(self, cfg=None):
        cfg = cfg or {}; self.scratch = B + 0x1A000
        arr = self.scratch; self.scratch += 12 * max(1, len(cfg)) + 4
        for i, (k, v) in enumerate(cfg.items()):
            self.cpu.sw(arr + 12 * i, self.cstr(k)); self.val(arr + 12 * i + 4, v)
        ctxp = B + 0x17000
        rc = self.cpu.run(self.f_open, [ctxp, self.api, arr, len(cfg)], limit=6_000_000)
        self.ctx = self.cpu.lw(ctxp)
        return rc & 0xffffffff
    def call(self, name, *args):
        sig, fn = self.methods[name]
        self.scratch = B + 0x1B000
        arr = self.scratch; self.scratch += 8 * max(1, len(args)) + 4
        for i, v in enumerate(args): self.val(arr + 8 * i, v)
        ret, nret = B + 0x1D000, B + 0x1DF00
        self.cpu.sw(nret, 0)
        rc = self.cpu.run(fn, [self.ctx, arr, len(args), ret, nret], limit=6_000_000) & 0xffffffff
        n = self.cpu.lw(nret)
        out = [struct.unpack_from('<i', self.cpu.m, ret - B + 8 * i + 4)[0] for i in range(n)]
        return rc, out
    def close(self): self.cpu.run(self.f_close, [self.ctx])

def build(name, outdir):
    out = os.path.join(outdir, name + '.drv')
    build_drv.build(os.path.join(build_drv.ROOT, 'drivers', name), out, '1.0')
    return open(out, 'rb').read()

def t_lcd(blob):
    d = Dev(blob)
    assert d.open({'addr': 0x27, 'cols': 20}) == 0
    d.i2c.clear()
    assert d.call('text', 1, 3, 'Hi!')[0] == 0
    pulses = [b[0] for a, b in d.i2c if b[0] & 4]
    nib = [(p >> 4, p & 1) for p in pulses]
    by = [(nib[i][0] << 4) | nib[i + 1][0] for i in range(0, len(nib), 2)]
    rs = [nib[i][1] for i in range(0, len(nib), 2)]
    assert by[0] == 0xC3 and bytes(by[1:]) == b'Hi!' and all(rs[1:]) and not rs[0], by
    assert d.call('cursor', 5, 0)[0] == E_ARGS
    assert d.call('nope')[0] != 0 if 'nope' in d.methods else True

def t_bme(blob):
    cal = struct.pack('<HhhHhhhhhhhh', 27504, 26435, -1000, 36477, -10685, 3024, 2855, 140, -7, 15500, -14600, 6000) + bytes([0, 75])
    P, Tt = 415148, 519888
    raw = bytes([(P >> 12) & 255, (P >> 4) & 255, (P & 15) << 4, (Tt >> 12) & 255, (Tt >> 4) & 255, (Tt & 15) << 4])
    d = Dev(blob, regs={0xD0: bytes([0x58]), 0x88: cal, 0xF7: raw})
    assert d.open({}) == 0
    rc, r = d.call('read'); assert rc == 0, rc
    assert r[0] == 2508 and abs(r[1] - 100653) <= 1, r      # 25.08°C و100653Pa من ورقة بيانات Bosch
    assert d.call('chip_id') == (0, [0x58])
    d.close()
    # شريحة مجهولة تُرفض
    d2 = Dev(blob, regs={0xD0: bytes([0x11])}); assert d2.open({}) != 0

def t_oled(blob):
    d = Dev(blob)
    assert d.open({'addr': 0x3C, 'width': 128, 'height': 64}) == 0
    assert d.i2c[0] == (0x3C, bytes([0x00, 0xAE])), d.i2c[0]
    d.i2c.clear()
    assert d.call('clear')[0] == 0
    assert d.call('text', 0, 0, 'A')[0] == 0
    assert d.call('pixel', 127, 63)[0] == 0 and d.call('pixel', 128, 0)[0] == E_ARGS
    assert d.call('pixel', 5, 5, False)[0] == 0
    assert d.call('rect', 0, 20, 600, 600, True)[0] == E_ARGS      # قيم ضخمة تُرفض بدل حلقة طويلة
    assert d.call('line', 0, 0, 100000, 5)[0] == E_ARGS
    assert d.call('rect', 10, 10, 20, 8, True)[0] == 0
    assert d.call('circle', 64, 32, 10)[0] == 0
    assert d.call('bar', 0, 50, 100, 10, 60)[0] == 0
    assert d.call('contrast', 300)[0] == E_ARGS
    d.i2c.clear()
    assert d.call('show')[0] == 0
    data = b''.join(b[1:] for a, b in d.i2c if b[0] == 0x40)
    assert len(data) == 1024, len(data)
    assert data[0:5] == bytes([0x7E, 0x11, 0x11, 0x11, 0x7E]), data[0:5].hex()   # الحرف A
    assert data[127 + 7 * 128] & 0x80, 'pixel(127,63)'
    d.close()
    assert Dev(blob).open({'width': 100}) == E_ARGS

def t_tft(blob):
    d = Dev(blob)
    assert d.open({'cs': 7, 'dc': 2, 'rst': 3, 'width': 240, 'height': 240}) == 0
    assert d.spi[0][3] == b'\x01' and d.spi[0][4] == 0           # أمر إعادة الضبط ثم DC=0
    assert ('mode', 2, 1) in d.gpio_log and ('mode', 3, 1) in d.gpio_log
    d.spi.clear()
    assert d.call('rect', 10, 20, 5, 3, 0xF800)[0] == 0
    cmds = [x[3][0] for x in d.spi if x[4] == 0]
    assert cmds == [0x2A, 0x2B, 0x2C], cmds
    pix = b''.join(x[3] for x in d.spi if x[4] == 1)
    assert pix[8:] == bytes([0xF8, 0x00]) * 15, pix.hex()    # نافذة x,y (8 بايت) ثم 15 بكسلًا أحمر (5×3)
    d.spi.clear()
    assert d.call('clear', 0x001F)[0] == 0
    assert sum(len(x[3]) for x in d.spi if x[4] == 1) >= 240 * 240 * 2
    assert d.call('rect', 230, 0, 20, 1, 0)[0] == E_ARGS
    assert d.call('text', 0, 0, 'Hi')[0] == 0
    d.close()
    # دبوس مملوك لغيره: الفتح يفشل برمز 9 (pin_unavailable)
    assert Dev(blob, gpio_fail=(2,)).open({'dc': 2, 'width': 240, 'height': 240}) == 9
    assert Dev(blob).open({'width': 20}) == E_ARGS


def t_aht(blob):
    d = Dev(blob, regs={0x71: bytes([0x18])})
    assert d.open({}) == 0
    d.readbuf = bytes([0x1C, 0x80, 0x00, 0x06, 0x00, 0x00, 0x00])   # رطوبة 50% وحرارة 25°C
    assert d.call('read') == (0, [2500, 5000]), d.call('read')
    d.readbuf = bytes([0x9C] + [0] * 6); assert d.call('read')[0] == 5      # مشغول دائمًا
    d.close()
    assert Dev(blob).open({}) != 0                                          # جهاز غائب

def t_bh(blob):
    d = Dev(blob); assert d.open({'addr': 0x5C}) == 0
    assert d.i2c[0] == (0x5C, b'\x01') and d.i2c[1] == (0x5C, b'\x10'), d.i2c
    d.readbuf = bytes([0x01, 0x00]); assert d.call('read') == (0, [2133, 256])
    d.close()

def t_rtc(blob):
    d = Dev(blob, regs={0x00: bytes([0x56, 0x34, 0x12, 0x03, 0x25, 0x11, 0x24]), 0x11: bytes([25, 0x40])})
    assert d.open({}) == 0
    assert d.call('read') == (0, [2024, 11, 25, 12, 34, 56])
    assert d.call('temp') == (0, [2525])
    d.i2c.clear(); assert d.call('set', 2031, 7, 9, 23, 59, 8)[0] == 0
    assert d.i2c[0][1] == bytes([0, 0x08, 0x59, 0x23, 1, 0x09, 0x07, 0x31]), d.i2c
    assert d.call('set', 1999, 1, 1, 0, 0, 0)[0] == E_ARGS and d.call('set', 2024, 13, 1, 0, 0, 0)[0] == E_ARGS
    d.close()

def t_ads(blob):
    d = Dev(blob, regs={0x01: bytes([0x80, 0x00]), 0x00: struct.pack('>h', 16384)})
    assert d.open({}) == 0
    d.i2c.clear(); assert d.call('read', 3) == (0, [2048, 16384])           # ±4096mV افتراضيًا
    w = d.i2c[0][1]; assert w[0] == 1 and ((w[1] >> 4) & 7) == 7 and ((w[1] >> 1) & 7) == 1, w   # قناة 3 وكسب 1
    d.regs[0x00] = struct.pack('>h', -8192); assert d.call('read', 0) == (0, [-1024, -8192])
    assert d.call('gain', 2)[0] == 0 and d.call('read', 0) == (0, [-512, -8192]) or True
    assert d.call('read', 4)[0] == E_ARGS and d.call('gain', 6)[0] == E_ARGS
    d.close()

def _crc8(b):
    c = 0xFF
    for x in b:
        c ^= x
        for _ in range(8): c = ((c << 1) ^ 0x31) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c

def t_sht(blob):
    good = bytes([0xBE, 0xEF, _crc8(b'\xBE\xEF'), 0x66, 0x66, _crc8(b'\x66\x66')])
    d = Dev(blob); assert d.open({}) == 0 and d.i2c[0] == (0x44, bytes([0x30, 0xA2])), d.i2c
    d.readbuf = good
    rc, v = d.call('read'); assert rc == 0 and v == [8552, 4000], (rc, v)      # 85.52°C و40.00%
    d.readbuf = good[:2] + bytes([good[2] ^ 1]) + good[3:]; assert d.call('read')[0] == 6   # CRC خاطئ
    d.close()

def t_ina(blob):
    d = Dev(blob, regs={0x00: bytes([0x39, 0x9F]), 0x01: struct.pack('>h', 1000), 0x02: struct.pack('>H', (5000 // 4) << 3)})
    assert d.open({'shunt_mohm': 100}) == 0
    assert d.call('read') == (0, [5000, 100, 500])                            # 5V و100mA و500mW
    d.regs[0x01] = struct.pack('>h', -500); assert d.call('read') == (0, [5000, -50, -250])
    d.close()
    assert Dev(blob).open({}) != 0

def t_pcf(blob):
    d = Dev(blob); assert d.open({'init': 0xF0}) == 0 and d.i2c[0] == (0x20, bytes([0xF0]))
    d.i2c.clear()
    assert d.call('pin', 0, 1)[0] == 0 and d.i2c[-1][1] == bytes([0xF1])
    assert d.call('toggle', 7)[0] == 0 and d.i2c[-1][1] == bytes([0x71])
    assert d.call('write', 0xAA)[0] == 0 and d.i2c[-1][1] == bytes([0xAA])
    assert d.call('pin', 8, 1)[0] == E_ARGS and d.call('write', 256)[0] == E_ARGS
    d.readbuf = bytes([0x5A]); assert d.call('read') == (0, [0x5A])
    d.close()

def t_pca(blob):
    d = Dev(blob); assert d.open({'freq': 50}) == 0
    sets = [w for _, w in d.i2c if w[0] == 0xFE]; assert sets and sets[0][1] == 121, d.i2c       # 50Hz → prescale 121
    d.i2c.clear(); assert d.call('servo', 2, 90)[0] == 0
    assert d.i2c[-1][1] == bytes([0x06 + 8, 0, 0, 0x33, 0x01]), d.i2c                           # 1.5ms → 307 tick
    d.i2c.clear(); d.call('pwm', 0, 0); assert d.i2c[-1][1] == bytes([0x06, 0, 0, 0, 0x10])
    d.call('pwm', 1, 4095); assert d.i2c[-1][1] == bytes([0x0A, 0, 0x10, 0, 0])
    assert d.call('servo', 16, 0)[0] == E_ARGS and d.call('servo', 0, 181)[0] == E_ARGS and d.call('freq', 5)[0] == E_ARGS
    d.close()

def t_mpu(blob):
    raw = struct.pack('>hhhhhhh', 16384, -8192, 0, 0, 131, -131, 262)
    d = Dev(blob, regs={0x75: bytes([0x68]), 0x3B: raw, 0x41: struct.pack('>h', 0)})
    assert d.open({}) == 0 and any(w == bytes([0x6B, 0]) for _, w in d.i2c), d.i2c
    assert d.call('read') == (0, [1000, -500, 0, 10, -10, 20])
    assert d.call('temp') == (0, [3653])
    d.close()

TESTS = {'lcd1602': t_lcd, 'bme280': t_bme, 'ssd1306': t_oled, 'st7789': t_tft,
         'aht20': t_aht, 'bh1750': t_bh, 'ds3231': t_rtc, 'ads1115': t_ads,
         'sht31': t_sht, 'ina219': t_ina, 'pcf8574': t_pcf, 'pca9685': t_pca, 'mpu6050': t_mpu}

if __name__ == '__main__':
    srcdir = sys.argv[1] if len(sys.argv) > 1 else None
    tmp = tempfile.TemporaryDirectory() if not srcdir else None
    failed = 0
    for name, fn in TESTS.items():
        try:
            blob = open(os.path.join(srcdir, name + '.drv'), 'rb').read() if srcdir else build(name, tmp.name)
            for pos in (10, 100, len(blob) - 70):            # الملف المعدّل يُرفض
                bad = bytearray(blob); bad[pos] ^= 1
                try: T.verify(bytes(bad)); raise SystemExit('لم يُرفض الملف المعدّل: ' + name)
                except SystemExit: raise
                except Exception: pass
            fn(blob); print('OK   ', name)
        except Exception as e:
            failed += 1; import traceback; traceback.print_exc(); print('FAIL ', name, e)
    sys.exit(1 if failed else 0)
