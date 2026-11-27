<script>
  import { untrack } from 'svelte';
  import { api, apiRaw, authHeaders } from '../api.js';
  import { DEFAULT_INDEX, fetchStore, compare, installToDevice, removeFromDevice, installFromStore, installApp, appDrivers, DRV_MAX, getMode, setMode } from '../drvstore.js';
  import { t } from '../store.svelte.js';
  import AppCards from './AppCards.svelte';
  import { lab } from '../lab.svelte.js';
  import Icon from './Icon.svelte';

  let { go = () => {} } = $props();

  const STORE_KEY = 'drv-store-url';
  let list = $state([]);            // المثبّت على الجهاز
  let msg = $state('');
  let busy = $state(false);
  let file = $state(null);
  let storeUrl = $state(localStorage.getItem(STORE_KEY) || DEFAULT_INDEX);
  let store = $state([]);           // درايفرات المتجر
  let apps = $state([]);            // تطبيقات المتجر (سكريبت Lua + درايفره)
  let myScripts = $state([]);       // أسماء السكريبتات على الجهاز
  let mode = $state({ mode: 'auto', exec: false });
  let storeMsg = $state('');
  let tab = $state('apps');          // apps | drivers | settings
  let busyApp = $state('');

  let box = $state();

  async function refresh() {
    try {
      list = await api('/drv/list');
      const r = await api('/terminal/log?since=' + lab.seq);
      lab.seq = r.seq;
      if (r.text) { lab.out = (lab.out + r.text).slice(-6000); queueMicrotask(() => { if (box) box.scrollTop = box.scrollHeight; }); }
    } catch {}
  }
  $effect(() => { refresh(); const id = setInterval(refresh, 1500); return () => clearInterval(id); });

  // ---- التثبيت والحذف (المنطق في drvstore.js) ----
  async function install(buf) {
    busy = true; msg = '';
    const r = await installToDevice(buf);
    msg = r.ok ? t('تم التثبيت') : t('تعذر التثبيت') + ': ' + r.error;
    busy = false; refresh();
    return r.ok;
  }
  async function upload() { if (file) await install(await file.arrayBuffer()); }
  async function remove(n) {
    if (!confirm(t('حذف الدرايفر') + ' ' + n + '؟')) return;
    const r = await removeFromDevice(n);
    msg = r.ok ? t('تم الحذف') : t('تعذر الحذف') + ': ' + r.error;
    refresh();
  }

  // ---- المتجر: يجلبه المتصفح لا الجهاز ----
  async function loadStore() {
    storeMsg = ''; store = []; apps = [];
    try {
      localStorage.setItem(STORE_KEY, storeUrl);
      const r = await fetchStore(storeUrl);
      store = r.drivers; apps = r.apps;
      if (!store.length && !apps.length) storeMsg = t('الفهرس فارغ');
    } catch (e) { storeMsg = t('تعذر جلب الفهرس (يلزم اتصال بالإنترنت)') + ': ' + (e.message || ''); }
  }
  async function loadScripts() { try { const f = await api('/script/files'); myScripts = (f || []).map((x) => x.name); } catch {} }
  async function loadMode() { try { mode = await getMode(); } catch {} }
  async function changeMode(m) {
    busy = true; msg = '';
    msg = (await setMode(m)) ? t('تم تغيير الوضع وأُعيد تحميل الدرايفرات') : t('تعذر تغيير الوضع');
    busy = false; await loadMode(); refresh();
  }
  // نافذة الأماكن: عند نقص خانات الدرايفرات يختار المستخدم ما يحذفه قبل التثبيت
  let slotAsk = $state(null);       // { app, missing, free, need, pick: [] }
  const usedBy = (d) => apps.filter((x) => myScripts.includes(x.name) && appDrivers(x).includes(d)).map((x) => x.title || x.name);
  async function getApp(a) {
    const missing = appDrivers(a).filter((d) => !list.some((i) => i.name === d));
    const free = Math.max(0, DRV_MAX - list.length);
    if (missing.length > free) { slotAsk = { app: a, missing, free, need: missing.length - free, pick: [] }; return; }
    await doInstall(a);
  }
  async function slotConfirm() {
    const q = slotAsk; if (!q || q.pick.length < q.need) return;
    slotAsk = null; busyApp = q.app.name; msg = '';
    const fail = [];
    for (const d of q.pick) { const r = await removeFromDevice(d); if (!r.ok) fail.push(d + ': ' + r.error); }
    await refresh();
    if (fail.length) { msg = t('تعذر الحذف') + ': ' + fail.join(' · '); busyApp = ''; return; }
    await doInstall(q.app);
  }
  async function doInstall(a) {
    busyApp = a.name; msg = '';
    try {
      const r = await installApp(a, store, list, storeUrl);
      msg = r.ok ? t('تم تثبيت التطبيق') : t('تعذر التثبيت') + ': ' + r.error;
    } catch (e) { msg = t('تعذر التنزيل') + ': ' + (e.message || ''); }
    busyApp = ''; refresh(); loadScripts();
  }
  // حذف التطبيق: السكريبت وصفحته ودرايفراته (إلا ما يستعمله تطبيق مثبّت آخر)
  async function delApp(a) {
    const mine = appDrivers(a);
    const others = apps.filter((x) => x.name !== a.name && myScripts.includes(x.name));
    const drop = mine.filter((d) => list.some((i) => i.name === d) && !others.some((x) => appDrivers(x).includes(d)));
    const keep = mine.filter((d) => list.some((i) => i.name === d) && !drop.includes(d));
    let q = t('حذف التطبيق') + ' ' + a.name + (drop.length ? ' + ' + t('درايفر') + ': ' + drop.join(', ') : '') + '؟';
    if (keep.length) q += '\n(' + t('يبقى لأن تطبيقًا آخر يستعمله') + ': ' + keep.join(', ') + ')';
    if (!confirm(q)) return;
    busyApp = a.name; msg = '';
    try {
      await apiRaw('/script/delete?name=' + encodeURIComponent(a.name));
      if (a.page_file) await fetch('/api/delete?path=' + encodeURIComponent('/ui/' + a.name + '.html'), { method: 'DELETE', headers: authHeaders() });
      const fail = [];
      for (const d of drop) { const r = await removeFromDevice(d); if (!r.ok) fail.push(d + ': ' + r.error); }
      msg = fail.length ? t('تعذر الحذف') + ': ' + fail.join(' · ') : t('تم الحذف');
    } catch { msg = t('تعذر الحذف'); }
    busyApp = ''; refresh(); loadScripts();
  }
  async function runApp(a) {
    try { await apiRaw('/script/runfile?name=' + encodeURIComponent(a.name)); msg = t('تم التشغيل'); } catch { msg = t('تعذر التشغيل'); }
  }
  async function editApp(a) {
    try {
      const r = await fetch('/api/read?path=' + encodeURIComponent('/scripts/' + a.name + '.lua'), { headers: authHeaders() });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const code = await r.text();
      const pagePath = '/ui/' + a.name + '.html';
      let pageHtml = null;
      if (a.page_file) {
        try {
          const pr = await fetch('/api/read?path=' + encodeURIComponent(pagePath), { headers: authHeaders() });
          if (pr.ok) pageHtml = await pr.text();
        } catch {}
      }
      lab.name = a.name; lab.code = code; lab.asLib = false;
      if (pageHtml !== null) { lab.pagePath = pagePath; lab.pageHtml = pageHtml; lab.showPage = true; }
      else { lab.pageHtml = ''; lab.showPage = false; }
      go('lab');
    } catch { msg = t('تعذر فتح الملف'); }
  }
  $effect(() => { untrack(() => { loadStore(); loadScripts(); loadMode(); }); });

  async function fromStore(d) {
    busy = true; msg = '';
    try {
      const r = await installFromStore(d, storeUrl);
      msg = r.ok ? t('تم التثبيت') : t('تعذر التثبيت') + ': ' + r.error;
    } catch (e) { msg = t('تعذر التنزيل') + ': ' + (e.message || ''); }
    busy = false; refresh();
  }

  // ---- توليد Lua للاختبار ----
  const tr = { i: 'int', s: 'str', b: 'bool', d: 'data' };
