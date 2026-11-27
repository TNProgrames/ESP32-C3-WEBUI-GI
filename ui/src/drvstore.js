// استقبال الدرايفرات من GitHub (يجلبها المتصفح لا الجهاز) وتثبيتها على الجهاز.
// الفهرس drivers/apps/index.json يُنتجه tools/drv/build_index.py ويحوي لكل درايفر:
//   name, version, api, size, mem, sha256, file (نسبي لمكان الفهرس), info, methods[{name,sig}]
// التحقق الحاسم من التوقيع يتم على الجهاز (/drv/install)؛ فحص sha256 هنا اختياري لكشف التنزيل التالف مبكرًا.
import { authHeaders } from './api.js';

export const DRV_MAX = 4;   // أقصى عدد درايفرات مثبّتة (DRV_MAX_LOADED في الفيرموير)
export const REPO = 'TNProgrames/ESP32-C3-WEBUI';
export const DEFAULT_INDEX = typeof location !== 'undefined' && location.origin
  ? new URL('/drivers/apps/index.json', location.origin).href
  : '/drivers/apps/index.json';

export async function listInstalled() {
  const r = await fetch('/drv/list', { headers: authHeaders() });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  return r.json();
}

// يجلب الفهرس ويتحقق من شكله
export async function fetchStore(url) {
  const r = await fetch(url, { cache: 'no-store' });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  const j = await r.json();
  if (j?.version !== 1 || !Array.isArray(j.drivers)) throw new Error('bad index format');
  const okName = (x) => x && typeof x.name === 'string' && /^[A-Za-z0-9_]{1,24}$/.test(x.name) && typeof x.file === 'string';
  return {
    drivers: j.drivers.filter(okName),
    apps: (Array.isArray(j.apps) ? j.apps : []).filter(okName),
  };
}
export async function fetchIndex(url) { return (await fetchStore(url)).drivers; }

// حالة الدرايفر مقابل المثبّت: new | same | upd
export function compare(entry, installed) {
  const i = installed.find((x) => x.name === entry.name);
  return !i ? 'new' : (i.version === entry.version ? 'same' : 'upd');
}

async function sha256Hex(buf) {
  if (!globalThis.crypto?.subtle) return null;       // الصفحات على http غير آمنة ولا توفّر crypto.subtle
  const h = await crypto.subtle.digest('SHA-256', buf);
  return [...new Uint8Array(h)].map((b) => b.toString(16).padStart(2, '0')).join('');
}

// ينزّل ملف .drv ويفحص الحجم وsha256 (إن أمكن)
export async function download(entry, indexUrl) {
  const r = await fetch(new URL(entry.file, indexUrl).href, { cache: 'no-store' });
  if (!r.ok) throw new Error('HTTP ' + r.status);
  const buf = await r.arrayBuffer();
  if (entry.size && buf.byteLength !== entry.size) throw new Error('size mismatch');
  const h = await sha256Hex(buf);
  if (h && entry.sha256 && h !== entry.sha256) throw new Error('sha256 mismatch');
  return buf;
}

async function post(path, body) {
  const r = await fetch(path, { method: 'POST', headers: { ...authHeaders(), 'Content-Type': 'application/octet-stream' }, body });
  let j = null; try { j = await r.json(); } catch {}
  if (r.status === 401) { location.reload(); return { ok: false, error: 'unauthorized' }; }
  return { ok: r.ok && !!j?.ok, error: j?.error || (r.ok ? '' : 'HTTP ' + r.status) };
}

// يرسل ملف .drv (ArrayBuffer) للجهاز؛ الجهاز يتحقق من التوقيع قبل الحفظ
export const installToDevice = (buf) => post('/drv/install', buf);
export const removeFromDevice = (name) => post('/drv/remove?name=' + encodeURIComponent(name), '');

// تنزيل من المتجر ثم تثبيت
export async function installFromStore(entry, indexUrl) {
  return installToDevice(await download(entry, indexUrl));
}

// تطبيق = سكريبت Lua + صفحة HTML/canvas اختيارية + الدرايفرات التي يحتاجها.
// يُحفظ: /scripts/<name>.lua و/ui/<name>.html و/scripts/<name>.page (الربط). التشغيل بعدها من زر ▶.
export const appDrivers = (app) => (Array.isArray(app.drivers) && app.drivers.length ? app.drivers : app.driver ? [app.driver] : []);

async function save(path, content) {
  const dir = path.slice(0, path.lastIndexOf('/'));          // LittleFS لا ينشئ المجلد تلقائيًا: أنشئه (الخطأ إن وُجد يُتجاهل)
  if (dir) await fetch('/api/mkdir?path=' + encodeURIComponent(dir), { method: 'POST', headers: authHeaders() }).catch(() => {});
  const r = await fetch('/api/save?path=' + encodeURIComponent(path), {
    method: 'POST', headers: { ...authHeaders(), 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ content }),
  });
  if (!r.ok) throw new Error('save ' + path + ' HTTP ' + r.status);
}
async function fetchText(indexUrl, file, sha) {
  const r = await fetch(new URL(file, indexUrl).href, { cache: 'no-store' });
  if (!r.ok) throw new Error(file + ' HTTP ' + r.status);
  const buf = await r.arrayBuffer();
  if (sha && globalThis.crypto?.subtle && (await sha256Hex(new Uint8Array(buf))) !== sha) throw new Error(file + ': sha256 mismatch');
  return new TextDecoder().decode(buf);
}

export async function installApp(app, drivers, installed, indexUrl) {
  for (const dn of appDrivers(app)) {
    if (installed.some((i) => i.name === dn)) continue;
    const drv = drivers.find((d) => d.name === dn);
    if (!drv) return { ok: false, error: 'driver ' + dn + ' not in store' };
    const r = await installFromStore(drv, indexUrl);
    if (!r.ok) return r;
  }
  const code = await fetchText(indexUrl, app.file, app.sha256);
  const html = app.page_file ? await fetchText(indexUrl, app.page_file, app.page_sha256) : '';
  await save('/scripts/' + app.name + '.lua', code);
  if (html) {
    await save('/ui/' + app.name + '.html', html);
    await save('/scripts/' + app.name + '.page', '/ui/' + app.name + '.html');
  }
  return { ok: true, error: '' };
}

export async function getMode() {
  const r = await fetch('/drv/mode', { headers: authHeaders() });
  return r.ok ? r.json() : { mode: 'auto', exec: false };
}
export async function setMode(mode) {
  const r = await fetch('/drv/mode?mode=' + mode, { method: 'POST', headers: authHeaders() });
  return r.ok;
}
