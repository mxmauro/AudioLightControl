<script lang="ts">
import FingerprintIcon from '@lucide/svelte/icons/fingerprint';
import { decodeBase64Key, getLastHost, isValidHost, normalizeHost } from '../lib/session';
import { errorMessage } from '../lib/ui';

let {
	busy = false,
	onconnect,
	ontrusteddevices
}: {
	busy?: boolean;
	onconnect: (host: string, username: string, key: ArrayBuffer) => Promise<void>;
	ontrusteddevices: () => void;
} = $props();
let host = $state(getLastHost());
let username = $state('');
let privateKey = $state('');
let errors = $state<Record<string, string>>({});

// -----------------------------------------------------------------------------

const submit = async (): Promise<void> => {
	// Validate connection details and decode the private key before attempting a session.
	errors = {};
	if (!isValidHost(host)) {
		errors.host = 'Enter a hostname or IP address with an optional port.';
	}
	if (username.trim().length < 1 || username.trim().length > 255) {
		errors.username = 'User name must be 1 to 255 characters.';
	}
	let key: ArrayBuffer | null = null;
	// Normalize the host only after validation, then clear decoded key bytes once the attempt completes.
	try {
		key = decodeBase64Key(privateKey, 32, 'Private key');
	} catch (error) {
		errors.privateKey = errorMessage(error);
	}
	if (Object.keys(errors).length > 0 || !key) {
		return;
	}
	try {
		await onconnect(normalizeHost(host), username.trim(), key);
		privateKey = '';
	} finally {
		new Uint8Array(key).fill(0);
	}
};

const handleSubmit = (event: SubmitEvent): void => {
	// Keep browser form submission local while reusing the asynchronous connection workflow.
	event.preventDefault();
	void submit();
};

const openTrustedDevices = (): void => {
	ontrusteddevices();
};
</script>

<main class="flex h-full min-h-0 items-center overflow-y-auto p-4">
	<section class="card bg-surface-100-900 mx-auto w-full max-w-3xl space-y-4 p-4">
		<header>
			<h1 class="text-base font-semibold text-primary-600-400">Connect to a lighting device</h1>
		</header>
		<hr class="border-primary-600-400">
		<form class="space-y-6" onsubmit={handleSubmit} novalidate>
			<div class="space-y-2">
				<label class="label"
					><span class="label-text">Device host</span>
					<input class="input" bind:value={host} placeholder="audiolight.local:80" disabled={busy} aria-invalid={!!errors.host}>
					{#if errors.host}
						<small class="text-error-600-400">{errors.host}</small>
					{/if}</label
				>
				<label class="label"
					><span class="label-text">User name</span>
					<input class="input" bind:value={username} autocomplete="username" disabled={busy} aria-invalid={!!errors.username}>
					{#if errors.username}
						<small class="text-error-600-400">{errors.username}</small>
					{/if}</label
				>
				<label class="label"
					><span class="label-text">Private key</span>
					<input
						class="input"
						type="password"
						bind:value={privateKey}
						placeholder="Base64-encoded 32-byte key"
						autocomplete="off"
						disabled={busy}
						aria-invalid={!!errors.privateKey}
					>
					{#if errors.privateKey}
						<small class="text-error-600-400">{errors.privateKey}</small>
					{/if}</label
				>
			</div>
			<footer class="form-actions">
				<div class="mr-auto">
					<button class="btn preset-filled-surface-500" type="button" onclick={openTrustedDevices}>
						<FingerprintIcon />
						Trusted devices
					</button>
				</div>
				<button class="btn preset-filled" type="submit" disabled={busy}>{busy ? 'Connecting…' : 'Connect'}</button>
			</footer>
		</form>
	</section>
</main>
