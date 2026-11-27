#!/usr/bin/env python3
"""اختبار على الحاسوب: يحاكي محمّل الفيرموير (تحقق + إعادة تموضع) ويشغّل الدرايفر بمفسّر RV32IM صغير."""
import struct, sys, hashlib
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
from cryptography.hazmat.primitives import hashes, serialization

import os, re
KEY = os.path.join(os.path.dirname(__file__), 'drv_key.pem')
API_VERSION = int(re.search(r'#define DRV_API_VERSION (\d+)', open(os.path.join(os.path.dirname(__file__), '..', '..', 'src', 'drv_api.h')).read()).group(1))

def verify(blob):
    """نفس منطق drv_loader.cpp"""
    if len(blob) < 64 + 64 or blob[:4] != b'ESPD': raise ValueError('magic')
    fmt, api, entry, img_len, mem_len, nrel = struct.unpack_from('<HHIIII', blob, 4)
    if fmt != 1 or api != API_VERSION: raise ValueError('version')
    if mem_len < img_len or entry >= img_len or entry % 2: raise ValueError('layout')
    if 64 + img_len + 4 * nrel + 64 != len(blob): raise ValueError('size')
    k = serialization.load_pem_private_key(open(KEY, 'rb').read(), None).public_key()
    r, s = int.from_bytes(blob[-64:-32], 'big'), int.from_bytes(blob[-32:], 'big')
    k.verify(encode_dss_signature(r, s), blob[:-64], ec.ECDSA(hashes.SHA256()))   # يرمي إن فسد
    rel = [struct.unpack_from('<I', blob, 64 + img_len + 4 * i)[0] for i in range(nrel)]
    for o in rel:
        if o % 4 or o + 4 > img_len: raise ValueError('reloc')
    return dict(entry=entry, img=blob[64:64 + img_len], mem=mem_len, rel=rel, name=blob[24:40].rstrip(b'\0').decode())

