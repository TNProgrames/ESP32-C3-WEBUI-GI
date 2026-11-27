<script>
  import { ui, toggleLang, toggleTheme } from '../store.svelte.js';
  import Icon from './Icon.svelte';
</script>

<div class="fabs">
  <button class="fab" onclick={toggleLang} aria-label="language" title="العربية / English">{ui.lang === 'ar' ? 'EN' : 'ع'}</button>
  <button class="fab" onclick={toggleTheme} aria-label="theme" title="Dark / Light">
    <Icon name={ui.theme === 'dark' ? 'sun' : 'moon'} size={18} />
  </button>
</div>
