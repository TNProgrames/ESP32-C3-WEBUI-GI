<script>
  import { api, apiRaw, authHeaders, ApiError } from '../api.js';
  import { t } from '../store.svelte.js';
  import { examples } from '../examples.js';
  import { lab, persistLab } from '../lab.svelte.js';
  import CodeEditor from './CodeEditor.svelte';
  import Icon from './Icon.svelte';

  let { go = () => {} } = $props();

  let box = $state();
  let busy = $state(false);

  // حفظ الحالة في المتصفح عند كل تعديل
  $effect(() => { lab.name; lab.code; lab.showPage; lab.asLib; lab.pagePath; lab.pageHtml; persistLab(); });

  const running = $derived(Object.fromEntries(lab.list.map((s) => [s.name, s])));
  // الأسماء: الملفات المحفوظة + أي سكريبت يعمل وليس محفوظًا
  const rows = $derived([
    ...lab.files.map((f) => ({ ...f, run: running[f.name] })),
    ...lab.list.filter((s) => !lab.files.some((f) => f.name === s.name)).map((s) => ({ name: s.name, size: 0, auto: false, run: s, unsaved: true }))
  ]);
  // سطر الخطأ من رسالة Lua مثل: [string "lab"]:3: ...
  const errLine = $derived.by(() => {
    const e = running[lab.name]?.err;
    const m = e && e.match(/:(\d+):/);
    return m ? Number(m[1]) : 0;
  });

  async function refresh() {
    try {
      const [list, files, libs] = await Promise.all([api('/script/list'), api('/script/files'), api('/script/libs')]);
      lab.list = list; lab.files = files; lab.libs = libs;
      const r = await api('/terminal/log?since=' + lab.seq);
      lab.seq = r.seq;
      if (r.text) {
        lab.out = (lab.out + r.text).slice(-6000);
        queueMicrotask(() => { if (box) box.scrollTop = box.scrollHeight; });
      }
    } catch {}
  }

  $effect(() => {
    refresh();
    const timer = setInterval(refresh, 1200);
    return () => clearInterval(timer);
  });

  async function savePage() {
    if (!/^\/ui\/[\w.-]+\.html$/.test(lab.pagePath)) throw new Error(t('مسار الصفحة يجب أن يكون /ui/<اسم>.html'));
    await fetch('/api/mkdir?path=/ui', { method: 'POST', headers: authHeaders() });
    const r = await fetch('/api/save?path=' + encodeURIComponent(lab.pagePath), {
      method: 'POST',
      headers: { ...authHeaders(), 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'content=' + encodeURIComponent(lab.pageHtml)
    });
    if (!r.ok) throw new Error(t('تعذر حفظ الصفحة'));
  }

  function say(msg) { lab.out = (lab.out + msg + '\n').slice(-6000); }
  function fail(e, what) {
    say(e instanceof ApiError ? what + ' [' + e.status + ']' : (e?.message || what));
  }

  // تشغيل = حفظ الملف على الجهاز ثم (إعادة) تشغيله
  async function run() {
    const n = lab.name.trim();
    if (!/^[A-Za-z0-9_-]{1,23}$/.test(n)) return say(t('اسم السكريبت: حروف إنجليزية وأرقام و _ و - فقط (حتى 23)'));
    if (!lab.code.trim()) return say(t('السكريبت فارغ'));
    busy = true;
    try {
      if (lab.asLib) {
        await apiRaw('/script/run?lib=1&name=' + n, lab.code);
        say(t('حُفظت المكتبة') + ' ' + n + ' → require("' + n + '")');
        busy = false; setTimeout(refresh, 300);
        return;
      }
      let q = '/script/run?save=1&name=' + n;
      if (lab.showPage && lab.pageHtml.trim()) { await savePage(); q += '&page=' + encodeURIComponent(lab.pagePath); }
      await apiRaw(q, lab.code);
    } catch (e) { fail(e, t('تعذر التشغيل (تحقق من الاسم أو الحجم)')); }
    busy = false;
    setTimeout(refresh, 300);
  }

  async function call(path, what) {
    try { await apiRaw(path); } catch (e) { fail(e, what); }
    setTimeout(refresh, 300);
  }
  const stop = (n) => call('/script/stop?name=' + encodeURIComponent(n), t('تعذر الإيقاف'));
  const start = (n) => call('/script/runfile?name=' + encodeURIComponent(n), t('تعذر التشغيل'));
  const setAuto = (n, on) => call('/script/auto?name=' + encodeURIComponent(n) + '&on=' + (on ? 1 : 0), t('تعذر الحفظ'));
  function del(n) { if (confirm(t('حذف السكريبت') + ' ' + n + '؟')) call('/script/delete?name=' + encodeURIComponent(n), t('تعذر الحذف')); }

  async function readFile(path) {
    const r = await fetch('/api/read?path=' + encodeURIComponent(path), { headers: authHeaders() });
    if (!r.ok) throw new ApiError(r.status);
    return r.text();
  }
  // تعديل سكريبت محفوظ (أو يعمل): يحمّل كوده وصفحته في المحرر
  async function edit(f) {
    try {
      const code = await readFile('/scripts/' + f.name + '.lua');
      let page = null;
      if (f.page) { try { page = await readFile(f.page); } catch {} }
      lab.name = f.name; lab.code = code;
      if (page !== null) { lab.pagePath = f.page; lab.pageHtml = page; lab.showPage = true; }
      else { lab.pageHtml = ''; lab.showPage = false; }
      document.getElementById('lab-editor')?.scrollIntoView({ behavior: 'smooth', block: 'start' });
    } catch (e) { fail(e, t('تعذر فتح الملف')); }
  }

  async function editLib(l) {
    try {
      lab.code = await readFile('/scripts/lib/' + l.name + '.lua');
      lab.name = l.name; lab.asLib = true; lab.showPage = false; lab.pageHtml = '';
      document.getElementById('lab-editor')?.scrollIntoView({ behavior: 'smooth', block: 'start' });
    } catch (e) { fail(e, t('تعذر فتح الملف')); }
  }
  function delLib(n) { if (confirm(t('حذف المكتبة') + ' ' + n + '؟')) call('/script/dellib?name=' + encodeURIComponent(n), t('تعذر الحذف')); }

  function load(e) {
    const k = e.target.value;
    if (!k) return;
    const ex = examples[k];
    lab.code = ex.code;
    lab.asLib = !!ex.lib;
    lab.name = ex.libName || k.replace(/[^A-Za-z0-9]+/g, '_').replace(/^_+|_+$/g, '').toLowerCase().slice(0, 20) || 'lab';
    if (ex.page) { lab.pagePath = ex.page.path; lab.pageHtml = ex.page.html; lab.showPage = true; }
    else { lab.pageHtml = ''; lab.showPage = false; }
    e.target.value = '';
  }
</script>

<div class="glass" id="lab-editor">
  <h2 style="margin-top:0"><Icon name="flask" size={17} /> {t('مختبر Lua')}</h2>
  <p style="color:var(--muted);font-size:13px;margin:0 0 10px">
    {t('اكتب سكريبت Lua وشغّله على الجهاز مباشرة. المتاح: gpio, adc, pwm, timer, sys, ui, print.')}
  </p>

  <div class="row" style="margin-bottom:8px">
    <input style="flex:1;min-width:90px" bind:value={lab.name} placeholder={t('اسم السكريبت')} autocapitalize="off" spellcheck="false" />
    <select style="width:auto;min-width:160px" onchange={load}>
      <option value="">{t('أمثلة...')}</option>
      {#each Object.keys(examples) as k}<option value={k}>{k}</option>{/each}
    </select>
  </div>

  <CodeEditor bind:value={lab.code} lang="lua" {errLine} height={240} />

  <label class="row" style="margin:10px 0 6px;font-size:13px;gap:8px">
    <input type="checkbox" style="width:auto" bind:checked={lab.showPage} /> {t('صفحة HTML مخصصة (تُحفظ على الجهاز قبل التشغيل)')}
  </label>
  {#if lab.showPage}
    <input style="direction:ltr;text-align:left;margin-bottom:6px" bind:value={lab.pagePath} spellcheck="false" autocapitalize="off" />
    <CodeEditor bind:value={lab.pageHtml} lang="html" height={200} />
  {/if}

  <label class="row" style="margin:6px 0;font-size:13px;gap:8px">
    <input type="checkbox" style="width:auto" bind:checked={lab.asLib} /> {t('حفظ كمكتبة (require) بدل التشغيل')}
  </label>

  <div class="row" style="margin:10px 0">
    <button class="btn" onclick={run} disabled={busy}>
      <Icon name={lab.asLib ? 'box' : 'play'} size={15} mono={true} /> {lab.asLib ? t('حفظ المكتبة') : t('حفظ وتشغيل')}
    </button>
    <button class="btn ghost" onclick={() => stop('all')}><Icon name="stop" size={15} /> {t('إيقاف الكل')}</button>
    <button class="btn ghost" onclick={() => go('panels')}><Icon name="layout" size={15} /> {t('اللوحات')}</button>
  </div>
</div>

<div class="glass">
  <h3 style="margin:0 0 10px"><Icon name="folder" size={16} /> {t('السكريبتات المحفوظة')}</h3>
  {#if !rows.length}
    <div style="color:var(--muted);font-size:13px">{t('لا توجد سكريبتات محفوظة بعد. «حفظ وتشغيل» يخزّن الملف على الجهاز.')}</div>
  {/if}
  {#each rows as f (f.name)}
    <div style="padding:8px 0;border-bottom:1px solid var(--border)">
      <div class="row" style="justify-content:space-between;gap:6px">
        <span class="row" style="font-size:13px;min-width:0;word-break:break-all;gap:7px">
          <span class="status-dot" class:run={f.run?.state === 'running'} class:err={f.run && f.run.state !== 'running'}></span>
          <b>{f.name}</b>
          <span class="row" style="color:var(--muted);gap:6px">
            {#if f.size}<span>· {f.size}B</span>{/if}
            {#if f.run}<span>· {(f.run.mem / 1024).toFixed(1)}KB · {f.run.up}s</span>{/if}
            {#if f.page}<span style="display:inline-flex;align-items:center" title={t('صفحة')}><Icon name="layout" size={13} /></span>{/if}
          </span>
        </span>
        <span class="row" style="gap:4px;flex-wrap:nowrap">
          {#if f.run}<button class="btn ghost icon-btn" title={t('إيقاف')} onclick={() => stop(f.name)}><Icon name="stop" size={14} /></button>
          {:else}<button class="btn ghost icon-btn" title={t('تشغيل')} onclick={() => start(f.name)}><Icon name="play" size={14} /></button>{/if}
          {#if !f.unsaved}<button class="btn ghost icon-btn" title={t('تعديل')} onclick={() => edit(f)}><Icon name="edit" size={14} /></button>
          <button class="btn ghost icon-btn" title={t('حذف')} onclick={() => del(f.name)}><Icon name="trash" size={14} /></button>{/if}
        </span>
      </div>
      {#if !f.unsaved}
        <label class="row" style="font-size:12px;color:var(--muted);gap:6px;margin-top:4px">
          <input type="checkbox" style="width:auto" checked={f.auto} onchange={(e) => setAuto(f.name, e.currentTarget.checked)} />
          {t('تشغيل تلقائي عند إقلاع الجهاز')}
        </label>
      {/if}
      {#if f.run?.err}<div style="color:var(--danger);font-size:12px;direction:ltr;text-align:left;margin-top:4px">{f.run.err}</div>{/if}
    </div>
  {/each}
</div>

<div class="glass">
  <h3 style="margin:0 0 10px"><Icon name="box" size={16} /> {t('المكتبات')} <span style="font-size:12px;color:var(--muted);font-weight:400">/scripts/lib · require("name")</span></h3>
  {#if !lab.libs.length}
    <div style="color:var(--muted);font-size:13px">{t('لا توجد مكتبات. فعّل «حفظ كمكتبة» واحفظ ملفًا ثم استعمله بـ require.')}</div>
  {/if}
  {#each lab.libs as l (l.name)}
    <div class="row" style="justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--border)">
      <span style="font-size:13px"><b>{l.name}</b> <span style="color:var(--muted)">· {l.size}B</span></span>
      <span class="row" style="gap:4px;flex-wrap:nowrap">
        <button class="btn ghost icon-btn" title={t('تعديل')} onclick={() => editLib(l)}><Icon name="edit" size={14} /></button>
        <button class="btn ghost icon-btn" title={t('حذف')} onclick={() => delLib(l.name)}><Icon name="trash" size={14} /></button>
      </span>
    </div>
  {/each}
</div>

<div class="glass">
  <div class="row" style="justify-content:space-between;margin-bottom:8px">
    <h3 style="margin:0"><Icon name="terminal" size={16} /> {t('الطرفية')}</h3>
    <button class="btn ghost sm" onclick={() => (lab.out = '')}><Icon name="eraser" size={14} /> {t('تنظيف')}</button>
  </div>
  <div class="term" bind:this={box} style="min-height:120px;max-height:220px">{lab.out}</div>
</div>
