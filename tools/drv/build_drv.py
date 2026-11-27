#!/usr/bin/env python3
"""يبني درايفر موقّعًا:  build_drv.py <مجلد الدرايفر> [-o out.drv] [--version 1.0]
المجلد يحوي ملفات .c (الأول/كلها تُجمَّع) ويكون الاسم = اسم المجلد."""
import argparse, glob, hashlib, os, struct, subprocess, sys, tempfile
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
from cryptography.hazmat.primitives import hashes, serialization

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
KEY = os.path.join(HERE, 'drv_key.pem')
API_VERSION = int(__import__('re').search(r'#define DRV_API_VERSION (\d+)', open(os.path.join(ROOT, 'src', 'drv_api.h')).read()).group(1))   # يطابق الفيرموير
MAX_FILE = 24 * 1024
CFLAGS = ['--target=riscv32', '-march=rv32im', '-mabi=ilp32', '-Os', '-fPIC', '-ffreestanding', '-fno-builtin',
          '-fvisibility=hidden', '-mno-relax', '-fno-stack-protector', '-fno-unwind-tables',
          '-fno-asynchronous-unwind-tables', '-ffunction-sections', '-fdata-sections', '-Wall', '-Werror',
          '-I', os.path.join(ROOT, 'src'), '-I', HERE]
LDFLAGS = ['-m', 'elf32lriscv', '-shared', '-Bsymbolic', '-nostdlib', '--gc-sections', '-z', 'norelro',
           '-z', 'noseparate-code', '--hash-style=sysv', '--entry=drv_entry', '-z', 'max-page-size=16']

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.exit('فشل: ' + ' '.join(cmd) + '\n' + r.stdout + r.stderr)

