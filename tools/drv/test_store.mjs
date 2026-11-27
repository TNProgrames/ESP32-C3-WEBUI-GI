import assert from 'node:assert';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'node:url';
import { createHash } from 'crypto';
globalThis.localStorage = { getItem: () => null, setItem() {} };
globalThis.sessionStorage = { getItem: () => 'tok' };
globalThis.location = { reload() {} };
const S = await import(new URL('../../ui/src/drvstore.js', import.meta.url).href);
const root = fileURLToPath(new URL('../../drivers/build/', import.meta.url));
const idxText = readFileSync(root + 'index.json', 'utf8');
const calls = [];
globalThis.fetch = async (url, opt) => {
  url = String(url); calls.push(url);
  if (url.endsWith('index.json')) return new Response(idxText);
  if (url.endsWith('.drv')) return new Response(readFileSync(root + url.split('/').pop()));
  if (url === '/drv/install') return new Response(JSON.stringify({ ok: true }));
  throw new Error('unexpected ' + url);
};
const base = 'https://raw.githubusercontent.com/x/y/main/drivers/build/index.json';
const idx = await S.fetchIndex(base);
assert.ok(idx.length >= 8 && idx.every((d) => d.sha256 && d.methods.length), 'index');
assert.ok(S.compare(idx[0], []) === 'new');
assert.ok(S.compare(idx[0], [{ name: idx[0].name, version: idx[0].version }]) === 'same');
assert.ok(S.compare(idx[0], [{ name: idx[0].name, version: '0.1' }]) === 'upd');
const buf = await S.download(idx[0], base);
assert.ok(calls.at(-1) === 'https://raw.githubusercontent.com/x/y/main/drivers/build/' + idx[0].file, calls.at(-1));
assert.ok(buf.byteLength === idx[0].size && createHash('sha256').update(Buffer.from(buf)).digest('hex') === idx[0].sha256);
let threw = false; try { await S.download({ ...idx[0], sha256: '00' + idx[0].sha256.slice(2) }, base); } catch (e) { threw = e.message; }
assert.ok(threw === 'sha256 mismatch', threw);
threw = false; try { await S.download({ ...idx[0], size: 5 }, base); } catch (e) { threw = e.message; }
assert.ok(threw === 'size mismatch', threw);
assert.ok((await S.installFromStore(idx[1], base)).ok === true);
globalThis.fetch = async () => new Response('{"version":2}'); threw = false;
try { await S.fetchIndex(base); } catch (e) { threw = e.message; } assert.ok(threw === 'bad index format');
console.log('drvstore OK');