</script>
<div class="glass">
  <h2 style="margin:0 0 10px"><Icon name="store" size={17} /> {t('متجر التطبيقات')}</h2>
  <div class="row" style="gap:6px">
    {#each [['apps', 'grid', 'التطبيقات'], ['drivers', 'plug', 'الدرايفرات'], ['settings', 'settings', 'الإعدادات']] as [id, ic, lb]}
      <button class="btn tab-btn" class:active={tab === id} style="flex:1" onclick={() => (tab = id)}>
        <Icon name={ic} size={15} /> {t(lb)}
      </button>
    {/each}
  </div>
  {#if msg}<div class="msg ok" style="margin-top:8px">{msg}</div>{/if}
</div>

{#if slotAsk}
  <div class="glass" style="border:1px solid var(--primary)">
    <div class="sub-title" style="margin-bottom:6px;color:var(--warning)"><Icon name="alert" size={16} /> {t('لا توجد أماكن كافية للدرايفرات')}</div>
    <div style="font-size:13px;margin:6px 0">
      {t('التطبيق')} <b>{slotAsk.app.title || slotAsk.app.name}</b> {t('يحتاج درايفرات جديدة')}: <b>{slotAsk.missing.join(', ')}</b>
    </div>
    <div class="row" style="gap:14px;font-size:13px;flex-wrap:wrap">
      <span>{t('الأماكن الكلية')}: <b>{DRV_MAX}</b></span>
      <span>{t('المستعملة')}: <b>{list.length}</b></span>
      <span>{t('المتبقية')}: <b>{slotAsk.free}</b></span>
      <span>{t('المطلوبة')}: <b>{slotAsk.missing.length}</b></span>
      <span style="color:{slotAsk.pick.length >= slotAsk.need ? 'var(--success)' : 'var(--danger)'}">{t('اختر للحذف')}: <b>{slotAsk.pick.length}/{slotAsk.need}</b></span>
    </div>
    <div style="margin-top:8px;font-size:13px">
      {#each list as d}
        <label class="row" style="gap:8px;padding:4px 0;align-items:center;cursor:pointer;color:var(--text)">
          <input type="checkbox" value={d.name} bind:group={slotAsk.pick} />
          <span><b>{d.name}</b>{#if usedBy(d.name).length} <span style="color:var(--danger);font-size:11px;display:inline-flex;align-items:center;gap:3px"><Icon name="alert" size={12} /> {t('يستعمله')}: {usedBy(d.name).join(', ')}</span>{/if}</span>
        </label>
      {/each}
    </div>
    <div class="row" style="gap:6px;margin-top:10px">
      <button class="btn" disabled={slotAsk.pick.length < slotAsk.need} onclick={slotConfirm}><Icon name="trash" size={14} mono={true} /> {t('احذف وأكمل التثبيت')}</button>
      <button class="btn ghost" onclick={() => (slotAsk = null)}>{t('إلغاء')}</button>
    </div>
  </div>
{/if}

{#if tab === 'apps'}
  <AppCards {apps} installed={list} scripts={myScripts} busyName={busyApp} msg={storeMsg}
    onInstall={getApp} onRun={runApp} onEdit={editApp} onRemove={delApp} onRefresh={loadStore} />
{:else if tab === 'drivers'}
<div class="glass">
  <p style="color:var(--muted);font-size:13px;margin:0 0 10px">
    {t('درايفرات موقّعة تُحمَّل عند الإقلاع. سكريبت Lua يستعملها بـ drv.open(...) ولا يلمس العتاد مباشرة.')}
  </p>
  {#if !list.length}<div style="color:var(--muted);font-size:13px">{t('لا توجد درايفرات مثبّتة')}</div>{/if}
  {#each list as d (d.name)}
    <div style="padding:8px 0;border-bottom:1px solid var(--border)">
      <div class="row" style="justify-content:space-between">
        <span><b>{d.name}</b> <span style="color:var(--muted);font-size:12px">v{d.version} · {d.size}B · RAM {d.mem}B · {d.mode === 'native' ? t('المعالج') : 'VM'}{d.users ? ' · ' + t('مفتوح') : ''}</span></span>
        <button class="btn ghost icon-btn" title={t('حذف')} disabled={busy || d.users > 0} onclick={() => remove(d.name)}><Icon name="trash" size={14} /></button>
      </div>
      <div style="color:var(--muted);font-size:12px">{d.info}</div>
      <div style="font-size:12px;direction:ltr;text-align:left;margin-top:2px">{d.methods.map((m) => m.name + '(' + [...m.sig].map((c) => tr[c.toLowerCase()] + (c === c.toUpperCase() ? '?' : '')).join(', ') + ')').join(' · ')}</div>
    </div>
  {/each}
</div>
<div class="glass">
  <h3 style="margin:0 0 8px"><Icon name="upload" size={16} /> {t('رفع ملف درايفر')}</h3>
  <input type="file" accept=".drv" onchange={(e) => (file = e.currentTarget.files[0])} disabled={busy} />
  <div style="margin-top:10px"><button class="btn" onclick={upload} disabled={busy || !file}><Icon name="upload" size={14} mono={true} /> {t('تثبيت')}</button></div>
</div>

<div class="glass">
  <h3 style="margin:0 0 8px"><Icon name="store" size={16} /> {t('المتجر')}</h3>
  <div class="row" style="gap:6px">
    <input style="flex:1;direction:ltr" bind:value={storeUrl} placeholder="https://.../index.json" />
    <button class="btn ghost" onclick={loadStore}><Icon name="refresh" size={14} /> {t('جلب')}</button>
  </div>
  {#if storeMsg}<div style="margin-top:6px;font-size:12px;color:var(--muted)">{storeMsg}</div>{/if}
  {#each store as d (d.name)}
    <div style="padding:6px 0;border-bottom:1px solid var(--border)">
      <div class="row" style="justify-content:space-between">
        <span style="font-size:13px"><b>{d.name}</b> <span style="color:var(--muted)">v{d.version} · {d.size}B · RAM {d.mem}B · API {d.api}</span></span>
        <button class="btn ghost sm" disabled={busy || compare(d, list) === 'same'} onclick={() => fromStore(d)}>
          {compare(d, list) === 'same' ? t('مثبّت') : compare(d, list) === 'upd' ? t('تحديث') : t('تثبيت')}
        </button>
      </div>
      <div style="color:var(--muted);font-size:12px">{d.info}</div>
    </div>
  {/each}
</div>

{:else}
<div class="glass">
  <h3 style="margin:0 0 8px"><Icon name="settings" size={16} /> {t('وضع تشغيل الدرايفرات')}</h3>
  <select value={mode.mode} disabled={busy} onchange={(e) => changeMode(e.currentTarget.value)}>
    <option value="auto">{t('تلقائي (المعالج إن أمكن وإلا المفسّر)')}</option>
    <option value="vm">{t('المفسّر VM (معزول، أبطأ)')}</option>
    <option value="native">{t('المعالج (سريع، يتطلب ذاكرة تنفيذ)')}</option>
  </select>
  <div style="color:var(--muted);font-size:12px;margin-top:6px">{mode.exec ? t('ذاكرة التنفيذ متاحة على هذا الجهاز') : t('ذاكرة التنفيذ غير متاحة: سيعمل المفسّر')}</div>
</div>

{/if}

<div class="glass">
  <div class="row" style="justify-content:space-between;margin-bottom:8px">
    <h3 style="margin:0"><Icon name="terminal" size={16} /> {t('الطرفية')}</h3>
    <button class="btn ghost sm" onclick={() => (lab.out = '')}><Icon name="eraser" size={14} /> {t('تنظيف')}</button>
  </div>
  <div class="term" bind:this={box} style="min-height:100px;max-height:200px">{lab.out}</div>
</div>
