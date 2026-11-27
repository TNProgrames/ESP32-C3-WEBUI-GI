<script>
  import { api } from '../api.js';
  import { t } from '../store.svelte.js';
  import Icon from './Icon.svelte';

  let c = $state({ localhost: '', host: '' });
  let pin = $state('');
  let msg = $state(''); let ok = $state(true);

  $effect(() => {
    api('/settings/get_comm').then(r => { c = { ...r, localhost: String(r.localhost || '').replace(/\.local$/i, '') }; origHost = c.localhost; }).catch(() => {});
    api('/settings/get_pin').then(r => (pin = r.pin)).catch(() => {});
  });

  // قيود الاسم المحلي: a-z و0-9 وشرطة، بلا شرطة في الأول أو الآخر، حتى 32
  const cleanHost = (v) => v.toLowerCase().replace(/[^a-z0-9-]/g, '').slice(0, 32);
  const hostOk = $derived(/^[a-z0-9]([a-z0-9-]*[a-z0-9])?$/.test(c.localhost));

  let origHost = '';
  async function save(e) {
    e.preventDefault();
    if (!hostOk) { ok = false; msg = t('الاسم المحلي: أحرف إنجليزية صغيرة وأرقام وشرطة فقط، دون شرطة في الأول أو الآخر'); return; }
    try {
      await api('/settings/save_comm', c);
      await api('/settings/save_pin', { pin });
      ok = true; msg = t('تم الحفظ');
      if (c.localhost !== origHost) {            // تغيّر الاسم المحلي: الجهاز يعيد التشغيل، ثم نعيد فتح الصفحة
        msg = t('تم الحفظ — يُعاد تشغيل الجهاز لتطبيق الاسم المحلي...');
        const h = location.hostname, viaLocal = /\.local$/i.test(h);
        setTimeout(() => { location.href = viaLocal ? location.protocol + '//' + c.localhost + '.local' + location.pathname : location.href; }, 9000);
        origHost = c.localhost;
      }
    } catch { ok = false; msg = t('فشل الحفظ'); }
  }
</script>

<form class="glass" onsubmit={save}>
  <h2><Icon name="link" size={17} /> {t('الاتصال')}</h2>
  <label for="l">{t('المضيف المحلي')}</label>
  <div class="row" style="gap:6px;direction:ltr">
    <input id="l" style="flex:1;min-width:0" bind:value={c.localhost} oninput={() => (c.localhost = cleanHost(c.localhost))} maxlength="32" placeholder="esp32" autocapitalize="none" spellcheck="false" />
    <span style="color:var(--muted)">.local</span>
  </div>
  <label for="h">{t('مضيف الإنترنت')}</label>
  <input id="h" bind:value={c.host} maxlength="32" />
  <label for="pn">{t('رقم PIN لإعادة الضبط')}</label>
  <input id="pn" bind:value={pin} maxlength="32" />
  <div style="margin-top:14px"><button class="btn green">{t('حفظ')}</button></div>
  <div class="msg" class:ok class:err={!ok}>{msg}</div>
</form>
