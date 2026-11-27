<script>
  import { authHeaders } from '../api.js';
  import { ui, t } from '../store.svelte.js';
  import { pn, onMsg, sendEvent, keyOf } from '../panels.svelte.js';
  import { buildSrcdoc } from '../pagebridge.js';

  let { panel, path } = $props();
  let doc = $state('');
  let err = $state('');
  let height = $state(260);
  let frame = $state();

  $effect(() => {
    const p = path;
    let dead = false;
    doc = ''; err = '';
    fetch('/api/read?path=' + encodeURIComponent(p), { headers: authHeaders() })
      .then(r => { if (!r.ok) throw 0; return r.text(); })
      .then(h => { if (!dead) doc = buildSrcdoc(h); })
      .catch(() => { if (!dead) err = t('تعذر تحميل الصفحة') + ': ' + p; });
    return () => { dead = true; };
  });

  function post(msg) { frame?.contentWindow?.postMessage({ esp: 1, ...msg }, '*'); }
  function init() { post({ k: 'init', theme: ui.theme, lang: ui.lang }); }

  // رسائل الصفحة → الجهاز (نقبل فقط ما جاء من إطارنا)
  $effect(() => {
    const h = (e) => {
      if (!frame || e.source !== frame.contentWindow) return;
      const d = e.data;
      if (!d || d.esp !== 1) return;
      if (d.k === 'hello') {
        init();
        const pre = panel + ':';
        for (const k of Object.keys(pn.vals)) if (k.startsWith(pre)) post({ k: 'emit', n: k.slice(pre.length), v: pn.vals[k] });
      } else if (d.k === 'send') {
        if (typeof d.n !== 'string' || d.n.length > 15) return;
        const s = JSON.stringify(d.v);
        if (s === undefined || s.length > 46) return;
        pn.vals[keyOf(panel, d.n)] = d.v;
        sendEvent(panel, d.n, d.v);
      } else if (d.k === 'h') {
        height = Math.max(60, Math.min(800, Number(d.h) || 260));
      }
    };
    window.addEventListener('message', h);
    return () => window.removeEventListener('message', h);
  });

  // قيم الجهاز → الصفحة
  $effect(() => onMsg((p, n, v) => { if (p === panel) post({ k: 'emit', n, v }); }));
  // تغيّر السمة/اللغة
  $effect(() => { ui.theme; ui.lang; init(); });
</script>

{#if err}
  <div style="color:#e55;font-size:13px">{err}</div>
{:else if doc}
  <iframe bind:this={frame} sandbox="allow-scripts" srcdoc={doc} title="page"
    style="width:100%;height:{height}px;border:1px solid var(--border);border-radius:12px;background:transparent;display:block"></iframe>
{/if}
