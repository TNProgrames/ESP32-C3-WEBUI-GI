<script>
  let { rssi = -100, size = 18 } = $props();

  const level = $derived(
    rssi >= -55 ? 4 :
    rssi >= -67 ? 3 :
    rssi >= -78 ? 2 :
    rssi > -100 ? 1 : 0
  );

  const BARS = [
    { x: 1,  y: 11, h: 4,  color: '#ef4444' }, // أحمر
    { x: 6,  y: 8,  h: 7,  color: '#eab308' }, // أصفر
    { x: 11, y: 5,  h: 10, color: '#10b981' }, // أخضر
    { x: 16, y: 2,  h: 13, color: '#3b82f6' }, // أزرق
  ];
</script>

<svg
  width={size}
  height={Math.round(size * 0.8)}
  viewBox="0 0 20 16"
  aria-label="{rssi} dBm"
  style="flex:none;display:inline-block;vertical-align:middle;direction:ltr"
>
  {#each BARS as b, i}
    <rect
      x={b.x}
      y={b.y}
      width="3.4"
      height={b.h}
      rx="1.1"
      fill={i < level ? b.color : 'rgba(148, 163, 184, 0.28)'}
    />
  {/each}
</svg>