BASE = 0x40000000
class CPU:
    def __init__(s, mem_size=0x20000):
        s.m = bytearray(mem_size); s.x = [0] * 32; s.pc = 0; s.hooks = {}
    def lw(s, a): return struct.unpack_from('<I', s.m, a - BASE)[0]
    def sw(s, a, v): struct.pack_into('<I', s.m, a - BASE, v & 0xffffffff)
    def run(s, entry, args, ret_addr=0xFFFF0000, limit=2_000_000):
        s.x[1] = ret_addr
        for i, v in enumerate(args): s.x[10 + i] = v & 0xffffffff
        s.pc = entry
        for _ in range(limit):
            if s.pc == ret_addr: return s.x[10]
            if s.pc in s.hooks:
                s.x[10] = s.hooks[s.pc](s) & 0xffffffff; s.pc = s.x[1]; continue
            s.step()
        raise RuntimeError('حلقة لا نهائية')
    def step(s):
        i = s.lw(s.pc); op = i & 0x7f; rd = (i >> 7) & 31; f3 = (i >> 12) & 7; r1 = (i >> 15) & 31; r2 = (i >> 20) & 31; f7 = i >> 25
        x = s.x; sx = lambda v: v - (1 << 32) if v & 0x80000000 else v
        immI = sx(i & 0xfff00000) >> 20 if False else ((i >> 20) - (1 << 12) if i >> 31 else i >> 20)
        npc = s.pc + 4
        if op == 0x37: x[rd] = i & 0xfffff000
        elif op == 0x17: x[rd] = (s.pc + (i & 0xfffff000)) & 0xffffffff
        elif op == 0x6f:
            imm = ((i >> 31) << 20) | (((i >> 12) & 0xff) << 12) | (((i >> 20) & 1) << 11) | (((i >> 21) & 0x3ff) << 1)
            if imm & (1 << 20): imm -= 1 << 21
            x[rd] = npc; npc = (s.pc + imm) & 0xffffffff
        elif op == 0x67: t = (x[r1] + immI) & ~1 & 0xffffffff; x[rd] = npc; npc = t
        elif op == 0x63:
            imm = ((i >> 31) << 12) | (((i >> 7) & 1) << 11) | (((i >> 25) & 0x3f) << 5) | (((i >> 8) & 0xf) << 1)
            if imm & (1 << 12): imm -= 1 << 13
            a, b = x[r1], x[r2]; sa, sb = sx(a), sx(b)
            c = [a == b, a != b, 0, 0, sa < sb, sa >= sb, a < b, a >= b][f3]
            if c: npc = s.pc + imm
        elif op == 0x03:
            a = (x[r1] + immI) & 0xffffffff
            if f3 == 0: v = s.m[a - BASE]; v = v - 256 if v & 128 else v
            elif f3 == 1: v = struct.unpack_from('<h', s.m, a - BASE)[0]
            elif f3 == 2: v = s.lw(a)
            elif f3 == 4: v = s.m[a - BASE]
            elif f3 == 5: v = struct.unpack_from('<H', s.m, a - BASE)[0]
            else: raise RuntimeError('load f3')
            x[rd] = v & 0xffffffff
        elif op == 0x23:
            imm = ((i >> 25) << 5) | ((i >> 7) & 31)
            if imm & 0x800: imm -= 0x1000
            a = (x[r1] + imm) & 0xffffffff; v = x[r2]
            if f3 == 0: s.m[a - BASE] = v & 255
            elif f3 == 1: struct.pack_into('<H', s.m, a - BASE, v & 0xffff)
            elif f3 == 2: s.sw(a, v)
            else: raise RuntimeError('store f3')
        elif op == 0x13:
            a = x[r1]; sh = r2
            if f3 == 0: v = a + immI
            elif f3 == 1: v = a << sh
            elif f3 == 2: v = int(sx(a) < immI)
            elif f3 == 3: v = int(a < (immI & 0xffffffff))
            elif f3 == 4: v = a ^ (immI & 0xffffffff)
            elif f3 == 5: v = (sx(a) >> sh) if f7 == 0x20 else (a >> sh)
            elif f3 == 6: v = a | (immI & 0xffffffff)
            else: v = a & (immI & 0xffffffff)
            x[rd] = v & 0xffffffff
        elif op == 0x33:
            a, b = x[r1], x[r2]; sa, sb = sx(a), sx(b)
            if f7 == 1:
                if f3 == 0: v = a * b
                elif f3 == 1: v = (sa * sb) >> 32
                elif f3 == 3: v = (a * b) >> 32
                elif f3 == 4: v = -1 if b == 0 else int(abs(sa) // abs(sb)) * (1 if (sa < 0) == (sb < 0) else -1)
                elif f3 == 5: v = 0xffffffff if b == 0 else a // b
                elif f3 == 6: v = sa if b == 0 else sa - sb * (int(abs(sa) // abs(sb)) * (1 if (sa < 0) == (sb < 0) else -1))
                elif f3 == 7: v = a if b == 0 else a % b
                else: raise RuntimeError('mul f3')
            else:
                if f3 == 0: v = a - b if f7 == 0x20 else a + b
                elif f3 == 1: v = a << (b & 31)
                elif f3 == 2: v = int(sa < sb)
                elif f3 == 3: v = int(a < b)
                elif f3 == 4: v = a ^ b
                elif f3 == 5: v = (sa >> (b & 31)) if f7 == 0x20 else (a >> (b & 31))
                elif f3 == 6: v = a | b
                else: v = a & b
            x[rd] = v & 0xffffffff
        else: raise RuntimeError('opcode %#x @ %#x' % (op, s.pc))
        x[0] = 0; s.pc = npc & 0xffffffff

def load(blob):
    info = verify(blob)
    cpu = CPU()
    img = bytearray(info['img']) + bytes(info['mem'] - len(info['img']))
    for o in info['rel']: struct.pack_into('<I', img, o, (struct.unpack_from('<I', img, o)[0] + BASE) & 0xffffffff)
    cpu.m[0:len(img)] = img
    return cpu, info

if __name__ == '__main__':
    blob = open(sys.argv[1], 'rb').read()
    # 1) رفض التلف
    for pos in (10, 100, len(blob) - 70, len(blob) - 3):
        bad = bytearray(blob); bad[pos] ^= 1
        try: verify(bytes(bad)); sys.exit('لم يُرفض الملف المعدّل عند %d' % pos)
        except Exception: pass
    try: verify(blob[:-1]); sys.exit('لم يُرفض الملف المبتور')
    except Exception: pass
    print('رفض التعديل/البتر: OK')
    # 2) تشغيل lcd1602 بـ API وهمي
    cpu, info = load(blob)
    ops = cpu.run(BASE + info['entry'], [])
    ver, nameptr, infoptr, mptr, nmeth, f_open, f_close = [cpu.lw(ops + 4 * i) for i in range(7)]
    assert ver == API_VERSION and nmeth == 4
    def cs(addr):
        e = addr - BASE
        return cpu.m[e:cpu.m.index(0, e)].decode()
    methods = {cs(cpu.lw(mptr + 12 * k)): (cs(cpu.lw(mptr + 12 * k + 4)), cpu.lw(mptr + 12 * k + 8)) for k in range(nmeth)}
    assert methods['text'][0] == 'iis' and methods['clear'][0] == '' and methods['backlight'][0] == 'b', methods
    HEAP = BASE + 0x10000; STACK = BASE + 0x1F000; cpu.x[2] = STACK
    log = []; heap = [HEAP]
    def alloc(c): p = heap[0]; heap[0] += (c.x[10] + 7) & ~7; return p
    def i2c_write(c):
        a, p, n = c.x[10], c.x[11], c.x[12]; log.append((a, bytes(c.m[p - BASE:p - BASE + n]))); return 0
    api = BASE + 0x18000
    fns = ['log', 'alloc', 'release', 'millis', 'delay_ms', 'delay_us', 'i2c_write', 'i2c_read', 'i2c_write_read']
    hk = {'log': lambda c: 0, 'alloc': alloc, 'release': lambda c: 0, 'millis': lambda c: 0, 'delay_ms': lambda c: 0,
          'delay_us': lambda c: 0, 'i2c_write': i2c_write, 'i2c_read': lambda c: 2, 'i2c_write_read': lambda c: 2}
    cpu.sw(api, API_VERSION)
    for k, n in enumerate(fns):
        cpu.sw(api + 4 + 4 * k, 0xF0000000 + 4 * k); cpu.hooks[0xF0000000 + 4 * k] = hk[n]
    def cstr(s, addr): b = s.encode() + b'\0'; cpu.m[addr - BASE:addr - BASE + len(b)] = b; return addr
    ctxp = BASE + 0x17000
    cfg = BASE + 0x16000; cstr('addr', BASE + 0x16100); cstr('cols', BASE + 0x16110)
    cpu.sw(cfg, BASE + 0x16100); cpu.m[cfg - BASE + 4:cfg - BASE + 5] = bytes([2]); cpu.sw(cfg + 8, 0x3F)       # key,{type,len,i}
    cpu.sw(cfg + 12, BASE + 0x16110); cpu.m[cfg - BASE + 16:cfg - BASE + 17] = bytes([2]); cpu.sw(cfg + 20, 20)
    assert cpu.run(f_open, [ctxp, api, cfg, 2]) == 0
    ctx = cpu.lw(ctxp)
    log.clear()
    args = BASE + 0x15000; cret = BASE + 0x15800; nret = BASE + 0x15F00
    def val(idx, t, **kw):
        o = args + 8 * idx; cpu.m[o - BASE] = t
        if t == 2: cpu.sw(o + 4, kw['i'])
        if t == 3: struct.pack_into('<H', cpu.m, o + 2 - BASE, len(kw['s'])); cpu.sw(o + 4, cstr(kw['s'], BASE + 0x15200 + 64 * idx))
    val(0, 2, i=1); val(1, 2, i=3); val(2, 3, s='Hi!')
    rc = cpu.run(methods['text'][1], [ctx, args, 3, cret, nret])
    assert rc == 0, rc
    assert all(a == 0x3F for a, d in log)
    pulses = [d[0] for a, d in log if d[0] & 4]
    nibs = [(p >> 4, p & 1) for p in pulses]
    bytes_ = [(nibs[i][0] << 4) | nibs[i + 1][0] for i in range(0, len(nibs), 2)]
    rs = [nibs[i][1] for i in range(0, len(nibs), 2)]
    assert bytes_[0] == 0x80 | (0x40 + 3) and rs[0] == 0, hex(bytes_[0])   # المؤشر: صف 1 عمود 3
    assert bytes(bytes_[1:]) == b'Hi!' and all(rs[1:]), (bytes_, rs)
    # حدود: صف خارج النطاق يرجع DRV_E_ARGS (-2)
    val(0, 2, i=5)
    assert cpu.run(methods['cursor'][1], [ctx, args, 2, cret, nret]) & 0xffffffff == 0xfffffffe
    print('تشغيل lcd1602 على RV32IM: OK —', info['name'], 'بايت الصورة', len(info['img']))
