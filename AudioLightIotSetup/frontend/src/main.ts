import { mount } from 'svelte';
import './app.css';
import App from './App.svelte';

// -----------------------------------------------------------------------------

declare global {
	interface Window {
		isWails: () => boolean;
	}
}

// -----------------------------------------------------------------------------

const app = mount(App, {
	// biome-ignore lint/style/noNonNullAssertion: Application MUST exist!
	target: document.getElementById('app')!
});

// -----------------------------------------------------------------------------

const isWails = (): boolean => {
	// For Wails v2: It creates a 'go' property for bound Go methods
	// For Wails v3: It uses the modern 'wails' runtime object
	if (typeof window === 'undefined') {
		return false;
	}
	const wailsWindow = window as Window & { go?: unknown; wails?: unknown };
	return !!wailsWindow.go || !!wailsWindow.wails;
};

window.isWails = isWails;

export default app;
