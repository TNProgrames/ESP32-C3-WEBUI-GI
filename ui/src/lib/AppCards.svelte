<script>
  import { t } from '../store.svelte.js';
  import { appDrivers } from '../drvstore.js';
  import Icon from './Icon.svelte';

  // بطاقات متجر التطبيقات: بحث + تصنيفات + حالة التثبيت. العمليات تُنفَّذ في المكوّن الأب.
  let { apps = [], installed = [], scripts = [], busyName = '', msg = '', onInstall, onRun, onEdit, onRemove, onRefresh } = $props();

  const CATS = [['all', 'grid', 'الكل'], ['sensors', 'thermometer', 'حساسات'], ['display', 'layers', 'شاشات'], ['time', 'clock', 'وقت'], ['tools', 'wrench', 'أدوات']];
  let q = $state('');
  let cat = $state('all');

  const shown = $derived(apps.filter((a) => {
    if (cat !== 'all' && (a.category || 'tools') !== cat) return false;
    const s = q.trim().toLowerCase();
    return !s || [a.title, a.name, a.desc, ...appDrivers(a)].some((x) => (x || '').toLowerCase().includes(s));
  }));
  const PER = 5;
  let pg = $state(1);
  const pages = $derived(Math.max(1, Math.ceil(shown.length / PER)));
  const cur = $derived(Math.min(pg, pages));
  const view = $derived(shown.slice((cur - 1) * PER, cur * PER));
  // أرقام الصفحات مع اختصار بنقاط: 1 … 4 5 6 … 12
  const nums = $derived.by(() => {
    const r = [];
    for (let i = 1; i <= pages; i++) if (i === 1 || i === pages || Math.abs(i - cur) <= 1) r.push(i); else if (r[r.length - 1] !== '…') r.push('…');
    return r;
  });
  const have = (d) => installed.some((i) => i.name === d);

  const ICONS = new Set([
    'activity', 'wave', 'thermometer', 'cloud-sun', 'toggle', 'cpu',
    'monitor', 'bulb', 'display', 'zap', 'sliders', 'clock', 'servo',
    'droplet', 'timer', 'gauge', 'compass', 'weather', 'layers', 'wrench', 'box'
  ]);
  function appIcon(a) {
    if (a.icon && ICONS.has(a.icon)) return a.icon;
    const c = a.category || 'tools';
    if (c === 'sensors') return 'thermometer';
    if (c === 'display') return 'layers';
    if (c === 'time') return 'clock';
    return 'cpu';
  }
</script>

<div class="glass">
  <div class="row" style="gap:6px;flex-wrap:nowrap">
    <div class="search-wrap">
      <span class="search-ic"><Icon name="search" size={15} /></span>
      <input type="search" placeholder={t('بحث عن تطبيق أو درايفر')} bind:value={q} oninput={() => (pg = 1)} />
    </div>
    <button class="btn ghost icon-btn" title={t('جلب')} aria-label={t('جلب')} disabled={!!busyName} onclick={onRefresh}><Icon name="refresh" size={16} /></button>
  </div>
  <div class="row" style="gap:6px;margin-top:10px;flex-wrap:wrap">
    {#each CATS as [id, ic, lb]}
      <button class="btn sm tab-btn" class:active={cat === id} onclick={() => { cat = id; pg = 1; }}>
        <Icon name={ic} size={14} /> {t(lb)}
      </button>
    {/each}
  </div>
</div>

{#if !shown.length}
  <div class="glass" style="color:var(--muted);font-size:13px">{msg || (apps.length ? t('لا نتائج') : t('لا توجد تطبيقات (اضغط جلب)'))}</div>
{/if}

{#each view as a (a.name)}
  {@const has = scripts.includes(a.name)}
  {@const missing = appDrivers(a).filter((d) => !have(d))}
  {@const busy = busyName === a.name}
  <div class="glass">
    <div class="row" style="gap:12px;align-items:flex-start;flex-wrap:nowrap">
      <div class="icon-badge"><Icon name={appIcon(a)} size={22} /></div>
      <div style="flex:1;min-width:0">
        <div class="row" style="justify-content:space-between;gap:6px">
          <b>{a.title || a.name}</b>
          {#if has}
            <span class="row" style="font-size:11px;color:var(--success);gap:5px">
              <span class="status-dot run"></span> {t('مثبّت')}
            </span>
          {/if}
        </div>
        <div style="color:var(--muted);font-size:12px;margin:4px 0 8px">{a.desc}</div>
        <div class="row" style="gap:6px;flex-wrap:wrap;font-size:11px">
          {#each appDrivers(a) as d}
            <span class="tag" class:ok={have(d)}>
              <Icon name="plug" size={12} /> {d}
              {#if have(d)}<Icon name="check" size={12} />{/if}
            </span>
          {/each}
          {#if a.page_file}<span class="tag"><Icon name="image" size={12} /> {t('صفحة + canvas')}</span>{/if}
          <span style="color:var(--muted)">{((a.size || 0) + (a.page_size || 0)) / 1000 | 0}KB</span>
        </div>
      </div>
    </div>
    <div class="row" style="gap:6px;margin-top:12px;flex-wrap:wrap">
      <button class="btn sm" disabled={!!busyName} onclick={() => onInstall(a)}>
        <Icon name={has ? 'refresh' : 'download'} size={14} mono={true} />
        {busy ? '...' : has ? t('تحديث') : t('تثبيت')}{!has && missing.length ? ' (+' + missing.length + ' ' + t('درايفر') + ')' : ''}
      </button>
      {#if has}
        <button class="btn green sm" onclick={() => onRun(a)}><Icon name="play" size={13} mono={true} /> {t('تشغيل')}</button>
        <button class="btn ghost sm" onclick={() => onEdit(a)}><Icon name="edit" size={14} /> {t('تعديل')}</button>
        <button class="btn ghost icon-btn" title={t('حذف')} disabled={!!busyName} onclick={() => onRemove(a)}><Icon name="trash" size={14} /></button>
      {/if}
    </div>
  </div>
{/each}

{#if pages > 1}
  <div class="row" style="gap:5px;justify-content:center;flex-wrap:wrap;margin:6px 0">
    <button class="btn ghost sm" disabled={cur === 1} aria-label="prev" onclick={() => (pg = cur - 1)}><Icon name="chevron-right" size={14} /></button>
    {#each nums as n}
      {#if n === '…'}<span style="color:var(--muted)">…</span>
      {:else}<button class="btn sm {n === cur ? '' : 'ghost'}" style="min-width:32px" onclick={() => (pg = n)}>{n}</button>{/if}
    {/each}
    <button class="btn ghost sm" disabled={cur === pages} aria-label="next" onclick={() => (pg = cur + 1)}><Icon name="chevron-left" size={14} /></button>
  </div>
  <div style="text-align:center;color:var(--muted);font-size:11px">{shown.length} {t('تطبيق')} · {t('صفحة')} {cur}/{pages}</div>
{/if}
