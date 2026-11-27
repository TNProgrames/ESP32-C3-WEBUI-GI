import express from 'express';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createLuaScriptRuntime, luaMulti } from './sim/lua_runner.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PORT = Number(process.env.PORT) || 3000;
const IS_PROD = process.env.NODE_ENV === 'production';

// ---------- Load Store Index for Driver Metadata ----------
let storeIndex = { version: 1, drivers: [], apps: [] };
try {
  storeIndex = JSON.parse(fs.readFileSync(path.join(__dirname, 'drivers/apps/index.json'), 'utf8'));
} catch {}

// ---------- In-Memory LittleFS Simulation ----------
const TOTAL_FS_BYTES = 1536 * 1024; // 1.5 MB LittleFS partition
const vfsDirs = new Set(['/', '/scripts', '/scripts/lib', '/scripts/.data', '/ui', '/drivers']);
const vfsFiles = new Map();

function constTimeEq(a, b) {
  if (typeof a !== 'string' || typeof b !== 'string') return false;
  const ba = Buffer.from(a, 'utf8');
  const bb = Buffer.from(b, 'utf8');
  if (ba.length !== bb.length) return false;
  return crypto.timingSafeEqual(ba, bb);
}

function hashPassword(plain) {
  const salt = crypto.randomBytes(8).toString('hex');
  const hex = crypto.createHash('sha256').update(`${salt}:${plain}`, 'utf8').digest('hex');
  return `sha256$${salt}$${hex}`;
}

function verifyPassword(plain, stored) {
  if (typeof plain !== 'string' || typeof stored !== 'string' || !stored) return false;
  if (!stored.startsWith('sha256$')) return constTimeEq(plain, stored);
  const parts = stored.split('$');
  if (parts.length !== 3 || !parts[1] || parts[2].length !== 64) return false;
  const hex = crypto.createHash('sha256').update(`${parts[1]}:${plain}`, 'utf8').digest('hex');
  return constTimeEq(hex, parts[2]);
}

function seedDefaultFiles() {
  vfsDirs.clear();
  ['/','/scripts','/scripts/lib','/scripts/.data','/ui','/drivers'].forEach((d) => vfsDirs.add(d));
  vfsFiles.clear();
  vfsFiles.set('/settings.json', Buffer.from(JSON.stringify([{
    localhost: 'esp32.local',
    internethost: 'api.esp-controller.io',
    pinreset: '9',
    wifi: {
      mode: 'AP',
      ssid: 'ESP32_STA',
      pass: '12345678',
      ap_ssid: 'ESP32_AP',
      ap_pass: '12345678'
    }
  }], null, 2)));
  vfsFiles.set('/users.json', Buffer.from(JSON.stringify([
    { username: 'admin', password: hashPassword('admin123') },
    { username: 'user', password: hashPassword('user123') }
  ], null, 2)));
  vfsFiles.set('/wifi_nets.json', Buffer.from('[]'));
}
seedDefaultFiles();

function normPath(p) {
  if (!p || typeof p !== 'string') return null;
  let s = p.trim();
  if (!s.startsWith('/')) s = '/' + s;
  s = path.posix.normalize(s);
  if (s.includes('..')) return null;
  if (s.length > 1 && s.endsWith('/')) s = s.slice(0, -1);
  return s;
}
function ensureParentDirs(p) {
  const parts = p.split('/').filter(Boolean);
  let cur = '';
  for (let i = 0; i < parts.length - 1; i++) {
    cur += '/' + parts[i];
    vfsDirs.add(cur);
  }
}
function vfsUsedBytes() {
  let total = 0;
  for (const buf of vfsFiles.values()) total += buf.length;
  return total;
}
function vfsReadText(p) {
  const b = vfsFiles.get(p);
  return b ? b.toString('utf8') : '';
}
function vfsWrite(p, data) {
  ensureParentDirs(p);
  vfsFiles.set(p, Buffer.isBuffer(data) ? data : Buffer.from(String(data), 'utf8'));
}
function vfsDeleteRecursive(p) {
  if (p === '/') return false;
  let removed = false;
  if (vfsFiles.delete(p)) removed = true;
  if (vfsDirs.delete(p)) removed = true;
  const prefix = p + '/';
  for (const k of [...vfsFiles.keys()]) {
    if (k.startsWith(prefix)) { vfsFiles.delete(k); removed = true; }
  }
  for (const d of [...vfsDirs]) {
    if (d.startsWith(prefix)) { vfsDirs.delete(d); removed = true; }
  }
  return removed;
}
function vfsRename(oldP, newP) {
  if (!oldP || !newP || oldP === '/') return false;
  ensureParentDirs(newP);
  if (vfsFiles.has(oldP)) {
    vfsFiles.set(newP, vfsFiles.get(oldP));
    vfsFiles.delete(oldP);
    return true;
  }
  if (vfsDirs.has(oldP)) {
    vfsDirs.delete(oldP);
    vfsDirs.add(newP);
    const oldPre = oldP + '/';
    for (const k of [...vfsFiles.keys()]) {
      if (k.startsWith(oldPre)) {
        vfsFiles.set(newP + '/' + k.slice(oldPre.length), vfsFiles.get(k));
        vfsFiles.delete(k);
      }
    }
    return true;
  }
  return false;
}
function vfsList(dirPath) {
  const d = normPath(dirPath);
  if (!d || !vfsDirs.has(d)) return null;
  const prefix = d === '/' ? '/' : d + '/';
  const seenDirs = new Set();
  const entries = [];
  for (const dir of vfsDirs) {
    if (dir === d || !dir.startsWith(prefix)) continue;
    const rest = dir.slice(prefix.length);
    if (!rest.includes('/')) {
      seenDirs.add(rest);
      entries.push({ name: rest, isDir: true, size: 0 });
    }
  }
  for (const [f, buf] of vfsFiles.entries()) {
    if (!f.startsWith(prefix)) continue;
    const rest = f.slice(prefix.length);
    if (!rest.includes('/')) {
      entries.push({ name: rest, isDir: false, size: buf.length });
    }
  }
  entries.sort((a, b) => Number(b.isDir) - Number(a.isDir) || a.name.localeCompare(b.name));
  return entries;
}

// ---------- Device State & Helpers ----------
const FW_VERSION = '1.0.0-sim';
let bootAt = Date.now();
const uptimeSec = () => Math.floor((Date.now() - bootAt) / 1000);

