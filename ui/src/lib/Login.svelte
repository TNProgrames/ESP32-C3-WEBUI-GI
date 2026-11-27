<script>
  import { api, session } from '../api.js';
  import PassInput from './PassInput.svelte';
  import Icon from './Icon.svelte';
  import { t } from '../store.svelte.js';
  let { onLogin } = $props();

  let username = $state('admin');
  let password = $state('');
  let msg = $state('');
  let ok = $state(false);
  let busy = $state(false);
  let fails = $state(0);
  let locked = $state(false);

  // إعادة ضبط كلمة المرور (تتطلب ضغط زر الجهاز)
  let resetting = $state(false);
  let resetInfo = $state('');

  async function submit(e) {
    e.preventDefault();
    busy = true; msg = ''; ok = false;
    try {
      const r = await api('/login', { username, password });
      session.set(r.token, r.role);
      onLogin(r.role);
    } catch (err) {
      if (err.status === 429) { locked = true; msg = t('محاولات كثيرة، انتظر 30 ثانية'); }
      else if (err.status === 401) { fails++; msg = t('اسم المستخدم أو كلمة المرور غير صحيحة'); }
      else msg = t('تعذر الاتصال بالجهاز');
    }
    busy = false;
  }

  async function startReset() {
    if (!confirm(t('سيُعاد ضبط كلمات المرور إلى الافتراضية. بعد الموافقة اضغط زر الجهاز الفيزيائي. هل تريد المتابعة؟'))) return;
    msg = ''; resetInfo = '';
    let pin = 0;
    try {
      const r = await api('/login/reset_request', {});
      pin = r.pin;
    } catch { ok = false; msg = t('تعذر بدء إعادة الضبط (تحقق من رقم PIN في الإعدادات)'); return; }
    resetting = true;
    for (let i = 0; i < 40; i++) {
      await new Promise((r) => setTimeout(r, 1000));
      try {
        const s = await api('/login/reset_status');
        if (s.state === 'done') {
          username = 'admin'; password = s.admin; // اقتراح كلمة المرور الافتراضية
          ok = true; resetInfo = t('تمت إعادة الضبط. كلمات المرور الافتراضية:') + ` admin / ${s.admin} ، user / ${s.user}`;
          fails = 0; locked = false; resetting = false;
          return;
        }
        if (s.state === 'waiting') resetInfo = t('اضغط زر الجهاز (GPIO {n}) خلال {s} ثانية').replace('{n}', pin).replace('{s}', s.left);
        else break;
      } catch {}
    }
    resetting = false; ok = false; resetInfo = ''; msg = t('انتهت المهلة، أعد المحاولة');
  }
</script>

<form class="glass" style="max-width:360px;margin:10vh auto 0" onsubmit={submit}>
  <div class="brand" style="margin-bottom:14px">
    <div class="brand-mark"><Icon name="lock" size={17} /></div>
    <div>
      <h2 style="margin:0">{t('تسجيل الدخول')}</h2>
      <span style="color:var(--muted);font-size:11px">ESP32-C3</span>
    </div>
  </div>
  <label for="u">{t('المستخدم')}</label>
  <select id="u" bind:value={username}>
    <option value="admin">admin</option>
    <option value="user">user</option>
  </select>
  <label for="p">{t('كلمة المرور')}</label>
  <PassInput id="p" bind:value={password} autocomplete="current-password" />
  <div style="margin-top:14px"><button class="btn" style="width:100%" disabled={busy || !password}>{t('دخول')}</button></div>
  <div class="msg" class:ok class:err={!ok}>{msg}</div>

  {#if fails >= 3 || locked}
    <div style="margin-top:12px;border-top:1px solid var(--border);padding-top:12px">
      <button type="button" class="btn ghost" style="width:100%" disabled={resetting} onclick={startReset}>
        <Icon name="refresh" size={15} /> {t('نسيت كلمة المرور؟')}
      </button>
    </div>
  {/if}
  {#if resetInfo}<div class="msg" class:ok style="color:var(--warning)">{resetInfo}</div>{/if}
</form>
