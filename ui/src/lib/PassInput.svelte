<script>
  import Icon from './Icon.svelte';
  let { value = $bindable(''), id = undefined, placeholder = '', autocomplete = 'off' } = $props();
  let show = $state(false);
</script>

<div class="row" style="gap:6px;align-items:stretch;flex-wrap:nowrap">
  <input {id} style="flex:1;min-width:0" type={show ? 'text' : 'password'} bind:value {placeholder} {autocomplete} />
  <button type="button" class="btn ghost icon-btn" aria-label="show password" aria-pressed={show} onclick={() => (show = !show)}>
    <Icon name={show ? 'eye-off' : 'eye'} size={16} />
  </button>
</div>
