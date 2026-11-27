<script>
  import { api } from '../api.js';
  import { t } from '../store.svelte.js';
  import Icon from './Icon.svelte';

  const MAX = 20000;
  let out = $state(t('اكتب help لعرض الأوامر') + '\n');
  let live = $state(false);
  let cmd = $state('');
  let box = $state();
  let seq = 0;
  let hist = [];
  let hi = -1;

  function append(s) {
    out = (out + s).slice(-MAX);
    queueMicrotask(() => { if (box) box.scrollTop = box.scrollHeight; });
  }

  async function poll() {
    try {
      const r = await api('/terminal/log?since=' + seq);
      seq = r.seq;
      if (r.text) append(r.text);
    } catch {}
  }

  $effect(() => {
    if (!live) return;
    poll();
    const timer = setInterval(poll, 1500);
    return () => clearInterval(timer);
  });

  async function send(e) {
    e.preventDefault();
    const c = cmd.trim();
    if (!c) return;
    hist.unshift(c); hi = -1; cmd = '';
    append('> ' + c + '\n');
    if (c === 'clear') { out = ''; return; }
    try {
      const r = await api('/terminal/cmd', { cmd: c });
      if (r.out) append(r.out + '\n');
    } catch { append('error\n'); }
  }

  function keys(e) {
    if (e.key === 'ArrowUp' && hist.length) { hi = Math.min(hi + 1, hist.length - 1); cmd = hist[hi]; e.preventDefault(); }
    else if (e.key === 'ArrowDown') { hi = Math.max(hi - 1, -1); cmd = hi < 0 ? '' : hist[hi]; e.preventDefault(); }
  }
</script>

<div class="glass">
  <div class="row" style="justify-content:space-between;margin-bottom:12px">
    <h2 style="margin:0"><Icon name="terminal" size={17} /> {t('الطرفية')}</h2>
    <div class="row">
      <span style="font-size:12px;color:var(--muted)">{t('عرض سجل الجهاز (Serial)')}</span>
      <label class="switch"><input type="checkbox" bind:checked={live} /><span class="slider"></span></label>
    </div>
  </div>

  <div class="term" bind:this={box}>{out}</div>

  <form class="row" style="margin-top:10px;flex-wrap:nowrap" onsubmit={send}>
    <input class="term-input" style="flex:1" bind:value={cmd} onkeydown={keys} placeholder={t('أمر...')} autocomplete="off" autocapitalize="off" spellcheck="false" />
    <button class="btn">{t('إرسال')}</button>
    <button type="button" class="btn ghost icon-btn" title={t('تنظيف')} aria-label={t('تنظيف')} onclick={() => (out = '')}><Icon name="eraser" size={16} /></button>
  </form>
</div>
