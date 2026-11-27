#!/usr/bin/env python3
"""يولّد مفتاح التوقيع (P-256) ويكتب المفتاح العام في src/drv_pubkey.h.
المشروع مفتوح المصدر فالمفتاح الخاص يُحفظ في المستودع عمدًا (التوقيع هنا للسلامة لا للسرية)."""
import os, sys
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
KEY = os.path.join(os.path.dirname(__file__), 'drv_key.pem')
HDR = os.path.join(ROOT, 'src', 'drv_pubkey.h')

if os.path.exists(KEY) and '--force' not in sys.argv:
    sys.exit('المفتاح موجود (استعمل --force لاستبداله؛ سيبطل كل الدرايفرات الموقّعة)')
k = ec.generate_private_key(ec.SECP256R1())
open(KEY, 'wb').write(k.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
pub = k.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
body = ',\n  '.join(', '.join('0x%02x' % b for b in pub[i:i + 13]) for i in range(0, len(pub), 13))
open(HDR, 'w').write('// مولَّد بواسطة tools/drv/gen_key.py — لا تعدّله يدويًا\n#pragma once\n#include <stdint.h>\nstatic const uint8_t DRV_PUBKEY[65] = {\n  %s\n};\n' % body)
print('wrote', KEY, HDR)