function getSettings() {
  try { return JSON.parse(vfsReadText('/settings.json'))[0] || {}; } catch { return {}; }
}
function saveSettings(obj) {
  vfsWrite('/settings.json', JSON.stringify([obj], null, 2));
}
function getUsers() {
  try { return JSON.parse(vfsReadText('/users.json')); } catch { return []; }
}
function saveUsers(arr) {
  vfsWrite('/users.json', JSON.stringify(arr, null, 2));
}
function getNets() {
  try { return JSON.parse(vfsReadText('/wifi_nets.json')) || []; } catch { return []; }
}
function saveNets(arr) {
  vfsWrite('/wifi_nets.json', JSON.stringify(arr, null, 2));
}

// Ring buffer terminal log
const LOG_MAX = 32768;
let logSeq = 0;
let logBuffer = '';
function logPrintf(line) {
  logBuffer += line;
  logSeq += line.length;
  if (logBuffer.length > LOG_MAX) {
    logBuffer = logBuffer.slice(logBuffer.length - LOG_MAX);
  }
}
logPrintf('[boot] ESP32-C3 WebUI simulator ready\n');
logPrintf('[wifi] AP ESP32_AP active (192.168.4.1)\n');

// Auth & Emergency session
let adminToken = '';
let userToken = '';
let loginFails = 0;
let loginLockUntil = 0;
let resetWaitUntil = 0;
let resetDoneAt = 0;
let resetPin = 9;
let emSess = crypto.randomBytes(12).toString('hex');
let emFails = 0;
let emLockUntil = 0;

function parseCookies(req) {
  const out = {};
  const raw = req.headers.cookie || '';
  for (const part of raw.split(';')) {
    const idx = part.indexOf('=');
    if (idx > 0) out[part.slice(0, idx).trim()] = decodeURIComponent(part.slice(idx + 1).trim());
  }
  return out;
}
function accessKey() {
  const custom = vfsReadText('/em_key').trim();
  if (custom.startsWith('sha256$') || custom.length >= 4) return custom;
  return '1234';
}
function isAdminToken(req) {
  const tok = req.get('user_token') || '';
  return Boolean(tok && adminToken && constTimeEq(tok, adminToken));
}
function isAnyToken(req) {
  const tok = req.get('user_token') || '';
  return Boolean(tok && ((adminToken && constTimeEq(tok, adminToken)) || (userToken && constTimeEq(tok, userToken))));
}
function isAuthorizedEm(req) {
  const cookies = parseCookies(req);
  return Boolean(cookies.session_key_em && constTimeEq(cookies.session_key_em, emSess));
}
function refreshEmCookie(res) {
  res.cookie('session_key_em', emSess, { path: '/', httpOnly: true, maxAge: 900000, sameSite: 'strict' });
}

// ---------- Multipart Parser (for em.html & Ota.svelte) ----------
function parseMultipart(contentType, buf) {
  const m = /boundary=([^;\s]+)/i.exec(contentType || '');
  if (!m) return {};
  const boundary = m[1].replace(/^"|"$/g, '');
  const delim = Buffer.from('--' + boundary);
  const fields = {};
  let pos = buf.indexOf(delim);
  while (pos !== -1) {
    pos += delim.length;
    if (buf[pos] === 45 && buf[pos + 1] === 45) break; // -- end
    if (buf[pos] === 13 && buf[pos + 1] === 10) pos += 2;
    const hdrEnd = buf.indexOf('\r\n\r\n', pos);
    if (hdrEnd === -1) break;
    const hdrStr = buf.slice(pos, hdrEnd).toString('utf8');
    const bodyStart = hdrEnd + 4;
    const nextPos = buf.indexOf(delim, bodyStart);
    if (nextPos === -1) break;
    let bodyEnd = nextPos;
    if (buf[bodyEnd - 2] === 13 && buf[bodyEnd - 1] === 10) bodyEnd -= 2;
    const bodyBuf = buf.slice(bodyStart, bodyEnd);
    const nameMatch = /name="([^"]+)"/i.exec(hdrStr);
    const fileMatch = /filename="([^"]*)"/i.exec(hdrStr);
    if (nameMatch) {
      fields[nameMatch[1]] = {
        filename: fileMatch ? fileMatch[1] : undefined,
        data: bodyBuf,
        text: bodyBuf.toString('utf8')
      };
    }
    pos = nextPos;
  }
  return fields;
}

// ---------- Drivers Subsystem ----------
const DRV_MAX_LOADED = 4;
let drvMode = 'auto';
const loadedDrivers = new Map(); // name -> { name, version, size, mem, users, info, mode, methods }

function parseDrvHeader(buf) {
  if (!buf || buf.length < 128) return { error: 'bad size' };
  if (buf.subarray(0, 4).toString('ascii') !== 'ESPD') return { error: 'bad magic' };
  const fmt = buf.readUInt16LE(4);
  const api = buf.readUInt16LE(6);
  if (fmt !== 1) return { error: 'bad format' };
  if (api !== 2) return { error: 'api version mismatch' };
  const imgLen = buf.readUInt32LE(12);
  const memLen = buf.readUInt32LE(16);
  const nrel = buf.readUInt32LE(20);
  if (64 + imgLen + 4 * nrel + 64 !== buf.length) return { error: 'size mismatch' };
  const rawName = buf.subarray(24, 40);
  const name = rawName.subarray(0, rawName.indexOf(0) === -1 ? 16 : rawName.indexOf(0)).toString('ascii');
  const rawVer = buf.subarray(40, 48);
  const version = rawVer.subarray(0, rawVer.indexOf(0) === -1 ? 8 : rawVer.indexOf(0)).toString('ascii');
  if (!/^[A-Za-z0-9]{1,15}$/.test(name)) return { error: 'bad name' };
  const idxEntry = (storeIndex.drivers || []).find((d) => d.name === name);
  return {
    name,
    version,
    size: buf.length,
    mem: memLen,
    info: idxEntry?.info || `${name} driver`,
    methods: idxEntry?.methods || []
  };
}

