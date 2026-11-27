import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';

// للتطوير على الجهاز: ESP_IP=192.168.4.1 npm run dev
const target = `http://${process.env.ESP_IP || '192.168.4.1'}`;

export default defineConfig({
  plugins: [svelte(), viteSingleFile()],
  build: { outDir: '../data/app', emptyOutDir: true },
  server: {
    host: '0.0.0.0',
    port: 3000,
    allowedHosts: true,
    proxy: { '/login': target, '/settings': target, '/hdusage': target }
  }
});
