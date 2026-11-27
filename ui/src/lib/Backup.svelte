<script>
  import { api, session } from '../api.js';
  import { t } from '../store.svelte.js';
  import Icon from './Icon.svelte';

  let msg = $state('');
  let ok = $state(true);
  let busy = $state(false);
  let file = $state(null);

  async function download() {
    busy = true; msg = '';
    try {
      const res = await fetch('/settings/backup', { headers: { User_Token: session.token, User_Role: session.role } });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const blob = await res.blob();
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = 'esp32-backup-' + new Date().toISOString().slice(0, 10) + '.json';
      a.click();
      URL.revokeObjectURL(a.href);
      ok = true; msg = t('تم تنزيل النسخة الاحتياطية');
    } catch (e) { ok = false; msg = t('فشل التنزيل: ') + e.message; }
    busy = false;
  }

  async function restore() {
    if (!file) return;
    if (!confirm(t('ستُستبدل الإعدادات وكلمات المرور الحالية بمحتوى الملف ويُعاد تشغيل الجهاز. متابعة؟'))) return;
    busy = true; msg = '';
    try {
      const obj = JSON.parse(await file.text());
      await api('/settings/restore', obj);
      ok = true; msg = t('تمت الاستعادة. يُعاد تشغيل الجهاز، سجّل الدخول من جديد...');
      setTimeout(() => { session.clear(); location.reload(); }, 8000);
    } catch (e) {
      ok = false; msg = e instanceof SyntaxError ? t('الملف ليس JSON صالحًا') : t('فشلت الاستعادة (تحقق من الملف)');
      busy = false;
    }
  }
</script>

<div class="glass">
  <h2><Icon name="save" size={18} /> {t('نسخة احتياطية')}</h2>
  <p style="font-size:12px;color:var(--muted);margin-bottom:12px">{t('تحفظ الإعدادات (الواي فاي والشبكات المحفوظة، الاتصال، PIN) وبيانات التحقق في ملف JSON واحد.')}</p>
  <button class="btn green" onclick={download} disabled={busy}><Icon name="download" size={15} mono={true} /> {t('تنزيل النسخة الاحتياطية')}</button>
  <p style="font-size:12px;color:var(--warning);margin-top:10px">{t('تنبيه: الملف يحتوي إعدادات الشبكات وبيانات التحقق المعمّاة، فاحفظه في مكان آمن.')}</p>
</div>

<div class="glass">
  <h2><Icon name="refresh" size={18} /> {t('استعادة')}</h2>
  <label for="r">{t('ملف النسخة الاحتياطية (.json)')}</label>
  <input id="r" type="file" accept=".json,application/json" onchange={(e) => (file = e.currentTarget.files[0])} disabled={busy} />
  <div style="margin-top:14px"><button class="btn" onclick={restore} disabled={busy || !file}><Icon name="upload" size={15} mono={true} /> {t('استعادة وإعادة التشغيل')}</button></div>
</div>

<div class="msg" class:ok class:err={!ok}>{msg}</div>