function createDriverInstance(drvName, cfg = {}) {
  const entry = loadedDrivers.get(drvName);
  if (!entry) return luaMulti(null, `driver '${drvName}' not installed`);
  entry.users++;
  let closed = false;
  const t0 = Date.now();
  let pcfByte = 0xff;
  let rtcOffsetMs = 0;

  const close = () => {
    if (!closed) {
      closed = true;
      entry.users = Math.max(0, entry.users - 1);
    }
  };

  const base = { close };
  const sec = () => (Date.now() - t0) / 1000;

  if (drvName === 'aht20' || drvName === 'sht31') {
    return Object.assign(base, {
      read: () => luaMulti(Math.round((24.2 + Math.sin(sec() * 0.4) * 1.8) * 100), Math.round((48.5 + Math.cos(sec() * 0.3) * 4.5) * 100)),
      info: () => `${drvName.toUpperCase()} simulated`,
      reset: () => true
    });
  }
  if (drvName === 'bme280') {
    return Object.assign(base, {
      read: () => luaMulti(
        Math.round((23.8 + Math.sin(sec() * 0.35) * 1.5) * 100),
        Math.round((101325 + Math.sin(sec() * 0.1) * 45)),
        Math.round((50.2 + Math.cos(sec() * 0.25) * 3.0) * 100)
      ),
      chip_id: () => 0x60,
      info: () => 'BME280 simulated'
    });
  }
  if (drvName === 'bh1750') {
    return Object.assign(base, {
      read: () => Math.max(1, Math.round(420 + 280 * Math.sin(sec() * 0.5))),
      info: () => 'BH1750 simulated'
    });
  }
  if (drvName === 'ads1115') {
    let gain = cfg?.gain ?? 1;
    return Object.assign(base, {
      gain: (g) => { gain = g; },
      read: (ch = 0) => Math.round(1650 + 950 * Math.sin(sec() * 2.2 + ch) / Math.max(1, gain * 0.5))
    });
  }
  if (drvName === 'ina219') {
    return Object.assign(base, {
      read: () => {
        const mv = Math.round(4980 + 40 * Math.sin(sec() * 0.8));
        const ma = Math.round(240 + 60 * Math.sin(sec() * 1.1));
        const mw = Math.round((mv * ma) / 1000);
        return luaMulti(mv, ma, mw);
      }
    });
  }
  if (drvName === 'ds3231') {
    return Object.assign(base, {
      read: () => {
        const d = new Date(Date.now() + rtcOffsetMs);
        return luaMulti(d.getFullYear(), d.getMonth() + 1, d.getDate(), d.getHours(), d.getMinutes(), d.getSeconds());
      },
      set: (y, mo, day, h, mi, s) => {
        const target = new Date(y, mo - 1, day, h, mi, s).getTime();
        rtcOffsetMs = target - Date.now();
        return luaMulti(true);
      },
      temp: () => Math.round((27.5 + Math.sin(sec() * 0.2) * 0.5) * 100)
    });
  }
  if (drvName === 'mpu6050') {
    return Object.assign(base, {
      read: () => luaMulti(
        Math.round(Math.sin(sec() * 0.9) * 220),
        Math.round(Math.cos(sec() * 0.7) * 180),
        980
      ),
      temp: () => 2850
    });
  }
  if (drvName === 'pcf8574') {
    return Object.assign(base, {
      read: () => pcfByte,
      write: (v) => { pcfByte = v & 0xff; },
      pin: (n, on) => { if (on) pcfByte |= (1 << n); else pcfByte &= ~(1 << n); },
      toggle: (n) => { pcfByte ^= (1 << n); }
    });
  }
  if (drvName === 'pca9685') {
    return Object.assign(base, {
      freq: () => {},
      pwm: () => {},
      servo: (ch, deg) => {
        if (ch < 0 || ch > 15 || deg < 0 || deg > 180) return luaMulti(null, 'bad arg');
        return luaMulti(true);
      },
      off: () => {}
    });
  }
  // Display drivers (lcd1602, ssd1306, st7789)
  return Object.assign(base, {
    clear: () => {},
    show: () => {},
    text: () => {},
    cursor: () => {},
    backlight: () => {},
    pixel: () => {},
    line: () => {},
    rect: () => {},
    circle: () => {},
    bar: () => {},
    invert: () => {},
    contrast: () => {}
  });
}

// ---------- Hardware, Bus, Storage & Lua Script Engine ----------
const MAX_SCRIPTS = 3;
const runningScripts = new Map(); // name -> { name, state, mem, startMs, err, runtime, panelIds, gpios }
const panels = new Map(); // id -> { id, script, title, page, widgets: [], pageListeners: Map }
let nextPanelId = 0;
let uiVer = 0;
let msgSeq = 0;
const uiMsgs = []; // { seq, p, n, v }
const MAX_UI_MSGS = 64;

const gpioValues = new Map();
const gpioWatchers = new Map();
const hardware = {
  bootAt,
  version: FW_VERSION,
  uptimeSec,
  gpioMode: (pin) => { if (!gpioValues.has(pin)) gpioValues.set(pin, 0); },
  gpioWrite: (pin, val) => { gpioValues.set(pin, val ? 1 : 0); },
  gpioRead: (pin) => gpioValues.get(pin) ?? 0,
  gpioToggle: (pin) => { const v = (gpioValues.get(pin) ?? 0) ? 0 : 1; gpioValues.set(pin, v); return v; },
  gpioOnChange: (scriptName, pin, cb) => { gpioWatchers.set(`${scriptName}:${pin}`, cb); },
  pwmSet: () => {},
  pwmDuty: () => {},
  pwmStop: () => {},
  i2cBegin: () => {}
};

const busSubs = new Map(); // topic -> Set<{ scriptName, cb }>
function busApiFor(scriptName) {
  return {
    publish(topic, val) {
      const subs = busSubs.get(topic);
      if (!subs) return;
      for (const s of subs) {
        if (s.scriptName !== scriptName) {
          queueMicrotask(() => { try { s.cb(val); } catch {} });
        }
      }
    },
    subscribe(topic, cb) {
      if (!busSubs.has(topic)) busSubs.set(topic, new Set());
      busSubs.get(topic).add({ scriptName, cb });
    },
    unsubscribe(topic) {
      const subs = busSubs.get(topic);
      if (!subs) return;
      for (const s of [...subs]) if (s.scriptName === scriptName) subs.delete(s);
    }
  };
}

