<script lang="ts">
import CheckIcon from '@lucide/svelte/icons/check';
import CopyIcon from '@lucide/svelte/icons/copy';
import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw';
import { Client } from '@mxmauro/iot-comm.js';
import { onMount } from 'svelte';
import { ClipboardSetText } from '../generated/wailsjs/runtime/runtime';
import { errorMessage } from '../lib/ui';

let { oncancel, onuse }: { oncancel: () => void; onuse: (publicKey: string) => void } = $props();
let publicKey = $state('');
let privateKey = $state('');
let error = $state('');
let publicKeyCopied = $state(false);
let privateKeyCopied = $state(false);
let generating = $state(true);

// -----------------------------------------------------------------------------

const toBase64 = (key: ArrayBuffer): string => {
	return btoa(String.fromCharCode(...new Uint8Array(key)));
};

const generate = async (): Promise<void> => {
	// Keep the generated key pair in dialog memory only until it is used or dismissed.
	generating = true;
	error = '';
	publicKeyCopied = false;
	privateKeyCopied = false;
	publicKey = '';
	privateKey = '';
	try {
		const pair = await Client.generateECDSAKeyPair();
		publicKey = toBase64(pair.publicKey);
		privateKey = toBase64(pair.privateKey);
		new Uint8Array(pair.publicKey).fill(0);
		new Uint8Array(pair.privateKey).fill(0);
	} catch (cause) {
		error = errorMessage(cause);
	} finally {
		generating = false;
	}
};

const copyKey = async (key: string, label: string): Promise<boolean> => {
	if (!key) {
		return false;
	}
	try {
		await ClipboardSetText(key);
		return true;
	} catch (cause) {
		error = `Could not copy the ${label}: ${errorMessage(cause)}`;
		return false;
	}
};

const copyPublicKey = async (): Promise<void> => {
	publicKeyCopied = await copyKey(publicKey, 'public key');
};

const copyPrivateKey = async (): Promise<void> => {
	privateKeyCopied = await copyKey(privateKey, 'private key');
};

const dismiss = (): void => {
	publicKey = '';
	privateKey = '';
	oncancel();
};

const useKeyPair = (): void => {
	if (publicKey) {
		const key = publicKey;
		publicKey = '';
		privateKey = '';
		onuse(key);
	}
};

onMount(() => {
	void generate();
});
</script>

<div class="fixed inset-0 z-50 grid place-items-center bg-black/50 p-4" role="presentation">
	<div
		class="card bg-surface-100-900 mx-auto w-full max-w-3xl space-y-4 p-4"
		role="dialog"
		aria-modal="true"
		aria-labelledby="key-pair-generator-title"
		tabindex="-1"
	>
		<header>
			<h2 id="key-pair-generator-title" class="text-base font-semibold text-primary-600-400">Generate key pair</h2>
		</header>
		{#if generating}
			<p class="flex items-center gap-2 text-sm" role="status"><RefreshCwIcon class="spin-360" />Generating key pair…</p>
		{:else if error}
			<p class="text-error-600-400" role="alert">{error}</p>
		{:else}
			<div class="space-y-2">
				<label class="label">
					<span class="label-text">Public key</span>
					<div class="relative">
						<input class="input !pr-24" type="text" value={publicKey} readonly autocomplete="off">
						<div class="absolute inset-y-0 right-0 flex items-center gap-1 pr-1">
							<button class="btn-icon" type="button" aria-label="Generate a new key pair" onclick={() => void generate()}>
								<RefreshCwIcon />
							</button>
							<button class="btn-icon" type="button" aria-label="Copy public key" onclick={() => void copyPublicKey()}>
								{#if publicKeyCopied}
									<CheckIcon />
								{:else}
									<CopyIcon />
								{/if}
							</button>
						</div>
					</div>
				</label>
				<label class="label">
					<span class="label-text">Private key</span>
					<div class="relative">
						<input class="input !pr-12" type="password" value={privateKey} readonly autocomplete="off">
						<button
							class="btn-icon absolute inset-y-0 right-0 my-auto mr-1"
							type="button"
							aria-label="Copy private key"
							onclick={() => void copyPrivateKey()}
						>
							{#if privateKeyCopied}
								<CheckIcon />
							{:else}
								<CopyIcon />
							{/if}
						</button>
					</div>
				</label>
			</div>
		{/if}
		<footer class="form-actions">
			<small class="mr-auto self-end">Copy and store the private key securely. It cannot be recovered after this dialog is closed.</small>
			<button class="btn preset-filled-surface-500" type="button" disabled={generating} onclick={dismiss}>Cancel</button>
			<button class="btn preset-filled" type="button" disabled={generating || !publicKey} onclick={useKeyPair}>Use this</button>
		</footer>
	</div>
</div>