def pack_image(so):
    d = open(so, 'rb').read()
    if d[:4] != b'\x7fELF' or d[4] != 1: sys.exit('ليس ELF32')
    (e_type, e_machine, _, e_entry, e_phoff, _, _, _, e_phentsize, e_phnum) = struct.unpack_from('<HHIIIIIHHH', d, 16)
    if e_machine != 243: sys.exit('ليس RISC-V')
    loads, dyn = [], None
    for i in range(e_phnum):
        p_type, p_off, p_va, _, p_fsz, p_msz, _, _ = struct.unpack_from('<IIIIIIII', d, e_phoff + i * e_phentsize)
        if p_type == 1: loads.append((p_va, p_off, p_fsz, p_msz))
        if p_type == 2: dyn = (p_off, p_fsz)
    if not loads: sys.exit('لا مقاطع تحميل')
    base = min(l[0] for l in loads)
    if base != 0: sys.exit('يجب أن يبدأ التحميل من العنوان 0')
    mem_len = max(l[0] + l[3] for l in loads)
    img_len = max(l[0] + l[2] for l in loads)
    img = bytearray(img_len)
    for va, off, fsz, _ in loads: img[va:va + fsz] = d[off:off + fsz]
    # أي رمز غير معرَّف (مثل __divdi3 غير الموجود) يعني قفزة إلى العنوان 0 على الجهاز: نرفضه هنا
    e_shoff, e_shentsize, e_shnum = struct.unpack_from('<III', d, 32)[0], *struct.unpack_from('<HH', d, 46)
    for i in range(e_shnum):
        sh = struct.unpack_from('<IIIIIIIIII', d, e_shoff + i * e_shentsize)
        if sh[1] == 11:   # SHT_DYNSYM
            strtab = struct.unpack_from('<IIIIIIIIII', d, e_shoff + sh[6] * e_shentsize)
            for k in range(1, sh[5] // 16):
                st_name, _, _, _, _, st_shndx = struct.unpack_from('<IIIBBH', d, sh[4] + k * 16)
                if st_shndx == 0:
                    nm = d[strtab[4] + st_name:d.index(b'\0', strtab[4] + st_name)].decode()
                    sys.exit('رمز غير معرَّف في الدرايفر: %s (أضفه إلى tools/drv/drvrt.c أو تجنّب استعماله)' % nm)
    relocs = []
    # جدول deinamic: DT_RELA(7) DT_RELASZ(8) DT_REL(17) DT_RELSZ(18) — العناوين افتراضية = إزاحات ملف (نقرؤها عبر المقاطع)
    def va2off(va):
        for lva, loff, lfsz, _ in loads:
            if lva <= va < lva + lfsz: return loff + va - lva
        sys.exit('عنوان خارج المقاطع')
    rela = relasz = 0
    if dyn:
        for i in range(0, dyn[1], 8):
            tag, val = struct.unpack_from('<iI', d, dyn[0] + i)
            if tag == 0: break
            if tag == 7: rela = val
            if tag == 8: relasz = val
            if tag in (17, 18): sys.exit('REL غير مدعوم (استعمل RELA)')
    for i in range(0, relasz, 12):
        r_off, r_info, r_add = struct.unpack_from('<IIi', d, va2off(rela) + i)
        typ, sym = r_info & 0xff, r_info >> 8
        if typ != 3 or sym != 0:
            sys.exit('إعادة تموضع غير مدعومة (type=%d sym=%d): الدرايفر يجب ألا يستورد رموزًا' % (typ, sym))
        if r_off % 4 or r_off + 4 > img_len: sys.exit('إزاحة إعادة تموضع غير صالحة %#x' % r_off)
        struct.pack_into('<I', img, r_off, r_add & 0xffffffff)   # الإضافة تُخزَّن في الكلمة، والفيرموير يزيد عليها العنوان الأساسي
        relocs.append(r_off)
    if e_entry >= img_len: sys.exit('نقطة الدخول خارج الصورة')
    return bytes(img), mem_len, sorted(relocs), e_entry

def build(src_dir, out, version):
    name = os.path.basename(os.path.abspath(src_dir))
    if not name.isalnum() or len(name) > 15: sys.exit('اسم الدرايفر: حروف وأرقام حتى 15')
    srcs = sorted(glob.glob(os.path.join(src_dir, '*.c'))) + [os.path.join(HERE, 'drvrt.c')]
    with tempfile.TemporaryDirectory() as t:
        objs = []
        for s in srcs:
            o = os.path.join(t, os.path.basename(s) + '.o'); run(['clang'] + CFLAGS + ['-c', s, '-o', o]); objs.append(o)
        so = os.path.join(t, name + '.so')
        run(['ld.lld'] + LDFLAGS + objs + ['-o', so])
        img, mem_len, relocs, entry = pack_image(so)
    hdr = b'ESPD' + struct.pack('<HHIIII', 1, API_VERSION, entry, len(img), mem_len, len(relocs))
    hdr += name.encode().ljust(16, b'\0') + version.encode()[:7].ljust(8, b'\0') + b'\0' * 16
    assert len(hdr) == 64
    body = hdr + img + b''.join(struct.pack('<I', r) for r in relocs)
    k = serialization.load_pem_private_key(open(KEY, 'rb').read(), None)
    r, s = decode_dss_signature(k.sign(body, ec.ECDSA(hashes.SHA256())))
    blob = body + r.to_bytes(32, 'big') + s.to_bytes(32, 'big')
    if len(blob) > MAX_FILE: sys.exit('الملف كبير: %d > %d' % (len(blob), MAX_FILE))
    open(out, 'wb').write(blob)
    print('%s v%s: %d بايت (صورة %d، ذاكرة %d، reloc %d) sha256=%s' % (name, version, len(blob), len(img), mem_len, len(relocs), hashlib.sha256(blob).hexdigest()[:12]))

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('dir'); ap.add_argument('-o'); ap.add_argument('--version', default='1.0')
    a = ap.parse_args()
    build(a.dir, a.o or os.path.join(ROOT, 'drivers', 'build', os.path.basename(os.path.abspath(a.dir)) + '.drv'), a.version)