function storageApiFor(scriptName) {
  const kvPath = `/scripts/.data/${scriptName}.json`;
  const loadKv = () => {
    try { return JSON.parse(vfsReadText(kvPath)) || {}; } catch { return {}; }
  };
  const saveKv = (obj) => vfsWrite(kvPath, JSON.stringify(obj));
  return {
    get(k, def = null) {
      const kv = loadKv();
      return k in kv ? kv[k] : def;
    },
    set(k, v) {
      const kv = loadKv();
      if (v === null || v === undefined) delete kv[k];
      else kv[k] = v;
      saveKv(kv);
    },
    remove(k) {
      const kv = loadKv();
      delete kv[k];
      saveKv(kv);
    },
    clear() {
      saveKv({});
    }
  };
}

function pushUiMsg(panelId, wid, val) {
  msgSeq++;
  uiMsgs.push({ seq: msgSeq, p: panelId, n: wid, v: val });
  if (uiMsgs.length > MAX_UI_MSGS) uiMsgs.shift();
}

function createPanelFor(scriptObj, title) {
  const id = nextPanelId++;
  const panel = {
    id,
    script: scriptObj.name,
    title: String(title || scriptObj.name),
    page: '',
    widgets: [],
    callbacks: new Map(),
    pageListeners: new Map()
  };
  panels.set(id, panel);
  scriptObj.panelIds.push(id);
  uiVer++;

  const addWidget = (type, wid, label, extra = {}) => {
    const w = { id: String(wid), type, label: String(label ?? ''), min: 0, max: 100, value: null, ...extra };
    panel.widgets.push(w);
    uiVer++;
    return w;
  };

  return {
    page(p) {
      let clean = String(p || '');
      if (!clean.startsWith('/')) clean = '/' + clean;
      panel.page = clean;
      uiVer++;
    },
    label(wid, val = '') { addWidget('label', wid, '', { value: val }); },
    button(wid, label, cb) {
      addWidget('button', wid, label, { value: false });
      if (typeof cb === 'function') panel.callbacks.set(wid, cb);
    },
    switch(wid, label, cb, val = false) {
      addWidget('switch', wid, label, { value: Boolean(val) });
      if (typeof cb === 'function') panel.callbacks.set(wid, cb);
    },
    slider(wid, label, min, max, cb, val = min) {
      addWidget('slider', wid, label, { min: Number(min), max: Number(max), value: Number(val ?? min) });
      if (typeof cb === 'function') panel.callbacks.set(wid, cb);
    },
    progress(wid, label, val = 0, max = 100) {
      addWidget('progress', wid, label, { min: 0, max: Number(max), value: Number(val) });
    },
    input(wid, label, cb, val = '') {
      addWidget('input', wid, label, { value: String(val ?? '') });
      if (typeof cb === 'function') panel.callbacks.set(wid, cb);
    },
    set(wid, val) {
      const w = panel.widgets.find((x) => x.id === wid);
      if (w) w.value = val;
      pushUiMsg(id, wid, val);
    },
    emit(evtName, val) {
      pushUiMsg(id, String(evtName), val);
    },
    on(evtName, cb) {
      if (typeof cb === 'function') panel.pageListeners.set(String(evtName), cb);
    }
  };
}

function getAutoList() {
  return new Set(vfsReadText('/scripts/.autorun').split('\n').map((s) => s.trim()).filter(Boolean));
}
function setAutoList(set) {
  vfsWrite('/scripts/.autorun', [...set].join('\n') + (set.size ? '\n' : ''));
}

function stopScript(name) {
  const s = runningScripts.get(name);
  if (!s) return false;
  try { s.runtime?.stop(); } catch {}
  for (const pid of s.panelIds) {
    if (panels.delete(pid)) uiVer++;
  }
  for (const subs of busSubs.values()) {
    for (const sub of [...subs]) if (sub.scriptName === name) subs.delete(sub);
  }
  for (const k of [...gpioWatchers.keys()]) {
    if (k.startsWith(name + ':')) gpioWatchers.delete(k);
  }
  runningScripts.delete(name);
  logPrintf(`[${name}] stopped\n`);
  return true;
}

function stopAllScripts() {
  for (const name of [...runningScripts.keys()]) stopScript(name);
}

function runScript(name, code) {
  if (!/^[A-Za-z0-9_-]{1,23}$/.test(name)) return false;
  if (runningScripts.has(name)) stopScript(name);
  if (runningScripts.size >= MAX_SCRIPTS) return false;

  const scriptObj = {
    name,
    state: 'running',
    mem: 6144 + Math.min(24000, code.length * 6),
    startMs: Date.now(),
    err: '',
    panelIds: [],
    runtime: null
  };
  runningScripts.set(name, scriptObj);
  logPrintf(`[${name}] started\n`);

  const rt = createLuaScriptRuntime({
    name,
    code,
    log: logPrintf,
    uiApi: { panel: (title) => createPanelFor(scriptObj, title) },
    hardware,
    requireLib: (libName) => {
      const libCode = vfsReadText(`/scripts/lib/${libName}.lua`);
      if (!libCode) throw new Error(`module '${libName}' not found (/scripts/lib/${libName}.lua)`);
      let modVal = true;
      const libRt = createLuaScriptRuntime({
        name: `lib/${libName}`,
        code: `local __ret = (function()\n${libCode}\nend)()\n__export(__ret)`,
        log: logPrintf,
        uiApi: { panel: (title) => createPanelFor(scriptObj, title) },
        hardware,
        requireLib: () => true,
        openDriver: createDriverInstance,
        storageApi: storageApiFor,
        busApi: busApiFor,
        onError: (err) => { throw new Error(err); }
      });
      libRt.env.__export = (v) => { if (v !== null && v !== undefined) modVal = v; };
      libRt.start();
      return modVal;
    },
    openDriver: createDriverInstance,
    storageApi: storageApiFor,
    busApi: busApiFor,
    onError: (errMsg) => {
      scriptObj.state = 'error';
      const formatted = errMsg.startsWith(':') ? `[string "${name}"]${errMsg}` : `[string "${name}"]:1: ${errMsg}`;
      scriptObj.err = formatted;
      logPrintf(`[${name}] error: ${formatted}\n`);
    }
  });
  scriptObj.runtime = rt;
  rt.start();
  return true;
}

// ---------- Express App Setup ----------
const app = express();

// Serve drivers/apps locally for the App & Driver Store
app.use('/drivers/apps', express.static(path.join(__dirname, 'drivers/apps')));

