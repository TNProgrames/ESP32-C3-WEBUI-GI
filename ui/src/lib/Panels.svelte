<script>
  import { t } from '../store.svelte.js';
  import { pn, startPanels, stopPanels, sendEvent, keyOf } from '../panels.svelte.js';
  import PageFrame from './PageFrame.svelte';
  import Icon from './Icon.svelte';

  $effect(() => { startPanels(); return stopPanels; });

  const val = (p, w) => pn.vals[keyOf(p.id, w.id)] ?? w.value;
  const show = (v) => (v === null || v === undefined ? '' : String(v));
  function set(p, w, v) { pn.vals[keyOf(p.id, w.id)] = v; sendEvent(p.id, w.id, v); }
</script>

<div class="glass">
  <h2 style="margin:0"><Icon name="layout" size={17} /> {t('اللوحات')}</h2>
  <p style="color:var(--muted);font-size:13px;margin:6px 0 0">
    {t('اللوحات التي تنشئها السكريبتات بـ ui.panel(...). شغّل مثالًا من تبويب المختبر لتظهر هنا.')}
  </p>
</div>

{#if !pn.panels.length}
  <div class="glass" style="text-align:center;color:var(--muted)">{t('لا توجد لوحات الآن')}</div>
{/if}

{#each pn.panels as p (p.id)}
  <div class="glass">
    <div class="row" style="justify-content:space-between;margin-bottom:10px">
      <strong>{p.title}</strong>
      <span style="color:var(--muted);font-size:12px">{p.script}</span>
    </div>

    {#each p.widgets as w (w.id)}
      <div class="row" style="justify-content:space-between;margin:8px 0;flex-wrap:nowrap;gap:12px">
        {#if w.type === 'label'}
          <span style="direction:ltr;unicode-bidi:plaintext">{show(val(p, w))}</span>
        {:else if w.type === 'button'}
          <button class="btn" onclick={() => sendEvent(p.id, w.id, true)}>{w.label}</button>
        {:else if w.type === 'switch'}
          <span>{w.label}</span>
          <label class="switch"><input type="checkbox" checked={!!val(p, w)} onchange={(e) => set(p, w, e.currentTarget.checked)} /><span class="slider"></span></label>
        {:else if w.type === 'slider'}
          <div style="flex:1">
            <div class="row" style="justify-content:space-between;font-size:13px"><span>{w.label}</span><b>{show(val(p, w))}</b></div>
            <input type="range" min={w.min} max={w.max} step="any" value={val(p, w)} style="padding:0"
              oninput={(e) => (pn.vals[keyOf(p.id, w.id)] = Number(e.currentTarget.value))}
              onchange={(e) => set(p, w, Number(e.currentTarget.value))} />
          </div>
        {:else if w.type === 'progress'}
          <div style="flex:1">
            <div class="row" style="justify-content:space-between;font-size:13px"><span>{w.label}</span><b>{show(val(p, w))}</b></div>
            <progress style="width:100%" max={w.max || 100} value={Number(val(p, w)) || 0}></progress>
          </div>
        {:else if w.type === 'input'}
          <div style="flex:1">
            <div style="font-size:13px;margin-bottom:4px">{w.label}</div>
            <input value={show(val(p, w))} maxlength="30" onchange={(e) => set(p, w, e.currentTarget.value)} />
          </div>
        {/if}
      </div>
    {/each}

    {#if p.page}<PageFrame panel={p.id} path={p.page} />{/if}
  </div>
{/each}
