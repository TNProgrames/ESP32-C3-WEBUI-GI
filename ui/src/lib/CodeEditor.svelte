<script>
  import { highlight } from '../highlight.js';

  // محرر كود: طبقة ملوّنة خلف textarea شفاف. يدعم Tab والمسافة البادئة التلقائية وإبراز سطر الخطأ
  let { value = $bindable(''), lang = 'lua', errLine = 0, height = 240 } = $props();

  let ta = $state(), pre = $state(), gut = $state();
  let st = $state(0);
  const html = $derived(highlight(value, lang) + '\n');
  const nums = $derived(Array.from({ length: value.split('\n').length }, (_, i) => i + 1).join('\n'));

  function sync() {
    if (!ta) return;
    st = ta.scrollTop;
    if (pre) { pre.scrollTop = ta.scrollTop; pre.scrollLeft = ta.scrollLeft; }
    if (gut) gut.scrollTop = ta.scrollTop;
  }
  $effect(() => { value; queueMicrotask(sync); });

  function insert(text, selectAfter) {
    const s = ta.selectionStart, e = ta.selectionEnd;
    ta.setRangeText(text, s, e, 'end');
    value = ta.value;
  }

  function keydown(e) {
    if (e.key === 'Tab') {
      e.preventDefault();
      const s = ta.selectionStart, en = ta.selectionEnd;
      if (s !== en && value.slice(s, en).includes('\n')) {
        // مسافة بادئة لعدة أسطر (Shift = إزالة)
        const ls = value.lastIndexOf('\n', s - 1) + 1;
        const block = value.slice(ls, en).split('\n');
        const out = block.map((l) => (e.shiftKey ? l.replace(/^ {1,2}/, '') : '  ' + l)).join('\n');
        ta.setRangeText(out, ls, en, 'select');
        value = ta.value;
      } else if (e.shiftKey) {
        const ls = value.lastIndexOf('\n', s - 1) + 1;
        if (value.slice(ls, ls + 2) === '  ') { ta.setRangeText('', ls, ls + 2, 'preserve'); value = ta.value; }
      } else insert('  ');
    } else if (e.key === 'Enter' && !e.ctrlKey && !e.metaKey && ta.selectionStart === ta.selectionEnd) {
      // مسافة بادئة تلقائية كالسطر الحالي، وزيادة بعد فتح كتلة
      const pos = ta.selectionStart;
      const ls = value.lastIndexOf('\n', pos - 1) + 1;
      const line = value.slice(ls, pos);
      const indent = line.match(/^\s*/)[0];
      const opens = lang === 'html'
        ? /<(?!\/|!|meta|br|hr|img|input|link)[A-Za-z][^>]*[^\/]>\s*$/.test(line)
        : /(\bthen|\bdo|\belse|\brepeat|\bfunction\b[^\n]*\)|\{)\s*(--.*)?$/.test(line);
      e.preventDefault();
      insert('\n' + indent + (opens ? '  ' : ''));
    }
  }
</script>

<div class="ce" style="height:{height}px">
  <pre class="gut" bind:this={gut} aria-hidden="true">{nums}</pre>
  <div class="body">
    <pre class="hl" bind:this={pre} aria-hidden="true">{@html html}</pre>
    {#if errLine > 0}<div class="errline" style="top:{8 + (errLine - 1) * 18 - st}px"></div>{/if}
    <textarea bind:this={ta} bind:value onscroll={sync} onkeydown={keydown}
      spellcheck="false" autocapitalize="off" autocomplete="off" autocorrect="off" wrap="off"></textarea>
  </div>
</div>

<style>
  .ce { display: flex; direction: ltr; text-align: left; border: 1px solid var(--border); border-radius: 12px; background: var(--input); overflow: hidden; position: relative; }
  .gut, .hl, textarea { margin: 0; font: 12.5px/18px 'Courier New', ui-monospace, monospace; tab-size: 2; -moz-tab-size: 2; letter-spacing: 0; }
  .gut { padding: 8px 8px 8px 10px; min-width: 2.4em; text-align: right; color: var(--muted); opacity: .6; overflow: hidden; user-select: none; border-right: 1px solid var(--border); }
  .body { position: relative; flex: 1; min-width: 0; }
  .hl, textarea { position: absolute; inset: 0; padding: 8px 10px; width: 100%; height: 100%; box-sizing: border-box; white-space: pre; overflow: auto; border: 0; }
  .hl { overflow: hidden; color: var(--text); pointer-events: none; }
  textarea { background: transparent; color: transparent; -webkit-text-fill-color: transparent; caret-color: var(--text); resize: none; outline: none; appearance: none; -webkit-appearance: none; border-radius: 0; direction: ltr; text-align: left; }
  textarea::selection { background: rgba(99, 102, 241, .35); -webkit-text-fill-color: transparent; }
  .errline { position: absolute; left: 0; right: 0; height: 18px; background: rgba(239, 68, 68, .28); border-left: 3px solid #ef4444; pointer-events: none; }

  .hl :global(.tk-k) { color: #c792ea; font-weight: 600; }
  .hl :global(.tk-l) { color: #82aaff; font-weight: 600; }
  .hl :global(.tk-b) { color: #7dcfff; }
  .hl :global(.tk-fn) { color: #ffcb6b; }
  .hl :global(.tk-p) { color: #f07178; }
  .hl :global(.tk-s) { color: #a5e075; }
  .hl :global(.tk-n) { color: #f78c6c; }
  .hl :global(.tk-c) { color: #7a8394; font-style: italic; }
  .hl :global(.tk-o) { color: #89ddff; }
  .hl :global(.tk-t) { color: #f07178; }
  .hl :global(.tk-a) { color: #ffcb6b; }
  :global([data-theme="light"]) .hl :global(.tk-k) { color: #7c3aed; }
  :global([data-theme="light"]) .hl :global(.tk-l) { color: #1d4ed8; }
  :global([data-theme="light"]) .hl :global(.tk-b) { color: #0369a1; }
  :global([data-theme="light"]) .hl :global(.tk-fn) { color: #b45309; }
  :global([data-theme="light"]) .hl :global(.tk-p) { color: #be123c; }
  :global([data-theme="light"]) .hl :global(.tk-s) { color: #15803d; }
  :global([data-theme="light"]) .hl :global(.tk-n) { color: #c2410c; }
  :global([data-theme="light"]) .hl :global(.tk-c) { color: #6b7280; }
  :global([data-theme="light"]) .hl :global(.tk-o) { color: #0e7490; }
  :global([data-theme="light"]) .hl :global(.tk-t) { color: #be123c; }
  :global([data-theme="light"]) .hl :global(.tk-a) { color: #b45309; }
</style>
