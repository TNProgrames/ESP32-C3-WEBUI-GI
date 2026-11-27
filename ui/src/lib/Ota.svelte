<script>
  import { session } from '../api.js';
  import { t } from '../store.svelte.js';
  import Icon from './Icon.svelte';

  let fwFile = $state(null);
  let fsFile = $state(null);
  let progress = $state(0);
  let busy = $state(false);
  let msg = $state('');
  let ok = $state(true);

  function send(file, type) {
    return new Promise((resolve, reject) => {
      const x = new XMLHttpRequest();
      x.open('POST', '/api/ota?type=' + type);
      x.setRequestHeader('User_Token', session.token);
      x.upload.onprogress = (e) => { if (e.lengthComputable) progress = Math.round((e.loaded / e.total) * 100); };
      x.onload = () => {
        if (x.status === 200) return resolve();
        let err = 'HTTP ' + x.status;
        try { err = JSON.parse(x.responseText).error || err; } catch {}
        reject(new Error(err));
      };
      x.onerror = () => reject(new Error(t('انقطع الاتصال أثناء الرفع')));
      const fd = new FormData();
      fd.append('file', file, file.name);
      x.send(fd);
    });
  }

  async function run(file, type) {
    if (!file) return;
    const warn = type === 'fs'
      ? t('تحديث الواجهة يستبدل نظام الملفات بالكامل ويمسح الإعدادات وكلمات المرور. هل تريد المتابعة؟')
      : t('سيُحدَّث البرنامج ثم يُعاد تشغيل الجهاز. هل تريد المتابعة؟');
    if (!confirm(warn)) return;
    busy = true; progress = 0; msg = ''; ok = true;
    try {
      await send(file, type);
      msg = t('تم التحديث. يُعاد تشغيل الجهاز، وستحتاج إلى تسجيل الدخول من جديد...');
      setTimeout(() => { session.clear(); location.reload(); }, 12000);
    } catch (e) {
      ok = false; msg = t('فشل التحديث: ') + e.message; busy = false;
    }
  }
</script>

<div class="glass">
  <h2><Icon name="upload" size={18} /> {t('تحديث البرنامج (firmware)')}</h2>
  <label for="fw">{t('الملف firmware-*.bin')}</label>
  <input id="fw" type="file" accept=".bin" onchange={(e) => (fwFile = e.currentTarget.files[0])} disabled={busy} />
  <div style="margin-top:14px">
    <button class="btn green" disabled={busy || !fwFile} onclick={() => run(fwFile, 'fw')}><Icon name="upload" size={15} mono={true} /> {t('رفع وتحديث')}</button>
  </div>
</div>

<div class="glass">
  <h2><Icon name="layers" size={18} /> {t('تحديث الواجهة (littlefs)')}</h2>
  <label for="fs">{t('الملف littlefs-*.bin')}</label>
  <input id="fs" type="file" accept=".bin" onchange={(e) => (fsFile = e.currentTarget.files[0])} disabled={busy} />
  <p style="font-size:12px;color:var(--warning);margin-top:8px">{t('تحذير: هذا يمسح الإعدادات وكلمات المرور المحفوظة ويعيدها للافتراضي.')}</p>
  <div style="margin-top:14px">
    <button class="btn green" disabled={busy || !fsFile} onclick={() => run(fsFile, 'fs')}><Icon name="upload" size={15} mono={true} /> {t('رفع وتحديث')}</button>
  </div>
</div>

{#if busy || msg}
  <div class="glass">
    {#if busy && ok && !msg}
      <div class="bar"><div style="width:{progress}%"></div></div>
      <div class="msg">{progress}%</div>
    {/if}
    <div class="msg" class:ok class:err={!ok}>{msg}</div>
  </div>
{/if}
