<script>
  import { api } from '../api.js';
  import { t } from '../store.svelte.js';
  import PassInput from './PassInput.svelte';
  import Icon from './Icon.svelte';

  let ap = $state(''); let up = $state(''); let em = $state('');
  let msg = $state(''); let ok = $state(true);

  async function save(e) {
    e.preventDefault();
    if (!ap && !up && !em) { ok = false; msg = t('أدخل كلمة مرور واحدة على الأقل'); return; }
    if (ap.length > 64 || up.length > 64) { ok = false; msg = t('فشل الحفظ'); return; }
    if (em && (em.length < 4 || em.length > 31)) { ok = false; msg = t('كلمة مرور الطوارئ بين 4 و 31 حرفًا'); return; }
    const payload = {};
    if (ap) payload.ap = ap;
    if (up) payload.up = up;
    if (em) payload.em = em;
    try {
      await api('/settings/save_pass', payload);
      ap = ''; up = ''; em = '';
      ok = true; msg = t('تم الحفظ');
    } catch { ok = false; msg = t('فشل الحفظ'); }
  }
</script>

<form class="glass" onsubmit={save}>
  <h2><Icon name="key" size={17} /> {t('كلمات المرور')}</h2>
  <p style="font-size:12px;color:var(--muted);margin-bottom:10px">{t('تُخزَّن كلمات المرور معمّاة ولا يمكن استرجاعها؛ اترك الحقل فارغًا للإبقاء على كلمة المرور الحالية.')}</p>
  <label for="a">{t('كلمة مرور admin')}</label>
  <PassInput id="a" bind:value={ap} placeholder={t('كلمة مرور جديدة (اتركها فارغة للإبقاء)')} autocomplete="new-password" />
  <label for="u">{t('كلمة مرور user')}</label>
  <PassInput id="u" bind:value={up} placeholder={t('كلمة مرور جديدة (اتركها فارغة للإبقاء)')} autocomplete="new-password" />
  <label for="em">{t('كلمة مرور صفحة الطوارئ (/em)')}</label>
  <PassInput id="em" bind:value={em} placeholder={t('4 أحرف على الأقل (اتركها فارغة للإبقاء)')} autocomplete="new-password" />
  <div style="margin-top:14px"><button class="btn green">{t('حفظ')}</button></div>
  <div class="msg" class:ok class:err={!ok}>{msg}</div>
</form>
