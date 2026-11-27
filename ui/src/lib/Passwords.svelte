<script>
  import { api } from '../api.js';
  import { t } from '../store.svelte.js';
  import PassInput from './PassInput.svelte';
  import Icon from './Icon.svelte';

  let ap = $state(''); let up = $state('');
  let msg = $state(''); let ok = $state(true);

  $effect(() => { api('/settings/get_pass').then(r => { ap = r.ap; up = r.up; }).catch(() => {}); });

  async function save(e) {
    e.preventDefault();
    if (!ap || !up) { ok = false; msg = t('الحقلان مطلوبان'); return; }
    try { await api('/settings/save_pass', { ap, up }); ok = true; msg = t('تم الحفظ'); }
    catch { ok = false; msg = t('فشل الحفظ'); }
  }
</script>

<form class="glass" onsubmit={save}>
  <h2><Icon name="key" size={17} /> {t('كلمات المرور')}</h2>
  <label for="a">{t('كلمة مرور admin')}</label>
  <PassInput id="a" bind:value={ap} />
  <label for="u">{t('كلمة مرور user')}</label>
  <PassInput id="u" bind:value={up} />
  <div style="margin-top:14px"><button class="btn green">{t('حفظ')}</button></div>
  <div class="msg" class:ok class:err={!ok}>{msg}</div>
</form>
