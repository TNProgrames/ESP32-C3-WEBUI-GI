#!/usr/bin/env python3
"""يبني كل الدرايفرات في drivers/*/ ويوقّعها وينتج drivers/apps/index.json."""
import argparse
import glob
import hashlib
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
import build_drv  # noqa: E402


def parse_driver_meta(src_dir):
    info = ''
    methods = []
    for path in sorted(glob.glob(os.path.join(src_dir, '*.c'))):
        txt = open(path, 'r', encoding='utf-8', errors='replace').read()
        m_ops = re.search(r'drv_ops_t\s+OPS\s*=\s*\{[^}]*?"([^"]*)"\s*,\s*METHODS', txt, re.S)
        if m_ops:
            info = m_ops.group(1)
        m_tbl = re.search(r'METHODS\s*\[\]\s*=\s*\{(.*?)\};', txt, re.S)
        if m_tbl:
            for nm, sig in re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]*)"\s*,', m_tbl.group(1)):
                methods.append({'name': nm, 'sig': sig})
    return info, methods


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', default='1.0')
    ap.add_argument('--out-dir', default=os.path.join(ROOT, 'drivers', 'apps'))
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    drivers_root = os.path.join(ROOT, 'drivers')
    entries = []

    for name in sorted(os.listdir(drivers_root)):
        if name in ('build', 'apps'):
            continue
        src_dir = os.path.join(drivers_root, name)
        if not os.path.isdir(src_dir) or not glob.glob(os.path.join(src_dir, '*.c')):
            continue
        out_drv = os.path.join(args.out_dir, f'{name}.drv')
        build_drv.build(src_dir, out_drv, args.version)
        blob = open(out_drv, 'rb').read()
        _, api_ver, _, img_len, mem_len, _ = struct.unpack_from('<HHIIII', blob, 4)
        info, methods = parse_driver_meta(src_dir)
        entries.append({
            'name': name,
            'version': args.version[:7],
            'api': api_ver,
            'size': len(blob),
            'mem': mem_len,
            'sha256': hashlib.sha256(blob).hexdigest(),
            'file': f'{name}.drv',
            'info': info,
            'methods': methods,
        })

    # التطبيقات: drivers/apps/<name>.lua + <name>.json (title, desc, driver) تُقرأ وتُنسخ إلى مجلد الإخراج عند الحاجة
    apps = []
    apps_root = os.path.join(ROOT, 'drivers', 'apps')
    same_dir = os.path.abspath(apps_root) == os.path.abspath(args.out_dir)
    for meta in sorted(glob.glob(os.path.join(apps_root, '*.json'))):
        name = os.path.splitext(os.path.basename(meta))[0]
        if name == 'index':
            continue
        lua = os.path.join(apps_root, name + '.lua')
        if not os.path.isfile(lua) or not re.fullmatch(r'[A-Za-z0-9_]{1,24}', name):
            continue
        m = json.load(open(meta, encoding='utf-8'))
        code = open(lua, 'rb').read()
        if not same_dir:
            with open(os.path.join(args.out_dir, name + '.lua'), 'wb') as f:
                f.write(code)
        drivers = m.get('drivers') or ([m['driver']] if m.get('driver') else [])
        entry = {'name': name, 'title': m.get('title', name), 'desc': m.get('desc', ''), 'icon': m.get('icon', 'box'),
                 'category': m.get('category', 'tools'), 'driver': drivers[0] if drivers else '', 'drivers': drivers,
                 'file': name + '.lua', 'size': len(code), 'sha256': hashlib.sha256(code).hexdigest()}
        html = os.path.join(apps_root, name + '.html')
        if m.get('page') and os.path.isfile(html):             # صفحة HTML/canvas ≤ 24KB تُحفظ في /ui/<name>.html
            page = open(html, 'rb').read()
            if len(page) > 24 * 1024: raise SystemExit(f'{name}.html > 24KB')
            if not same_dir:
                with open(os.path.join(args.out_dir, name + '.html'), 'wb') as f:
                    f.write(page)
            entry.update(page_file=name + '.html', page_size=len(page), page_sha256=hashlib.sha256(page).hexdigest())
        for d in drivers:
            if d not in [e['name'] for e in entries]: raise SystemExit(f'{name}: driver {d} not found')
        apps.append(entry)

    index_path = os.path.join(args.out_dir, 'index.json')
    with open(index_path, 'w', encoding='utf-8') as f:
        json.dump({'version': 1, 'drivers': entries, 'apps': apps}, f, ensure_ascii=False, indent=2)
    print(f'Wrote {index_path} ({len(entries)} drivers)')


if __name__ == '__main__':
    main()
