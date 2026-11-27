<script>
  import { session } from './api.js';
  import { t } from './store.svelte.js';
  import Login from './lib/Login.svelte';
  import Dashboard from './lib/Dashboard.svelte';
  import Wifi from './lib/Wifi.svelte';
  import Comm from './lib/Comm.svelte';
  import Passwords from './lib/Passwords.svelte';
  import Backup from './lib/Backup.svelte';
  import Terminal from './lib/Terminal.svelte';
  import Ota from './lib/Ota.svelte';
  import Lab from './lib/Lab.svelte';
  import Drivers from './lib/Drivers.svelte';
  import Panels from './lib/Panels.svelte';
  import Toggles from './lib/Toggles.svelte';
  import Icon from './lib/Icon.svelte';

  let role = $state(session.role);
  let tab = $state('dash');

  const tabs = [
    { id: 'dash', icon: 'activity', label: 'الحالة', admin: false },
    { id: 'wifi', icon: 'wifi', label: 'الواي فاي', admin: true },
    { id: 'comm', icon: 'link', label: 'الاتصال', admin: true },
    { id: 'pass', icon: 'key', label: 'كلمات المرور', admin: true },
    { id: 'backup', icon: 'save', label: 'نسخ احتياطي', admin: true },
    { id: 'term', icon: 'terminal', label: 'الطرفية', admin: true },
    { id: 'lab', icon: 'flask', label: 'مختبر Lua', admin: true },
    { id: 'drivers', icon: 'store', label: 'متجر التطبيقات', admin: true },
    { id: 'panels', icon: 'layout', label: 'اللوحات', admin: true },
    { id: 'ota', icon: 'upload', label: 'تحديث', admin: true }
  ];

  function onLogin(r) { role = r; tab = 'dash'; }
  function logout() { session.clear(); role = ''; }
</script>

<div class="container">
  {#if !role}
    <Login {onLogin} />
  {:else}
    <div class="glass topbar">
      <div class="brand">
        <div class="brand-mark"><Icon name="cpu" size={20} /></div>
        <div>
          <strong style="display:block;font-size:14px;line-height:1.2">ESP32-C3</strong>
          <span style="color:var(--muted);font-size:11px">{role === 'admin' ? t('مدير') : t('مستخدم')}</span>
        </div>
      </div>
      <button class="btn ghost sm" onclick={logout}><Icon name="logout" size={15} /> {t('خروج')}</button>
    </div>

    <div class="tabs">
      {#each tabs.filter(x => !x.admin || role === 'admin') as x}
        <button class="btn tab-btn" class:active={tab === x.id} onclick={() => (tab = x.id)}>
          <Icon name={x.icon} size={16} /> {t(x.label)}
        </button>
      {/each}
    </div>

    {#if tab === 'dash'}<Dashboard {role} />
    {:else if tab === 'wifi'}<Wifi />
    {:else if tab === 'comm'}<Comm />
    {:else if tab === 'pass'}<Passwords />
    {:else if tab === 'backup'}<Backup />
    {:else if tab === 'term'}<Terminal />
    {:else if tab === 'lab'}<Lab go={(id) => (tab = id)} />
    {:else if tab === 'drivers'}<Drivers go={(id) => (tab = id)} />
    {:else if tab === 'panels'}<Panels />
    {:else if tab === 'ota'}<Ota />
    {/if}
  {/if}
</div>

<Toggles />