// Raw body middleware for specific binary/text/multipart routes
const rawBody = express.raw({ type: '*/*', limit: '4mb' });
const jsonBody = express.json({ limit: '1mb' });

// --- Emergency Manager (/em, /em/check, /api/*) ---
app.get('/em', (req, res) => {
  if (isAuthorizedEm(req)) {
    refreshEmCookie(res);
    return res.status(200).type('html').send(fs.readFileSync(path.join(__dirname, 'web/em.html'), 'utf8'));
  }
  return res.status(401).type('html').send(fs.readFileSync(path.join(__dirname, 'web/em_login.html'), 'utf8'));
});

app.post('/em/check', rawBody, (req, res) => {
  if (emLockUntil && Date.now() < emLockUntil) return res.sendStatus(429);
  const ct = req.get('content-type') || '';
  let pass = '';
  if (ct.includes('application/x-www-form-urlencoded')) {
    pass = new URLSearchParams(req.body.toString('utf8')).get('pass') || '';
  }
  if (pass && verifyPassword(pass, accessKey())) {
    emFails = 0; emLockUntil = 0;
    refreshEmCookie(res);
    return res.sendStatus(200);
  }
  if (++emFails >= 5) { emFails = 0; emLockUntil = Date.now() + 60000; }
  return res.sendStatus(401);
});

app.post('/em/logout', (_req, res) => {
  res.setHeader('Set-Cookie', 'session_key_em=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict');
  res.sendStatus(200);
});

function emOrAdmin(req, res) {
  if (isAdminToken(req) || isAuthorizedEm(req)) return true;
  res.sendStatus(403);
  return false;
}

app.get('/api/list', (req, res) => {
  if (!emOrAdmin(req, res)) return;
  const p = normPath(String(req.query.path || '/'));
  const files = vfsList(p);
  if (!files) return res.sendStatus(400);
  res.json({ total: TOTAL_FS_BYTES, used: vfsUsedBytes(), files });
});

app.get('/api/read', (req, res) => {
  if (!emOrAdmin(req, res)) return;
  const p = normPath(String(req.query.path || ''));
  if (!p || !vfsFiles.has(p)) return res.sendStatus(400);
  res.type('text/plain').send(vfsFiles.get(p).toString('utf8'));
});

app.get('/api/download', (req, res) => {
  if (!emOrAdmin(req, res)) return;
  const p = normPath(String(req.query.path || ''));
  if (!p || !vfsFiles.has(p)) return res.sendStatus(400);
  res.type('application/octet-stream').send(vfsFiles.get(p));
});

app.post('/api/mkdir', (req, res) => {
  if (!emOrAdmin(req, res)) return;
  const p = normPath(String(req.query.path || ''));
  if (!p) return res.sendStatus(400);
  ensureParentDirs(p + '/_');
  vfsDirs.add(p);
  res.sendStatus(200);
});

app.post('/api/save', rawBody, (req, res) => {
  if (!emOrAdmin(req, res)) return;
  const p = normPath(String(req.query.path || ''));
  if (!p) return res.sendStatus(400);
  const ct = req.get('content-type') || '';
  let content = '';
  if (ct.includes('multipart/form-data')) {
    const fields = parseMultipart(ct, req.body);
    if (!fields.content) return res.sendStatus(400);
    content = fields.content.text;
  } else if (ct.includes('application/x-www-form-urlencoded')) {
    const params = new URLSearchParams(req.body.toString('utf8'));
    if (!params.has('content')) return res.sendStatus(400);
    content = params.get('content');
  } else {
    content = req.body.toString('utf8');
  }
  vfsWrite(p, content);
  res.sendStatus(200);
});

app.post('/api/upload', rawBody, (req, res) => {
  if (!emOrAdmin(req, res)) return;
  const ct = req.get('content-type') || '';
  const fields = parseMultipart(ct, req.body);
  const f = fields.file;
  if (!f || !f.filename) return res.sendStatus(400);
  const p = normPath(f.filename);
  if (!p) return res.sendStatus(400);
  vfsWrite(p, f.data);
  res.sendStatus(200);
});

app.delete('/api/delete', (req, res) => {
  if (!emOrAdmin(req, res)) return;
  const p = normPath(String(req.query.path || ''));
  if (!p || p === '/') return res.sendStatus(400);
  res.sendStatus(vfsDeleteRecursive(p) ? 200 : 500);
});

app.post('/api/rename', (req, res) => {
  if (!emOrAdmin(req, res)) return;
  const oldP = normPath(String(req.query.old || ''));
  const newP = normPath(String(req.query.new || ''));
  if (!oldP || !newP) return res.sendStatus(400);
  vfsRename(oldP, newP);
  res.sendStatus(200);
});

app.post('/api/format', (req, res) => {
  if (!emOrAdmin(req, res)) return;
  stopAllScripts();
  loadedDrivers.clear();
  seedDefaultFiles();
  res.sendStatus(200);
});

app.post('/api/ota', rawBody, (req, res) => {
  const isFs = req.query.type === 'fs';
  const okAuth = isAdminToken(req) || (isFs && isAuthorizedEm(req));
  if (!okAuth) return res.sendStatus(401);
  const ct = req.get('content-type') || '';
  const fields = parseMultipart(ct, req.body);
  if (!fields.file) return res.status(400).json({ error: 'no file' });
  if (!isFs && fields.file.data[0] !== 0xe9) {
    return res.status(500).json({ error: 'not a firmware image' });
  }
  if (isFs) {
    stopAllScripts();
    loadedDrivers.clear();
    seedDefaultFiles();
  }
  bootAt = Date.now();
  logPrintf(`[ota] ${isFs ? 'filesystem' : 'firmware'} updated, restarting\n`);
  res.json({ ok: true });
});

// --- Auth & Login ---
app.post('/login', jsonBody, (req, res) => {
  const { username, password } = req.body || {};
  if (!username || !password) return res.sendStatus(400);
  if (loginLockUntil && Date.now() < loginLockUntil) {
    return res.status(429).json({ error: 'locked' });
  }
  const users = getUsers();
  const idx = username === 'admin' ? 0 : username === 'user' ? 1 : -1;
  if (idx !== -1 && users[idx] && verifyPassword(String(password), users[idx].password)) {
    if (!String(users[idx].password).startsWith('sha256$')) {
      users[idx].password = hashPassword(String(password));
      saveUsers(users);
    }
    const token = crypto.randomBytes(16).toString('hex');
    if (idx === 0) adminToken = token;
    else userToken = token;
    loginFails = 0; loginLockUntil = 0;
    logPrintf(`Login ok: ${username}\n`);
    return res.json({ token, role: username });
  }
  logPrintf(`Login failed: ${username}\n`);
  if (++loginFails >= 5) { loginFails = 0; loginLockUntil = Date.now() + 30000; }
  return res.sendStatus(401);
});

