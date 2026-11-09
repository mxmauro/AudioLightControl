import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import tailwindcss from '@tailwindcss/vite';
import wails from '@wailsio/runtime/plugins/vite';

// -----------------------------------------------------------------------------

// https://vite.dev/config/
export default defineConfig({
	plugins: [
		tailwindcss(),
		svelte(),
		wails('./src/bindings')
	],
	server: {
		host: '127.0.0.1',
		port: Number(process.env.WAILS_VITE_PORT) || 5173,
		strictPort: true
	},
	build: {
		outDir: './dist'
	}
});
