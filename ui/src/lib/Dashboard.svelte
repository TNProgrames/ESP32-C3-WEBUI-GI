<script>
  import { api, session } from '../api.js';
  import { t } from '../store.svelte.js';
  import Icon from './Icon.svelte';
  import SignalBars from './SignalBars.svelte';
  let { role } = $props();

  let ram = $state(0);
  let storage = $state(0);
  let version = $state('');
  let uptime = $state(0);
  let temp = $state(null);
  let wake = $state('');
  let slSecs = $state(60);
  let slPins = $state([]);
  let slLevel = $state(1);
  let net = $state(null);
  let msg = $state('');

  function fmt(s) {
    const d = Math.floor(s / 86400), h = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
    return `${d}d ${String(h).padStart(2, '0')}h ${String(m).padStart(2, '0')}m ${String(s % 60).padStart(2, '0')}s`;
  }

  async function refresh() {
    try {
      const u = await api('/hdusage');
      ram = parseFloat(u.RamUsage) || 0;
      storage = parseFloat(u.StorageUsage) || 0;
      version = u.Version || '';
      uptime = u.Uptime || 0;
      temp = typeof u.Temp === 'number' ? u.Temp : null;
      wake = u.Wake === 'timer' ? t('مؤقت') : u.Wake === 'gpio' ? t('رجل') + ' GPIO' + u.WakePin : t('إقلاع عادي');
    } catch {}
    try { net = await api('/settings/get_stat'); } catch { net = null; }
  }

  async function sleepNow() {
    if (!slPins.length && !(slSecs > 0)) { msg = t('حدّد مؤقتًا أو رجل استيقاظ'); return; }
    if (!confirm(t('سيدخل الجهاز النوم العميق وينقطع الاتصال. متابعة؟'))) return;
    try {
      await api(`/settings/sleep?secs=${slSecs > 0 ? Math.floor(slSecs) : 0}&pins=${slPins.join(',')}&level=${slLevel}`, {});
      msg = t('الجهاز في النوم العميق. يستيقظ بالمؤقت أو رجل الاستيقاظ أو زر RESET.');
    } catch { msg = t('تعذّر'); }
  }

  async function restart() {
    if (!confirm(t('هل تريد إعادة تشغيل الجهاز؟'))) return;
    try {
      await api('/settings/restart', {});
      msg = t('يُعاد تشغيل الجهاز، سجّل الدخول من جديد...');
      setTimeout(() => { session.clear(); location.reload(); }, 10000);
    } catch {}
  }

  $effect(() => {
    refresh();
    const timer = setInterval(refresh, 4000);
    return () => clearInterval(timer);
  });
</script>

<div class="grid-2">
  <div class="glass">
    <h2><Icon name="activity" size={17} /> {t('استخدام الموارد')}</h2>
    <div style="margin-bottom:12px">
      <div class="row" style="justify-content:space-between;font-size:12px;margin-bottom:5px">
        <span style="color:var(--muted)">{t('الذاكرة (RAM)')}</span>
        <b>{ram}%</b>
      </div>
      <div class="bar"><div style="width:{ram}%"></div></div>
    </div>
    <div>
      <div class="row" style="justify-content:space-between;font-size:12px;margin-bottom:5px">
        <span style="color:var(--muted)">{t('التخزين')}</span>
        <b>{storage}%</b>
      </div>
      <div class="bar"><div style="width:{storage}%;background:var(--success)"></div></div>
    </div>
  </div>

  <div class="glass">
    <h2><Icon name="globe" size={17} /> {t('الشبكة')}</h2>
    {#if net}
      <div class="stat-row">
        <div class="stat-item"><span class="stat-label">IP</span><span class="stat-val" style="direction:ltr;text-align:start">{net.ip}</span></div>
        {#if net.ssid}<div class="stat-item"><span class="stat-label">{t('الشبكة')}</span><span class="stat-val">{net.ssid}</span></div>{/if}
        {#if net.status}<div class="stat-item"><span class="stat-label">{t('الحالة')}</span><span class="stat-val">{net.status}</span></div>{/if}
        {#if net.rssi}<div class="stat-item"><span class="stat-label">{t('الإشارة')}</span><span class="stat-val row" style="gap:8px;flex-wrap:nowrap"><SignalBars rssi={net.rssi} /><span style="direction:ltr">{net.rssi} dBm</span></span></div>{/if}
      </div>
    {:else}
      <span style="color:var(--muted);font-size:13px">{t('لا توجد بيانات (قد يكون الواي فاي قيد الضبط)')}</span>
    {/if}
  </div>
</div>

<div class="glass">
  <h2><Icon name="info" size={17} /> {t('الحالة')}</h2>
  <div class="stat-row">
    <div class="stat-item"><span class="stat-label">{t('الإصدار')}</span><span class="stat-val">{version || '-'}</span></div>
    <div class="stat-item"><span class="stat-label"><Icon name="clock" size={12} /> {t('مدة التشغيل')}</span><span class="stat-val" style="direction:ltr;text-align:start">{fmt(uptime)}</span></div>
    {#if temp !== null}
      <div class="stat-item">
        <span class="stat-label"><Icon name="thermometer" size={12} /> {t('حرارة المعالج')}</span>
        <span class="stat-val" style={temp > 70 ? 'color:var(--danger)' : ''}>{temp.toFixed(1)}°C</span>
      </div>
    {/if}
    <div class="stat-item"><span class="stat-label"><Icon name="clock" size={12} /> {t('سبب آخر إقلاع')}</span><span class="stat-val">{wake || '-'}</span></div>
  </div>

  {#if role === 'admin'}
    <div style="margin-top:16px;padding-top:14px;border-top:1px solid var(--border)">
      <div class="sub-title" style="margin-bottom:8px"><Icon name="moon" size={15} /> {t('النوم العميق')}</div>
      <div class="row" style="gap:12px;align-items:flex-end;font-size:13px">
        <div style="min-width:130px">
          <label for="sl-secs" style="margin-top:0">{t('مؤقت (ثانية، 0 = بلا)')}</label>
          <input id="sl-secs" type="number" min="0" max="2592000" style="width:120px" bind:value={slSecs} />
        </div>
        <div style="min-width:150px">
          <label for="sl-lvl" style="margin-top:0">{t('مستوى الاستيقاظ')}</label>
          <select id="sl-lvl" bind:value={slLevel}><option value={1}>{t('عالٍ')} (1)</option><option value={0}>{t('منخفض')} (0)</option></select>
        </div>
      </div>
      <div class="row" style="gap:12px;margin-top:10px;font-size:13px">
        <span style="color:var(--muted);font-size:12px">{t('أرجل الاستيقاظ')}:</span>
        {#each [0, 1, 2, 3, 4, 5] as n}
          <label class="row" style="margin:0;gap:5px;cursor:pointer;color:var(--text)"><input type="checkbox" value={n} bind:group={slPins} /> GPIO {n}</label>
        {/each}
      </div>
      <div class="row" style="margin-top:12px">
        <button class="btn ghost" onclick={sleepNow}><Icon name="moon" size={15} /> {t('نوم عميق الآن')}</button>
        <button class="btn ghost" onclick={restart}><Icon name="refresh" size={15} /> {t('إعادة تشغيل الجهاز')}</button>
      </div>
    </div>
  {/if}
  {#if msg}<div class="msg ok">{msg}</div>{/if}
</div>