app.post('/login/reset_request', (_req, res) => {
  const st = getSettings();
  resetPin = Number(st.pinreset) || 9;
  resetWaitUntil = Date.now() + 30000;
  resetDoneAt = Date.now() + 2500; // Simulate physical BOOT button press after 2.5s
  logPrintf(`Password reset requested: press GPIO${resetPin} within 30s\n`);
  res.json({ pin: resetPin, seconds: 30 });
});

app.get('/login/reset_status', (_req, res) => {
  const now = Date.now();
  if (resetDoneAt && now >= resetDoneAt) {
    resetWaitUntil = 0;
    resetDoneAt = 0;
    saveUsers([
      { username: 'admin', password: hashPassword('admin123') },
      { username: 'user', password: hashPassword('user123') }
    ]);
    adminToken = '';
    userToken = '';
    logPrintf('Passwords reset to defaults\n');
    return res.json({ state: 'done', admin: 'admin123', user: 'user123' });
  }
  if (resetWaitUntil && now < resetWaitUntil) {
    return res.json({ state: 'waiting', left: Math.ceil((resetWaitUntil - now) / 1000), pin: resetPin });
  }
  return res.json({ state: 'idle' });
});

// --- Dashboard & Settings ---
app.get('/hdusage', (req, res) => {
  if (!isAnyToken(req)) return res.sendStatus(401);
  const ramPct = (36.5 + Math.sin(Date.now() / 4000) * 2.2).toFixed(1);
  const storPct = ((vfsUsedBytes() / TOTAL_FS_BYTES) * 100).toFixed(1);
  const temp = Number((40.5 + Math.sin(Date.now() / 6000) * 1.4).toFixed(1));
  res.json({
    RamUsage: ramPct,
    StorageUsage: storPct,
    Version: FW_VERSION,
    Uptime: uptimeSec(),
    Temp: temp,
    Wake: 'power',
    WakePin: -1
  });
});

app.get('/settings/get_stat', (_req, res) => {
  const st = getSettings();
  const mode = st.wifi?.mode || 'AP';
  const nets = getNets();
  const connected = mode !== 'AP' && nets.length > 0;
  res.status(200).json({
    ssid: connected ? nets[0].ssid : '',
    ip: connected ? '192.168.1.42' : '192.168.4.1',
    status: connected ? 'CONNECTED' : 'DISCONNECTED',
    rssi: connected ? -54 : 0
  });
});

app.get('/settings/scan_wifi', (_req, res) => {
  res.status(200).json([
    { ssid: 'Home_WiFi_5G', rssi: -48, secure: true },
    { ssid: 'ESP32_Lab', rssi: -62, secure: true },
    { ssid: 'Guest_Network', rssi: -74, secure: false }
  ]);
});

app.get('/settings/get_wifi', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const st = getSettings();
  res.json({
    mode: st.wifi?.mode || 'AP',
    ap_ssid: st.wifi?.ap_ssid || 'ESP32_AP',
    ap_pass: st.wifi?.ap_pass || '12345678',
    nets: getNets()
  });
});

app.post('/settings/save_wifi', jsonBody, (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const d = req.body || {};
  const st = getSettings();
  if (Array.isArray(d.nets)) {
    const clean = d.nets.filter((n) => n?.ssid).slice(0, 6).map((n) => ({ ssid: String(n.ssid), pass: String(n.pass || '') }));
    saveNets(clean);
    d.ssid = clean[0]?.ssid || '';
    d.pass = clean[0]?.pass || '';
  }
  st.wifi = {
    mode: d.mode || st.wifi?.mode || 'AP',
    ssid: d.ssid ?? st.wifi?.ssid ?? '',
    pass: d.pass ?? st.wifi?.pass ?? '',
    ap_ssid: d.ap_ssid ?? st.wifi?.ap_ssid ?? 'ESP32_AP',
    ap_pass: d.ap_pass ?? st.wifi?.ap_pass ?? '12345678'
  };
  saveSettings(st);
  logPrintf(`[wifi] mode=${st.wifi.mode} applied\n`);
  res.sendStatus(200);
});

app.get('/settings/get_comm', (req, res) => {
  if (!isAnyToken(req)) return res.sendStatus(401);
  const st = getSettings();
  res.json({ localhost: st.localhost || 'esp32.local', host: st.internethost || 'api.esp-controller.io' });
});

app.post('/settings/save_comm', jsonBody, (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const { localhost, host } = req.body || {};
  if (localhost && !/^[a-z0-9]([a-z0-9-]*[a-z0-9])?(\.local)?$/i.test(localhost)) return res.sendStatus(400);
  const st = getSettings();
  if (localhost !== undefined) st.localhost = localhost.endsWith('.local') ? localhost : `${localhost}.local`;
  if (host !== undefined) st.internethost = host;
  saveSettings(st);
  res.sendStatus(200);
});

app.get('/settings/get_pin', (req, res) => {
  if (!isAnyToken(req)) return res.sendStatus(401);
  res.json({ pin: String(getSettings().pinreset || '9') });
});

app.post('/settings/save_pin', jsonBody, (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const st = getSettings();
  st.pinreset = String(req.body?.pin ?? '9');
  saveSettings(st);
  res.sendStatus(200);
});

app.get('/settings/get_pass', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const u = getUsers();
  res.json({ hasAp: Boolean(u[0]?.password), hasUp: Boolean(u[1]?.password), hasEm: vfsFiles.has('/em_key'), ap: '', up: '', em: '' });
});

app.post('/settings/save_pass', jsonBody, (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const ap = typeof req.body?.ap === 'string' ? req.body.ap : '';
  const up = typeof req.body?.up === 'string' ? req.body.up : '';
  const em = typeof req.body?.em === 'string' ? req.body.em : '';
  if (!ap && !up && !em) return res.sendStatus(400);
  if (ap.length > 64 || up.length > 64) return res.sendStatus(400);
  if (em && (em.length < 4 || em.length > 31)) return res.sendStatus(400);
  const u = getUsers();
  if (ap) u[0] = { username: 'admin', password: hashPassword(ap) };
  if (up) u[1] = { username: 'user', password: hashPassword(up) };
  if (ap || up) saveUsers(u);
  if (em) {
    vfsWrite('/em_key', hashPassword(em));
    emSess = crypto.randomBytes(12).toString('hex');
  }
  res.sendStatus(200);
});

