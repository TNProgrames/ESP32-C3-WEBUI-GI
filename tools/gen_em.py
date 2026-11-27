#!/usr/bin/env python3
"""يضغط صفحات الطوارئ web/em.html و web/em_login.html بـgzip ويولّد src/em.h (مصفوفات بايت).
التشغيل: python3 tools/gen_em.py   (CI يشغّله قبل البناء، والملف الناتج يُودَع أيضًا ليعمل البناء المحلي)"""
import gzip, os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def arr(name, path):
    raw = open(os.path.join(ROOT, 'web', path), 'rb').read()
    gz = gzip.compress(raw, 9, mtime=0)          # mtime=0: ناتج ثابت بين التشغيلات
    rows = [', '.join('0x%02x' % b for b in gz[i:i + 16]) for i in range(0, len(gz), 16)]
    return f'// web/{path}: {len(raw)} -> {len(gz)} bytes (gzip)\nstatic const uint8_t {name}[] PROGMEM = {{\n  ' + ',\n  '.join(rows) + f'\n}};\nstatic const size_t {name}_len = {len(gz)};\n'

out = '// مولَّد آليًا بـ tools/gen_em.py من web/*.html — لا تعدّله يدويًا\n#ifndef EM_H\n#define EM_H\n\n#include <Arduino.h>\n\n'
out += arr('em_html_gz', 'em.html') + '\n' + arr('em_login_gz', 'em_login.html') + '\n#endif\n'
open(os.path.join(ROOT, 'src', 'em.h'), 'w').write(out)
print('src/em.h written', len(out), 'chars')
