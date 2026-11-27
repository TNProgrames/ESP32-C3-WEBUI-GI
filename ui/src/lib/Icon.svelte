<script>
  let { name, size = 16, mono = false } = $props();
</script>

<svg
  width={size}
  height={size}
  viewBox="0 0 24 24"
  fill="none"
  aria-hidden="true"
  style="flex:none;display:inline-block;vertical-align:middle"
>
  {#if name === 'activity'}
    <rect x="3" y="12" width="4.5" height="9" rx="1.8" fill={mono ? 'currentColor' : '#34d399'} />
    <rect x="9.75" y="7" width="4.5" height="14" rx="1.8" fill={mono ? 'currentColor' : '#10b981'} />
    <rect x="16.5" y="3" width="4.5" height="18" rx="1.8" fill={mono ? 'currentColor' : '#22d3ee'} />
  {:else if name === 'wifi'}
    <path d="M2.1 9.2a15.5 15.5 0 0 1 19.8 0l-1.9 2.2a12.6 12.6 0 0 0-16 0z" fill={mono ? 'currentColor' : '#38bdf8'} />
    <path d="M5.6 13.1a10.2 10.2 0 0 1 12.8 0l-1.9 2.2a7.3 7.3 0 0 0-9 0z" fill={mono ? 'currentColor' : '#0ea5e9'} />
    <path d="M9.1 17a5 5 0 0 1 5.8 0L12 20.5z" fill={mono ? 'currentColor' : '#6366f1'} />
  {:else if name === 'link'}
    <path d="M10.6 13.4a4.5 4.5 0 0 0 6.4 0l3.2-3.2a4.5 4.5 0 0 0-6.4-6.4l-1.6 1.6" stroke={mono ? 'currentColor' : '#22d3ee'} stroke-width="2.6" stroke-linecap="round" />
    <path d="M13.4 10.6a4.5 4.5 0 0 0-6.4 0l-3.2 3.2a4.5 4.5 0 0 0 6.4 6.4l1.6-1.6" stroke={mono ? 'currentColor' : '#6366f1'} stroke-width="2.6" stroke-linecap="round" />
  {:else if name === 'key'}
    <path fill-rule="evenodd" clip-rule="evenodd" d="M8 7a6 6 0 1 0 5.66 8H17v2.5a1 1 0 0 0 1 1h2a1 1 0 0 0 1-1V15h1a1 1 0 0 0 1-1v-2a1 1 0 0 0-1-1h-8.34A6 6 0 0 0 8 7Zm-2.5 6a1.5 1.5 0 1 1 3 0 1.5 1.5 0 0 1-3 0Z" fill={mono ? 'currentColor' : '#f59e0b'} />
    <circle cx="7" cy="13" r="1.4" fill={mono ? 'none' : '#fef3c7'} />
  {:else if name === 'save'}
    <path d="M4 3h13l4 4v13a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 1-2Z" fill={mono ? 'currentColor' : '#3b82f6'} />
    <rect x="7" y="3" width="8" height="5" rx="1" fill={mono ? 'rgba(255,255,255,.35)' : '#93c5fd'} />
    <rect x="6.5" y="13" width="11" height="8" rx="1.2" fill={mono ? 'rgba(255,255,255,.25)' : '#dbeafe'} />
  {:else if name === 'terminal'}
    <rect x="2" y="3.5" width="20" height="17" rx="3.2" fill={mono ? 'currentColor' : '#0f766e'} />
    <rect x="3.5" y="5" width="17" height="14" rx="2.2" fill={mono ? 'rgba(0,0,0,.15)' : '#064e3b'} />
    <path d="M7 9.5 10.5 12 7 14.5" stroke={mono ? '#fff' : '#34d399'} stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" />
    <line x1="12.5" y1="15" x2="17" y2="15" stroke={mono ? '#fff' : '#6ee7b7'} stroke-width="2.2" stroke-linecap="round" />
  {:else if name === 'flask'}
    <path d="M9 2.5h6v2h-6z" fill={mono ? 'currentColor' : '#a855f7'} />
    <path d="M9.5 4.5h5v4.3l4.9 8.5A2.5 2.5 0 0 1 17.2 21H6.8a2.5 2.5 0 0 1-2.2-3.7l4.9-8.5V4.5Z" fill={mono ? 'currentColor' : '#7e22ce'} />
    <path d="M7.6 13.5h8.8l2 3.5a1.5 1.5 0 0 1-1.3 2.2H6.9a1.5 1.5 0 0 1-1.3-2.2l2-3.5Z" fill={mono ? 'rgba(255,255,255,.3)' : '#22d3ee'} />
    <circle cx="10.5" cy="16.2" r="1" fill={mono ? '#fff' : '#cffafe'} />
  {:else if name === 'store'}
    <path d="M8.5 7V6a3.5 3.5 0 0 1 7 0v1" stroke={mono ? 'currentColor' : '#fb923c'} stroke-width="2.2" stroke-linecap="round" />
    <path d="M4.2 7h15.6l1.1 12.2A2 2 0 0 1 18.9 21H5.1a2 2 0 0 1-2-1.8L4.2 7Z" fill={mono ? 'currentColor' : '#f97316'} />
    <path d="M9 11a3 3 0 0 0 6 0" stroke={mono ? '#fff' : '#ffedd5'} stroke-width="2" stroke-linecap="round" />
  {:else if name === 'layout'}
    <rect x="2.5" y="3" width="19" height="18" rx="3" fill={mono ? 'currentColor' : '#0d9488'} />
    <rect x="4.5" y="5" width="15" height="3.2" rx="1" fill={mono ? 'rgba(255,255,255,.35)' : '#5eead4'} />
    <rect x="4.5" y="9.8" width="5.2" height="9.2" rx="1" fill={mono ? 'rgba(255,255,255,.25)' : '#2dd4bf'} />
    <rect x="11.2" y="9.8" width="8.3" height="9.2" rx="1" fill={mono ? 'rgba(255,255,255,.2)' : '#99f6e4'} />
  {:else if name === 'upload'}
    <path d="M4 16v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3" stroke={mono ? 'currentColor' : '#6366f1'} stroke-width="2.4" stroke-linecap="round" />
    <path d="M12 2.8 6.3 8.8h3.7V15h4V8.8h3.7L12 2.8Z" fill={mono ? 'currentColor' : '#22d3ee'} />
  {:else if name === 'download'}
    <path d="M4 16v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3" stroke={mono ? 'currentColor' : '#10b981'} stroke-width="2.4" stroke-linecap="round" />
    <path d="M10 3h4v6.5h3.7L12 15.8 6.3 9.5H10V3Z" fill={mono ? 'currentColor' : '#34d399'} />
  {:else if name === 'thermometer'}
    <path d="M9 4.5a3 3 0 0 1 6 0v7.6a5.5 5.5 0 1 1-6 0V4.5Z" fill={mono ? 'currentColor' : '#fca5a5'} />
    <circle cx="12" cy="16.5" r="3.6" fill={mono ? 'currentColor' : '#ef4444'} />
    <rect x="10.8" y="6.5" width="2.4" height="9.5" rx="1.2" fill={mono ? 'currentColor' : '#ef4444'} />
  {:else if name === 'clock'}
    <circle cx="12" cy="12" r="9.5" fill={mono ? 'currentColor' : '#0ea5e9'} />
    <circle cx="12" cy="12" r="7" fill={mono ? 'rgba(255,255,255,.2)' : '#e0f2fe'} />
    <path d="M12 7.5V12l3.2 2" stroke={mono ? '#fff' : '#0369a1'} stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" />
  {:else if name === 'moon'}
    <path d="M20.6 13.8A8.8 8.8 0 1 1 10.2 3.4a7 7 0 0 0 10.4 10.4Z" fill={mono ? 'currentColor' : '#818cf8'} />
    <circle cx="17.5" cy="5.5" r="1.4" fill={mono ? 'currentColor' : '#fbbf24'} />
  {:else if name === 'sun'}
    <circle cx="12" cy="12" r="5" fill={mono ? 'currentColor' : '#f59e0b'} />
    <path d="M12 2v2.4M12 19.6V22M4.9 4.9l1.7 1.7M17.4 17.4l1.7 1.7M2 12h2.4M19.6 12H22M4.9 19.1l1.7-1.7M17.4 6.6l1.7-1.7" stroke={mono ? 'currentColor' : '#fbbf24'} stroke-width="2.3" stroke-linecap="round" />
  {:else if name === 'refresh'}
    <path d="M20.5 11.5A8.5 8.5 0 0 0 5.3 6.8L3.5 8.5M3.5 4v4.5H8" stroke={mono ? 'currentColor' : '#22d3ee'} stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round" />
    <path d="M3.5 12.5a8.5 8.5 0 0 0 15.2 4.7l1.8-1.7M20.5 20v-4.5H16" stroke={mono ? 'currentColor' : '#10b981'} stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round" />
  {:else if name === 'globe'}
    <circle cx="12" cy="12" r="9.5" fill={mono ? 'currentColor' : '#0284c7'} />
    <ellipse cx="12" cy="12" rx="4.2" ry="9.3" stroke={mono ? '#fff' : '#7dd3fc'} stroke-width="1.6" />
    <line x1="2.8" y1="12" x2="21.2" y2="12" stroke={mono ? '#fff' : '#7dd3fc'} stroke-width="1.6" />
  {:else if name === 'lock'}
    <path d="M7.5 10V7a4.5 4.5 0 0 1 9 0v3" stroke={mono ? 'currentColor' : '#94a3b8'} stroke-width="2.4" stroke-linecap="round" />
    <rect x="4" y="10" width="16" height="11" rx="2.8" fill={mono ? 'currentColor' : '#f59e0b'} />
    <circle cx="12" cy="15.2" r="1.5" fill={mono ? 'rgba(0,0,0,.35)' : '#78350f'} />
  {:else if name === 'eye'}
    <path d="M1.5 12S5.5 5 12 5s10.5 7 10.5 7-4 7-10.5 7S1.5 12 1.5 12Z" fill={mono ? 'currentColor' : '#38bdf8'} />
    <circle cx="12" cy="12" r="3.4" fill={mono ? 'rgba(0,0,0,.35)' : '#1e1b4b'} />
    <circle cx="13" cy="11" r="1" fill="#fff" />
  {:else if name === 'eye-off'}
    <path d="M2 12s4-6.5 10-6.5S22 12 22 12s-4 6.5-10 6.5S2 12 2 12Z" fill={mono ? 'currentColor' : '#64748b'} />
    <circle cx="12" cy="12" r="3" fill={mono ? 'rgba(0,0,0,.3)' : '#1e293b'} />
    <line x1="3" y1="3" x2="21" y2="21" stroke={mono ? 'currentColor' : '#f43f5e'} stroke-width="2.4" stroke-linecap="round" />
  {:else if name === 'trash'}
    <path d="M9 3h6l1 2h4.5v2H3.5V5H8l1-2Z" fill={mono ? 'currentColor' : '#f87171'} />
    <path d="M5.5 8h13l-1 11.5a2 2 0 0 1-2 1.8h-7a2 2 0 0 1-2-1.8L5.5 8Z" fill={mono ? 'currentColor' : '#ef4444'} />
  {:else if name === 'edit'}
    <path d="M4 16.5V20h3.5L18.4 9.1l-3.5-3.5L4 16.5Z" fill={mono ? 'currentColor' : '#3b82f6'} />
    <path d="M19.8 7.7a1.5 1.5 0 0 0 0-2.1l-1.4-1.4a1.5 1.5 0 0 0-2.1 0l-1.3 1.3 3.5 3.5 1.3-1.3Z" fill={mono ? 'currentColor' : '#fbbf24'} />
  {:else if name === 'play'}
    <path d="M6.5 4.2a1.2 1.2 0 0 1 1.8-1l11.8 7.3a1.2 1.2 0 0 1 0 2L8.3 19.8a1.2 1.2 0 0 1-1.8-1V4.2Z" fill={mono ? 'currentColor' : '#10b981'} />
  {:else if name === 'stop'}
    <rect x="4.5" y="4.5" width="15" height="15" rx="3" fill={mono ? 'currentColor' : '#ef4444'} />
  {:else if name === 'plus'}
    <path d="M12 4.5v15M4.5 12h15" stroke={mono ? 'currentColor' : '#10b981'} stroke-width="2.8" stroke-linecap="round" />
  {:else if name === 'arrow-up'}
    <path d="M12 3.5 5.5 10h4.3v9.5h4.4V10h4.3L12 3.5Z" fill={mono ? 'currentColor' : '#38bdf8'} />
  {:else if name === 'search'}
    <circle cx="10.5" cy="10.5" r="6.5" fill={mono ? 'currentColor' : '#38bdf8'} fill-opacity={mono ? '1' : '0.25'} stroke={mono ? 'currentColor' : '#38bdf8'} stroke-width="2.3" />
    <line x1="15.5" y1="15.5" x2="20.5" y2="20.5" stroke={mono ? 'currentColor' : '#6366f1'} stroke-width="2.6" stroke-linecap="round" />
  {:else if name === 'eraser'}
    <path d="m7.5 20.5-4.3-4.3a2 2 0 0 1 0-2.8l8.8-8.8a2 2 0 0 1 2.8 0l5 5a2 2 0 0 1 0 2.8l-8 8.1H7.5Z" fill={mono ? 'currentColor' : '#f43f5e'} />
    <path d="m6.5 10.5 6.8 6.8" stroke="#fff" stroke-opacity=".45" stroke-width="1.6" />
    <line x1="12" y1="20.5" x2="21.5" y2="20.5" stroke={mono ? 'currentColor' : '#fda4af'} stroke-width="2.2" stroke-linecap="round" />
  {:else if name === 'cpu'}
    <path d="M9 1.5v3M15 1.5v3M9 19.5v3M15 19.5v3M19.5 9h3M19.5 15h3M1.5 9h3M1.5 15h3" stroke={mono ? 'currentColor' : '#22d3ee'} stroke-width="2.1" stroke-linecap="round" />
    <rect x="5" y="5" width="14" height="14" rx="3" fill={mono ? 'currentColor' : '#6366f1'} />
    <rect x="8.5" y="8.5" width="7" height="7" rx="1.4" fill={mono ? 'rgba(255,255,255,.35)' : '#38bdf8'} />
  {:else if name === 'box'}
    <path d="M12 2.2 3.2 7l8.8 4.8L20.8 7 12 2.2Z" fill={mono ? 'currentColor' : '#fdba74'} />
    <path d="M3.2 7v9.8l8.8 4.8v-9.8L3.2 7Z" fill={mono ? 'currentColor' : '#f97316'} />
    <path d="M20.8 7v9.8L12 21.6v-9.8L20.8 7Z" fill={mono ? 'currentColor' : '#ea580c'} />
  {:else if name === 'plug'}
    <path d="M9 2.5v4.5M15 2.5v4.5" stroke={mono ? 'currentColor' : '#fbbf24'} stroke-width="2.4" stroke-linecap="round" />
    <path d="M6 7h12v5a6 6 0 0 1-12 0V7Z" fill={mono ? 'currentColor' : '#06b6d4'} />
    <path d="M12 18v3.5" stroke={mono ? 'currentColor' : '#22d3ee'} stroke-width="2.6" stroke-linecap="round" />
  {:else if name === 'image'}
    <rect x="2.5" y="3.5" width="19" height="17" rx="3" fill={mono ? 'currentColor' : '#ec4899'} />
    <circle cx="8" cy="8.8" r="1.8" fill={mono ? '#fff' : '#fde047'} />
    <path d="M4.5 18.5 10.5 12l3.8 3.8 2.4-2.4 2.8 5.1h-15Z" fill={mono ? 'rgba(255,255,255,.4)' : '#fbcfe8'} />
  {:else if name === 'alert'}
    <path d="M10.4 3.6a1.8 1.8 0 0 1 3.2 0l8 14.2a1.8 1.8 0 0 1-1.6 2.7H4a1.8 1.8 0 0 1-1.6-2.7l8-14.2Z" fill={mono ? 'currentColor' : '#f59e0b'} />
    <line x1="12" y1="9" x2="12" y2="13.5" stroke={mono ? 'rgba(0,0,0,.55)' : '#1e293b'} stroke-width="2.2" stroke-linecap="round" />
    <circle cx="12" cy="16.8" r="1.2" fill={mono ? 'rgba(0,0,0,.55)' : '#1e293b'} />
  {:else if name === 'check'}
    <circle cx="12" cy="12" r="9.5" fill={mono ? 'currentColor' : '#10b981'} />
    <path d="m8 12.2 2.8 2.8 5.4-5.6" stroke="#fff" stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round" />
  {:else if name === 'folder'}
    <path d="M2.5 6a2.2 2.2 0 0 1 2.2-2.2h4.6l2 2.4h8a2.2 2.2 0 0 1 2.2 2.2V18a2.2 2.2 0 0 1-2.2 2.2H4.7A2.2 2.2 0 0 1 2.5 18V6Z" fill={mono ? 'currentColor' : '#f59e0b'} />
    <rect x="2.5" y="8" width="19" height="12.2" rx="2.2" fill={mono ? 'currentColor' : '#fbbf24'} />
  {:else if name === 'settings'}
    <path fill-rule="evenodd" clip-rule="evenodd" d="M10.3 2.5h3.4l.5 2.2a7.5 7.5 0 0 1 1.7.7l1.9-1.2 2.4 2.4-1.2 1.9c.3.5.5 1.1.7 1.7l2.2.5v3.4l-2.2.5a7.5 7.5 0 0 1-.7 1.7l1.2 1.9-2.4 2.4-1.9-1.2a7.5 7.5 0 0 1-1.7.7l-.5 2.2h-3.4l-.5-2.2a7.5 7.5 0 0 1-1.7-.7l-1.9 1.2-2.4-2.4 1.2-1.9a7.5 7.5 0 0 1-.7-1.7L2 13.7v-3.4l2.2-.5c.2-.6.4-1.2.7-1.7L3.7 6.2l2.4-2.4 1.9 1.2c.5-.3 1.1-.5 1.7-.7l.6-1.8ZM12 8.8a3.2 3.2 0 1 0 0 6.4 3.2 3.2 0 0 0 0-6.4Z" fill={mono ? 'currentColor' : '#818cf8'} />
    <circle cx="12" cy="12" r="2.2" fill={mono ? 'none' : '#22d3ee'} />
  {:else if name === 'layers'}
    <path d="M12 14.5 2.5 9.5 12 4.5l9.5 5-9.5 5Z" fill={mono ? 'currentColor' : '#38bdf8'} />
    <path d="m2.5 13.2 9.5 4.8 9.5-4.8" stroke={mono ? 'currentColor' : '#818cf8'} stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" />
    <path d="m2.5 16.8 9.5 4.7 9.5-4.7" stroke={mono ? 'currentColor' : '#6366f1'} stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" />
  {:else if name === 'grid'}
    <rect x="3" y="3" width="8" height="8" rx="2" fill={mono ? 'currentColor' : '#6366f1'} />
    <rect x="13" y="3" width="8" height="8" rx="2" fill={mono ? 'currentColor' : '#38bdf8'} />
    <rect x="3" y="13" width="8" height="8" rx="2" fill={mono ? 'currentColor' : '#10b981'} />
    <rect x="13" y="13" width="8" height="8" rx="2" fill={mono ? 'currentColor' : '#f59e0b'} />
  {:else if name === 'wrench'}
    <path d="M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76Z" fill={mono ? 'currentColor' : '#38bdf8'} />
  {:else if name === 'logout'}
    <path d="M10 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h5" stroke={mono ? 'currentColor' : '#94a3b8'} stroke-width="2.3" stroke-linecap="round" />
    <path d="M15.5 7.5 21 12l-5.5 4.5v-3H9v-3h6.5v-3Z" fill={mono ? 'currentColor' : '#f43f5e'} />
  {:else if name === 'chevron-left'}
    <path d="m14.5 6.5-5.5 5.5 5.5 5.5" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" />
  {:else if name === 'chevron-right'}
    <path d="m9.5 6.5 5.5 5.5-5.5 5.5" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" />
  {:else if name === 'info'}
    <circle cx="12" cy="12" r="9.5" fill={mono ? 'currentColor' : '#3b82f6'} />
    <line x1="12" y1="11.2" x2="12" y2="16.5" stroke="#fff" stroke-width="2.3" stroke-linecap="round" />
    <circle cx="12" cy="7.8" r="1.2" fill="#fff" />
  {:else if name === 'wave'}
    <rect x="2.5" y="3.5" width="19" height="17" rx="3" fill={mono ? 'currentColor' : '#1e293b'} />
    <rect x="4" y="5" width="16" height="14" rx="2" fill={mono ? 'rgba(255,255,255,.15)' : '#0f172a'} />
    <path d="M4.5 12h3l2-5 3 10 2.5-6.5 1.8 1.5H19.5" stroke={mono ? '#fff' : '#22d3ee'} stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" />
  {:else if name === 'cloud-sun'}
    <circle cx="16.2" cy="8.2" r="3.6" fill={mono ? 'currentColor' : '#fbbf24'} />
    <path d="M16.2 2.6v1.4M21.8 8.2h-1.4M20.2 4.2l-1 1M20.2 12.2l-1-1" stroke={mono ? 'currentColor' : '#f59e0b'} stroke-width="1.9" stroke-linecap="round" />
    <path d="M7.5 19.5h9a4 4 0 0 0 .6-7.95A5.5 5.5 0 0 0 6.4 10.7a4.2 4.2 0 0 0 1.1 8.8Z" fill={mono ? 'currentColor' : '#38bdf8'} />
  {:else if name === 'toggle'}
    <rect x="2.5" y="4" width="19" height="7" rx="3.5" fill={mono ? 'currentColor' : '#10b981'} />
    <circle cx="17.5" cy="7.5" r="2.5" fill={mono ? 'rgba(255,255,255,.8)' : '#ecfdf5'} />
    <rect x="2.5" y="13" width="19" height="7" rx="3.5" fill={mono ? 'currentColor' : '#64748b'} />
    <circle cx="6.5" cy="16.5" r="2.5" fill={mono ? 'rgba(255,255,255,.8)' : '#f1f5f9'} />
  {:else if name === 'monitor'}
    <rect x="2" y="4" width="20" height="13.5" rx="2.6" fill={mono ? 'currentColor' : '#0ea5e9'} />
    <rect x="4" y="6" width="16" height="9.5" rx="1.4" fill={mono ? 'rgba(0,0,0,.25)' : '#0f766e'} />
    <line x1="6" y1="9" x2="15" y2="9" stroke={mono ? '#fff' : '#6ee7b7'} stroke-width="2" stroke-linecap="round" />
    <line x1="6" y1="12.5" x2="12" y2="12.5" stroke={mono ? '#fff' : '#a7f3d0'} stroke-width="2" stroke-linecap="round" />
    <path d="M9 17.5h6l1 2.8H8l1-2.8Z" fill={mono ? 'currentColor' : '#38bdf8'} />
  {:else if name === 'bulb'}
    <path d="M12 2.5a6.5 6.5 0 0 0-4.2 11.4c.8.7 1.3 1.6 1.5 2.6h5.4c.2-1 .7-1.9 1.5-2.6A6.5 6.5 0 0 0 12 2.5Z" fill={mono ? 'currentColor' : '#fbbf24'} />
    <circle cx="12" cy="8.5" r="2.4" fill={mono ? 'rgba(255,255,255,.45)' : '#fef3c7'} />
    <rect x="9.2" y="17.2" width="5.6" height="2" rx="1" fill={mono ? 'currentColor' : '#94a3b8'} />
    <rect x="10" y="20" width="4" height="1.8" rx="0.9" fill={mono ? 'currentColor' : '#64748b'} />
  {:else if name === 'display'}
    <rect x="2.5" y="4.5" width="19" height="15" rx="3" fill={mono ? 'currentColor' : '#6366f1'} />
    <rect x="4.5" y="6.5" width="15" height="11" rx="1.8" fill={mono ? 'rgba(0,0,0,.3)' : '#1e1b4b'} />
    <rect x="6.5" y="8.5" width="6" height="3" rx="0.8" fill={mono ? '#fff' : '#38bdf8'} />
    <circle cx="16" cy="14" r="1.8" fill={mono ? '#fff' : '#22d3ee'} />
    <line x1="6.5" y1="14.5" x2="12" y2="14.5" stroke={mono ? '#fff' : '#a5b4fc'} stroke-width="1.8" stroke-linecap="round" />
  {:else if name === 'zap'}
    <path d="M13.2 2.2 4.8 13h6.4l-1.4 8.8L19.2 10.8h-6.4l.4-8.6Z" fill={mono ? 'currentColor' : '#f59e0b'} />
    <path d="M12.7 5.2 7.5 12h4.6l-.8 5.4 5.2-6.4h-4.6l.8-5.8Z" fill={mono ? 'rgba(255,255,255,.3)' : '#fde047'} />
  {:else if name === 'sliders'}
    <line x1="3" y1="6.5" x2="21" y2="6.5" stroke={mono ? 'currentColor' : '#94a3b8'} stroke-width="2.2" stroke-linecap="round" />
    <circle cx="9" cy="6.5" r="2.8" fill={mono ? 'currentColor' : '#38bdf8'} />
    <line x1="3" y1="12" x2="21" y2="12" stroke={mono ? 'currentColor' : '#94a3b8'} stroke-width="2.2" stroke-linecap="round" />
    <circle cx="15.5" cy="12" r="2.8" fill={mono ? 'currentColor' : '#a855f7'} />
    <line x1="3" y1="17.5" x2="21" y2="17.5" stroke={mono ? 'currentColor' : '#94a3b8'} stroke-width="2.2" stroke-linecap="round" />
    <circle cx="11" cy="17.5" r="2.8" fill={mono ? 'currentColor' : '#10b981'} />
  {:else if name === 'servo'}
    <rect x="3" y="10" width="12" height="10.5" rx="2.5" fill={mono ? 'currentColor' : '#6366f1'} />
    <circle cx="9" cy="15.2" r="2.4" fill={mono ? 'rgba(255,255,255,.35)' : '#c7d2fe'} />
    <path d="M9 9V5.5h8.5a2 2 0 0 1 0 4H9Z" fill={mono ? 'currentColor' : '#f97316'} />
    <circle cx="17.5" cy="7.5" r="1.4" fill={mono ? '#fff' : '#ffedd5'} />
  {:else if name === 'droplet'}
    <path d="M12 2.4S5.2 10 5.2 14.8a6.8 6.8 0 0 0 13.6 0C18.8 10 12 2.4 12 2.4Z" fill={mono ? 'currentColor' : '#0ea5e9'} />
    <path d="M9.2 15.2a3.2 3.2 0 0 0 3.2 3.2" stroke={mono ? '#fff' : '#bae6fd'} stroke-width="2" stroke-linecap="round" />
  {:else if name === 'timer'}
    <rect x="9.5" y="2" width="5" height="2.2" rx="1" fill={mono ? 'currentColor' : '#f59e0b'} />
    <path d="m17.5 5.2 1.8 1.8" stroke={mono ? 'currentColor' : '#fbbf24'} stroke-width="2.2" stroke-linecap="round" />
    <circle cx="12" cy="13.2" r="8" fill={mono ? 'currentColor' : '#10b981'} />
    <circle cx="12" cy="13.2" r="5.8" fill={mono ? 'rgba(255,255,255,.2)' : '#ecfdf5'} />
    <path d="M12 9.5v4l2.8 1.8" stroke={mono ? '#fff' : '#047857'} stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round" />
  {:else if name === 'gauge'}
    <path d="M3.5 16.5a8.5 8.5 0 1 1 17 0" stroke={mono ? 'currentColor' : '#38bdf8'} stroke-width="3" stroke-linecap="round" />
    <path d="M14.5 7.2a8.5 8.5 0 0 1 6 9.3" stroke={mono ? 'currentColor' : '#10b981'} stroke-width="3" stroke-linecap="round" />
    <path d="m12 15.5 4.2-4.8" stroke={mono ? 'currentColor' : '#f43f5e'} stroke-width="2.4" stroke-linecap="round" />
    <circle cx="12" cy="16" r="2.3" fill={mono ? 'currentColor' : '#6366f1'} />
  {:else if name === 'compass'}
    <circle cx="12" cy="12" r="9.5" fill={mono ? 'currentColor' : '#0284c7'} />
    <circle cx="12" cy="12" r="6.8" fill={mono ? 'rgba(255,255,255,.18)' : '#e0f2fe'} />
    <path d="m15.6 8.4-2.1 5.1-5.1 2.1 2.1-5.1 5.1-2.1Z" fill={mono ? '#fff' : '#ef4444'} />
    <circle cx="12" cy="12" r="1.2" fill={mono ? 'rgba(0,0,0,.4)' : '#fff'} />
  {:else if name === 'weather'}
    <circle cx="15.8" cy="7.5" r="3.2" fill={mono ? 'currentColor' : '#fbbf24'} />
    <path d="M7 16.5h9.5a3.5 3.5 0 0 0 .5-7A5 5 0 0 0 7.4 8.8 3.8 3.8 0 0 0 7 16.5Z" fill={mono ? 'currentColor' : '#38bdf8'} />
    <path d="M9 19l-1 2.5M13 19l-1 2.5M17 19l-1 2.5" stroke={mono ? 'currentColor' : '#0ea5e9'} stroke-width="2" stroke-linecap="round" />
  {/if}
</svg>