app.get('/settings/backup', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  res.setHeader('Content-Disposition', 'attachment; filename=esp32-backup.json');
  const payload = { version: 1, settings: JSON.parse(vfsReadText('/settings.json')), users: getUsers(), nets: getNets() };
  const emKey = vfsReadText('/em_key').trim();
  if (emKey) payload.em_key = emKey;
  res.json(payload);
});

app.post('/settings/restore', jsonBody, (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const { settings, users, nets, em_key: emKey } = req.body || {};
  if (!Array.isArray(settings) || settings.length < 1 || !Array.isArray(users) || users.length < 2
      || typeof users[0]?.username !== 'string' || typeof users[0]?.password !== 'string' || !users[0].password
      || typeof users[1]?.username !== 'string' || typeof users[1]?.password !== 'string' || !users[1].password) {
    return res.status(400).json({ error: 'invalid backup' });
  }
  if (typeof emKey === 'string' && emKey.trim()) {
    const k = emKey.trim();
    if (!k.startsWith('sha256$') && (k.length < 4 || k.length > 31)) {
      return res.status(400).json({ error: 'invalid backup' });
    }
    vfsWrite('/em_key', k.startsWith('sha256$') ? k : hashPassword(k));
  }
  const hashedUsers = users.map((entry, i) => i < 2 ? {
    username: entry.username,
    password: entry.password.startsWith('sha256$') ? entry.password : hashPassword(entry.password)
  } : entry);
  vfsWrite('/settings.json', JSON.stringify(settings, null, 2));
  saveUsers(hashedUsers);
  if (Array.isArray(nets)) {
    const clean = nets.filter((n) => n?.ssid && String(n.ssid).length <= 32 && String(n.pass || '').length <= 63)
      .slice(0, 6).map((n) => ({ ssid: String(n.ssid), pass: String(n.pass || '') }));
    saveNets(clean);
  } else {
    vfsDeleteRecursive('/wifi_nets.json');
  }
  bootAt = Date.now();
  res.json({ ok: true });
});

app.post('/settings/restart', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  logPrintf('Restart requested\n');
  bootAt = Date.now();
  res.json({ ok: true });
});

app.post('/settings/sleep', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  logPrintf(`[sleep] deep sleep secs=${req.query.secs || 0} pins=${req.query.pins || ''}\n`);
  res.json({ ok: true });
});

// --- Terminal ---
app.get('/terminal/log', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const since = Number(req.query.since) || 0;
  const oldest = Math.max(0, logSeq - logBuffer.length);
  const start = since > logSeq || since < oldest ? oldest : since;
  const text = logBuffer.slice(start - oldest);
  res.json({ seq: logSeq, text });
});

app.post('/terminal/cmd', jsonBody, (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const cmd = String(req.body?.cmd || '').trim();
  const [op, ...rest] = cmd.split(/\s+/);
  let out = '';
  if (op === 'help') {
    out = 'commands: help, version, uptime, heap, wifi, ls [path], df, echo <text>, scripts, run <name>, stop <name|all>, drivers, reboot';
  } else if (op === 'version') {
    out = FW_VERSION;
  } else if (op === 'uptime') {
    out = `${uptimeSec()}s`;
  } else if (op === 'heap') {
    out = `free: ${Math.round(180000 - Math.random() * 4000)} bytes`;
  } else if (op === 'wifi') {
    const st = getSettings();
    out = `mode=${st.wifi?.mode || 'AP'} ap=${st.wifi?.ap_ssid || 'ESP32_AP'} ip=192.168.4.1`;
  } else if (op === 'ls') {
    const list = vfsList(rest[0] || '/');
    out = list ? list.map((f) => `${f.isDir ? '[DIR]' : '     '} ${f.name.padEnd(20)} ${f.size}B`).join('\n') : 'not a directory';
  } else if (op === 'df') {
    out = `used: ${vfsUsedBytes()} / ${TOTAL_FS_BYTES} bytes`;
  } else if (op === 'echo') {
    out = rest.join(' ');
  } else if (op === 'scripts') {
    out = runningScripts.size
      ? [...runningScripts.values()].map((s) => `${s.name}  ${s.state}  ${s.mem}B${s.err ? '  ' + s.err : ''}`).join('\n')
      : '(no scripts)';
  } else if (op === 'run' && rest[0]) {
    const code = vfsReadText(`/scripts/${rest[0]}.lua`);
    out = code && runScript(rest[0], code) ? `started ${rest[0]}` : 'failed';
  } else if (op === 'stop' && rest[0]) {
    if (rest[0] === 'all') { stopAllScripts(); out = 'stopped all'; }
    else out = stopScript(rest[0]) ? `stopped ${rest[0]}` : 'not running';
  } else if (op === 'drivers') {
    out = loadedDrivers.size
      ? [...loadedDrivers.values()].map((d) => `${d.name} v${d.version}  file=${d.size}B ram=${d.mem}B users=${d.users}`).join('\n')
      : 'no drivers';
  } else if (op === 'reboot') {
    bootAt = Date.now();
    out = 'restarting...';
  } else {
    out = `unknown command: ${op} (try 'help')`;
  }
  res.json({ out });
});

// --- Drivers ---
app.get('/drv/mode', (req, res) => {
  if (!isAnyToken(req)) return res.sendStatus(401);
  res.json({ mode: drvMode, exec: true });
});

app.post('/drv/mode', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const m = String(req.query.mode || '');
  if (!['auto', 'vm', 'native'].includes(m)) return res.status(400).json({ ok: false, error: 'bad mode' });
  drvMode = m;
  for (const d of loadedDrivers.values()) {
    if (d.users === 0) d.mode = m === 'vm' ? 'vm' : 'native';
  }
  res.json({ ok: true });
});

app.get('/drv/list', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  res.json([...loadedDrivers.values()]);
});

