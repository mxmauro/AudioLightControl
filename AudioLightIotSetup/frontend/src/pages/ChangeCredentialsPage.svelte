<script lang="ts">
import EyeIcon from '@lucide/svelte/icons/eye';
import EyeOffIcon from '@lucide/svelte/icons/eye-off';
import RectangleEllipsisIcon from '@lucide/svelte/icons/rectangle-ellipsis';
import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw';
import type { Client } from '@mxmauro/iot-comm.js';
import KeyPairGeneratorDialog from '../components/KeyPairGeneratorDialog.svelte';
import { decodeBase64Key } from '../lib/session';
import { errorMessage, type Notice, preventSubmit } from '../lib/ui';

let { client, onnotice }: { client: Client; onnotice: (notice: Notice) => void } = $props();
let oldPrivateKey = $state('');
let newPublicKey = $state('');
let confirmation = $state('');
let showOldPrivateKey = $state(false);
let showNewPublicKey = $state(false);
let showConfirmation = $state(false);
let busy = $state(false);
let errors = $state<Record<string, string>>({});
let generatingKeyPair = $state(false);

// -----------------------------------------------------------------------------

const submit = async (): Promise<void> => {
	// Validate both key inputs before submitting a credential rotation to the connected device.
	errors = {};
	if (newPublicKey !== confirmation) {
		errors.confirmation = 'Public-key confirmation does not match.';
	}
	let oldKey: ArrayBuffer | null = null;
	let newKey: ArrayBuffer | null = null;
	try {
		oldKey = decodeBase64Key(oldPrivateKey, 32, 'Current private key');
	} catch (error) {
		errors.oldPrivateKey = errorMessage(error);
	}
	try {
		newKey = decodeBase64Key(newPublicKey, 65, 'New public key');
	} catch (error) {
		errors.newPublicKey = errorMessage(error);
	}
	if (Object.keys(errors).length > 0 || !oldKey || !newKey) {
		return;
	}
	// Submit the validated replacement and clear sensitive text after a successful update.
	busy = true;
	try {
		await client.changeUserCredentialsCommand(oldKey, newKey);
		oldPrivateKey = '';
		newPublicKey = '';
		confirmation = '';
		onnotice({ tone: 'success', message: 'Your credentials were updated.' });
	} catch (error) {
		onnotice({ tone: 'error', message: errorMessage(error) });
	} finally {
		// Wipe decoded private-key bytes regardless of the command result.
		new Uint8Array(oldKey).fill(0);
		busy = false;
	}
};

const useGeneratedKeyPair = (key: string): void => {
	newPublicKey = key;
	confirmation = key;
	delete errors.newPublicKey;
	delete errors.confirmation;
	generatingKeyPair = false;
};
</script>

<form class="card bg-surface-100-900 p-4 w-full max-w-3xl mx-auto space-y-4" onsubmit={preventSubmit}>
	<header>
		<h1 class="text-base font-semibold text-primary-600-400">Change my credentials</h1>
	</header>
	<hr class="border-primary-600-400">
	<div class="space-y-2">
		<label class="label"
			><span class="label-text">Current private key</span>
			<div class="relative">
				<input
					class="input !pr-12"
					type={showOldPrivateKey ? 'text' : 'password'}
					bind:value={oldPrivateKey}
					autocomplete="off"
					disabled={busy}
					aria-invalid={!!errors.oldPrivateKey}
					aria-describedby="old-private-key-error"
				>
				<button
					class="btn-icon absolute inset-y-0 right-0 my-auto mr-1"
					type="button"
					aria-label={showOldPrivateKey ? 'Hide current private key' : 'Show current private key'}
					disabled={busy}
					onclick={() => (showOldPrivateKey = !showOldPrivateKey)}
				>
					{#if showOldPrivateKey}
						<EyeOffIcon />
					{:else}
						<EyeIcon />
					{/if}
				</button>
			</div>
			{#if errors.oldPrivateKey}
				<small id="old-private-key-error" class="text-error-600-400">{errors.oldPrivateKey}</small>
			{/if}</label
		>
		<label class="label"
			><span class="label-text">New public key</span>
			<div class="relative">
				<input
					class="input !pr-24"
					type={showNewPublicKey ? 'text' : 'password'}
					bind:value={newPublicKey}
					autocomplete="off"
					disabled={busy}
					aria-invalid={!!errors.newPublicKey}
					aria-describedby="new-public-key-error"
				>
				<div class="absolute inset-y-0 right-0 flex items-center gap-1 pr-1">
					<button
						class="btn-icon"
						type="button"
						aria-label="Generate public and private key pair"
						disabled={busy}
						onclick={() => (generatingKeyPair = true)}
					>
						<RectangleEllipsisIcon />
					</button>
					<button
						class="btn-icon"
						type="button"
						aria-label={showNewPublicKey ? 'Hide new public key' : 'Show new public key'}
						disabled={busy}
						onclick={() => (showNewPublicKey = !showNewPublicKey)}
					>
						{#if showNewPublicKey}
							<EyeOffIcon />
						{:else}
							<EyeIcon />
						{/if}
					</button>
				</div>
			</div>
			{#if errors.newPublicKey}
				<small id="new-public-key-error" class="text-error-600-400">{errors.newPublicKey}</small>
			{/if}</label
		>
		<label class="label"
			><span class="label-text">Confirm new public key</span>
			<div class="relative">
				<input
					class="input !pr-12"
					type={showConfirmation ? 'text' : 'password'}
					bind:value={confirmation}
					autocomplete="off"
					disabled={busy}
					aria-invalid={!!errors.confirmation}
					aria-describedby="credential-confirmation-error"
				>
				<button
					class="btn-icon absolute inset-y-0 right-0 my-auto mr-1"
					type="button"
					aria-label={showConfirmation ? 'Hide public-key confirmation' : 'Show public-key confirmation'}
					disabled={busy}
					onclick={() => (showConfirmation = !showConfirmation)}
				>
					{#if showConfirmation}
						<EyeOffIcon />
					{:else}
						<EyeIcon />
					{/if}
				</button>
			</div>
			{#if errors.confirmation}
				<small id="credential-confirmation-error" class="text-error-600-400">{errors.confirmation}</small>
			{/if}</label
		>
	</div>
	<footer class="form-actions">
		{#if busy}
			<span class="mr-auto flex items-center gap-2 text-sm" role="status"><RefreshCwIcon class="spin-360" />Changing credentials…</span>
		{/if}
		<button class="btn preset-filled-primary-500" type="button" disabled={busy} onclick={() => void submit()}>Change credentials</button>
	</footer>
</form>
{#if generatingKeyPair}
	<KeyPairGeneratorDialog oncancel={() => (generatingKeyPair = false)} onuse={useGeneratedKeyPair} />
{/if}
