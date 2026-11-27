<script>
  import { api } from '../api.js';
  import { t } from '../store.svelte.js';
  import PassInput from './PassInput.svelte';
  import Icon from './Icon.svelte';
  import SignalBars from './SignalBars.svelte';

  let f = $state({ mode: 'AP', ap_ssid: '', ap_pass: '', nets: [] });
  const MAX_NETS = 6;
  let found = $state([]);
  let scanning = $state(false);
  let msg = $state(''); let ok = $state(true);

  $effect(() => { api('/settings/get_wifi').then(r => (f = { ...r, nets: r.nets || [] })).catch(() => {}); });

  async function scan() {
    scanning = true; found = [];
    for (let i = 0; i < 10; i++) {
      try {
        const r = await api('/settings/scan_wifi');
        if (Array.isArray(r)) { found = r; break; }
      } catch {}
      await new Promise(r => setTimeout(r, 1500));
    }
    scanning = false;
  }

  function addNet(ssid = '') {
    if (f.nets.length >= MAX_NETS) return;
    const i = f.nets.findIndex((n) => n.ssid === ssid);
    if (ssid && i >= 0) return;
    f.nets.push({ ssid, pass: '' });
  }
  const rmNet = (i) => f.nets.splice(i, 1);
  const upNet = (i) => { if (i > 0) f.nets.splice(i - 1, 0, ...f.nets.splice(i, 1)); };

  async function save(e) {
    e.preventDefault();
    try {
      await api('/settings/save_wifi', f);
      ok = true; msg = t('تم الحفظ. سيُعاد ضبط الواي فاي وقد ينقطع الاتصال لحظات.');
    } catch { ok = false; msg = t('فشل الحفظ'); }
  }
</script>

<form class="glass" onsubmit={save}>
  <h2><Icon name="wifi" size={17} /> {t('إعدادات الواي فاي')}</h2>
  <label for="m">{t('الوضع')}</label>
  <select id="m" bind:value={f.mode}>
    <option value="AP">{t('AP (نقطة وصول)')}</option>
    <option value="STA">{t('STA (اتصال بشبكة)')}</option>
    <option value="AP_STA">AP + STA</option>
  </select>

  {#if f.mode !== 'AP'}
    <label>{t('الشبكات المحفوظة (تُجرَّب بالترتيب والأقوى أولاً؛ إن فشلت كلها يدخل وضع الاسترداد)')}</label>
    {#each f.nets as n, i}
      <div class="net" style="display:block;cursor:default">
        <div class="row" style="gap:6px;flex-wrap:nowrap">
          <input style="flex:1;min-width:0" placeholder={t('اسم الشبكة')} bind:value={n.ssid} maxlength="32" />
          <button type="button" class="btn ghost icon-btn" title={t('أعلى')} onclick={() => upNet(i)} disabled={i === 0}><Icon name="arrow-up" size={15} /></button>
          <button type="button" class="btn ghost icon-btn" title={t('حذف')} onclick={() => rmNet(i)}><Icon name="trash" size={15} /></button>
        </div>
        <div style="margin-top:6px"><PassInput bind:value={n.pass} placeholder={t('كلمة المرور')} /></div>
      </div>
    {/each}
    <div class="row" style="gap:6px;margin-top:8px">
      <button type="button" class="btn ghost sm" onclick={() => addNet()} disabled={f.nets.length >= MAX_NETS}><Icon name="plus" size={14} /> {t('إضافة شبكة')}</button>
      <button type="button" class="btn ghost sm" onclick={scan} disabled={scanning}><Icon name="search" size={14} /> {scanning ? '...' : t('مسح')}</button>
    </div>
    {#each found as n}
      <div class="net" role="button" tabindex="0" onclick={() => addNet(n.ssid)} onkeydown={(e) => e.key === 'Enter' && addNet(n.ssid)}>
        <span class="row" style="gap:6px">{n.ssid} {#if n.secure}<Icon name="lock" size={13} />{/if}</span>
        <span class="row" style="gap:8px;flex-wrap:nowrap;direction:ltr"><SignalBars rssi={n.rssi} /><span style="color:var(--muted)">{n.rssi} dBm</span></span>
      </div>
    {/each}
  {/if}

  {#if f.mode !== 'STA'}
    <label for="as">{t('اسم نقطة الوصول (AP)')}</label>
    <input id="as" bind:value={f.ap_ssid} />
    <label for="ap">{t('كلمة مرور نقطة الوصول (8 أحرف على الأقل)')}</label>
    <PassInput id="ap" bind:value={f.ap_pass} />
  {/if}

  <div style="margin-top:14px"><button class="btn green">{t('حفظ وتطبيق')}</button></div>
  <div class="msg" class:ok class:err={!ok}>{msg}</div>
</form>