app.post('/drv/install', rawBody, (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const parsed = parseDrvHeader(req.body);
  if (parsed.error) return res.status(400).json({ ok: false, error: parsed.error });
  const existing = loadedDrivers.get(parsed.name);
  if (existing && existing.users > 0) return res.status(400).json({ ok: false, error: 'driver in use' });
  if (!existing && loadedDrivers.size >= DRV_MAX_LOADED) return res.status(400).json({ ok: false, error: 'too many drivers' });
  vfsWrite(`/drivers/${parsed.name}.drv`, req.body);
  loadedDrivers.set(parsed.name, {
    ...parsed,
    users: 0,
    mode: drvMode === 'vm' ? 'vm' : 'native'
  });
  logPrintf(`[drv] installed ${parsed.name} v${parsed.version}\n`);
  res.json({ ok: true });
});

app.post('/drv/remove', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const name = String(req.query.name || '');
  const d = loadedDrivers.get(name);
  if (d && d.users > 0) return res.status(400).json({ ok: false, error: 'driver in use' });
  loadedDrivers.delete(name);
  vfsDeleteRecursive(`/drivers/${name}.drv`);
  res.json({ ok: true });
});

// --- Lua Scripts & UI Panels ---
app.get('/script/list', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const out = [...runningScripts.values()].map((s) => ({
    name: s.name,
    state: s.state,
    mem: s.mem,
    up: Math.floor((Date.now() - s.startMs) / 1000),
    ...(s.err ? { err: s.err } : {})
  }));
  res.json(out);
});

app.get('/script/files', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const auto = getAutoList();
  const files = (vfsList('/scripts') || [])
    .filter((f) => !f.isDir && f.name.endsWith('.lua'))
    .map((f) => {
      const name = f.name.slice(0, -4);
      const page = vfsReadText(`/scripts/${name}.page`).trim();
      return { name, size: f.size, auto: auto.has(name), ...(page ? { page } : {}) };
    });
  res.json(files);
});

app.get('/script/libs', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const libs = (vfsList('/scripts/lib') || [])
    .filter((f) => !f.isDir && f.name.endsWith('.lua'))
    .map((f) => ({ name: f.name.slice(0, -4), size: f.size }));
  res.json(libs);
});

app.post('/script/run', rawBody, (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const name = String(req.query.name || '');
  if (!/^[A-Za-z0-9_-]{1,23}$/.test(name)) return res.status(400).json({ ok: false });
  const code = req.body.toString('utf8');
  if (!code) return res.status(400).json({ ok: false });

  if (req.query.lib === '1') {
    vfsWrite(`/scripts/lib/${name}.lua`, code);
    return res.json({ ok: true });
  }
  if (req.query.save === '1') {
    vfsWrite(`/scripts/${name}.lua`, code);
    const page = String(req.query.page || '');
    if (page) vfsWrite(`/scripts/${name}.page`, page);
    else vfsDeleteRecursive(`/scripts/${name}.page`);
  }
  const ok = runScript(name, code);
  res.status(ok ? 200 : 503).json({ ok });
});

app.post('/script/runfile', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const name = String(req.query.name || '');
  const code = vfsReadText(`/scripts/${name}.lua`);
  if (!code) return res.status(400).json({ ok: false });
  const ok = runScript(name, code);
  res.status(ok ? 200 : 400).json({ ok });
});

app.post('/script/stop', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const name = String(req.query.name || '');
  if (name === 'all') { stopAllScripts(); return res.json({ ok: true }); }
  res.status(stopScript(name) ? 200 : 400).json({ ok: true });
});

app.post('/script/auto', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const name = String(req.query.name || '');
  const set = getAutoList();
  if (req.query.on === '1') set.add(name);
  else set.delete(name);
  setAutoList(set);
  res.json({ ok: true });
});

app.post('/script/delete', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const name = String(req.query.name || '');
  stopScript(name);
  const set = getAutoList();
  set.delete(name);
  setAutoList(set);
  vfsDeleteRecursive(`/scripts/${name}.lua`);
  vfsDeleteRecursive(`/scripts/${name}.page`);
  vfsDeleteRecursive(`/scripts/.data/${name}.json`);
  res.json({ ok: true });
});

app.post('/script/dellib', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const name = String(req.query.name || '');
  vfsDeleteRecursive(`/scripts/lib/${name}.lua`);
  res.json({ ok: true });
});

app.get('/ui/panels', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const out = [...panels.values()].map((p) => ({
    id: p.id,
    script: p.script,
    title: p.title,
    ...(p.page ? { page: p.page } : {}),
    widgets: p.widgets
  }));
  res.json({ ver: uiVer, panels: out });
});

app.get('/ui/poll', (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const since = Number(req.query.since) || 0;
  const msgs = uiMsgs.filter((m) => m.seq > since);
  res.json({ seq: msgSeq, ver: uiVer, msgs });
});

app.post('/ui/event', rawBody, (req, res) => {
  if (!isAdminToken(req)) return res.sendStatus(401);
  const pId = Number(req.query.p);
  const n = String(req.query.n || '');
  const panel = panels.get(pId);
  if (!panel) return res.status(400).json({ ok: false });
  let val = null;
  try { val = JSON.parse(req.body.toString('utf8')); } catch {}
  const w = panel.widgets.find((x) => x.id === n);
  if (w && (w.type === 'switch' || w.type === 'slider' || w.type === 'input')) {
    w.value = val;
  }
  const cb = panel.callbacks.get(n) || panel.pageListeners.get(n);
  if (cb) {
    try { cb(val); } catch (e) { logPrintf(`[${panel.script}] event error: ${e.message}\n`); }
  }
  res.json({ ok: true });
});

// --- Frontend Serving (Vite Dev Middleware or Built Static Dist) ---
async function start() {
  if (!IS_PROD) {
    const { createServer: createViteServer } = await import('vite');
    const vite = await createViteServer({
      configFile: path.join(__dirname, 'ui/vite.config.js'),
      root: path.join(__dirname, 'ui'),
      server: { middlewareMode: true, allowedHosts: true }
    });
    app.use(vite.middlewares);
  } else {
    const distDir = path.join(__dirname, 'dist');
    app.use(express.static(distDir));
    app.get('*', (_req, res) => res.sendFile(path.join(distDir, 'index.html')));
  }

  app.listen(PORT, '0.0.0.0', () => {
    console.log(`ESP32-C3 WebUI server listening on http://0.0.0.0:${PORT}`);
  });
}

start();
