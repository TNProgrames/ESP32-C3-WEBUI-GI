// يضغط كل ملف في data/app إلى .gz ويحذف الأصل (الخادم يخدم .gz تلقائياً)
import { readdirSync, readFileSync, writeFileSync, unlinkSync } from 'node:fs';
import { gzipSync } from 'node:zlib';
import { join } from 'node:path';

const dir = new URL('../../data/app/', import.meta.url).pathname;
for (const f of readdirSync(dir)) {
  if (f.endsWith('.gz')) continue;
  const p = join(dir, f);
  writeFileSync(p + '.gz', gzipSync(readFileSync(p), { level: 9 }));
  unlinkSync(p);
  console.log('gzip', f);
}
