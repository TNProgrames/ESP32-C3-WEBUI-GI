import { api, apiRaw } from './api.js';

// حالة اللوحات: التعريف من /ui/panels، والتحديثات من /ui/poll (استطلاع)
export const pn = $state({ panels: [], vals: {} });
const listeners = new Set();
let seq = 0, seenVer = -1, timer = null, busy = false;

export function onMsg(fn) { listeners.add(fn); return () => listeners.delete(fn); }
export const keyOf = (p, n) => p + ':' + n;

async function tick() {
  if (busy) return;
  busy = true;
  try {
    const r = await api('/ui/poll?since=' + seq);
    if (r.seq < seq) { seq = 0; seenVer = -1; busy = false; return; }   // الجهاز أُعيد تشغيله
    seq = r.seq;
    for (const m of r.msgs) {
      pn.vals[keyOf(m.p, m.n)] = m.v;
      listeners.forEach(f => f(m.p, m.n, m.v));
    }
    if (r.ver !== seenVer) {
      const d = await api('/ui/panels');
      for (const p of d.panels) for (const w of p.widgets) pn.vals[keyOf(p.id, w.id)] = w.value;
      pn.panels = d.panels;
      seenVer = r.ver;
    }
  } catch {}
  busy = false;
}

export function startPanels() {
  if (timer) return;
  seq = 0; seenVer = -1;
  tick();
  timer = setInterval(tick, 600);
}
export function stopPanels() { clearInterval(timer); timer = null; }

export async function sendEvent(p, n, v) {
  try { await apiRaw('/ui/event?p=' + p + '&n=' + encodeURIComponent(n), JSON.stringify(v === undefined ? null : v)); } catch {}
}
