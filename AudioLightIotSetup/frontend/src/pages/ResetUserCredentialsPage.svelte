<script lang="ts">
import EyeIcon from '@lucide/svelte/icons/eye';
import EyeOffIcon from '@lucide/svelte/icons/eye-off';
import RectangleEllipsisIcon from '@lucide/svelte/icons/rectangle-ellipsis';
import type { Client } from '@mxmauro/iot-comm.js';
import ConfirmationDialog from '../components/ConfirmationDialog.svelte';
import KeyPairGeneratorDialog from '../components/KeyPairGeneratorDialog.svelte';
import { decodeBase64Key } from '../lib/session';
import { errorMessage, type Notice, preventSubmit } from '../lib/ui';

let { client, onnotice }: { client: Client; onnotice: (notice: Notice) => void } = $props();
let name = $state('');
let publicKey = $state('');
let confirmation = $state('');
let showPublicKey = $state(false);
let showConfirmation = $state(false);
let pendingKey = $state<ArrayBuffer | null>(null);
let confirming = $state(false);
let busy = $state(false);
let errors = $state<Record<string, string>>({});
let generatingKeyPair = $state(false);

// -----------------------------------------------------------------------------

const requestReset = (): void => {
	// Validate and retain the replacement public key only while the confirmation dialog is open.
	errors = {};
	if (!name.trim()) {
		errors.name = 'Enter the user name to reset.';
	}
	if (publicKey !== confirmation) {
		errors.confirmation = 'Public-key confirmation does not match.';
	}
	try {
		pendingKey = decodeBase64Key(publicKey, 65, 'Public key');
	} catch (error) {
		errors.publicKey = errorMessage(error);
	}
	if (Object.keys(errors).length > 0 || !pendingKey) {
		return;
	}
	confirming = true;
};

const confirmReset = async (): Promise<void> => {
	// Submit the confirmed reset and clear the pending key after either result.
	if (!pendingKey) {
		return;
	}
	busy = true;
	try {
		await client.resetUserCredentialsCommand(name.trim(), pendingKey);
		name = '';
		publicKey = '';
		confirmation = '';
		confirming = false;
		onnotice({ tone: 'success', message: 'User credentials reset.' });
	} catch (error) {
		confirming = false;
		onnotice({ tone: 'error', message: errorMessage(error) });
	} finally {
		pendingKey = null;
		busy = false;
	}
};

const cancelReset = (): void => {
	// Discard the temporary replacement key when the user abandons the reset.
	confirming = false;
	pendingKey = null;
};

const useGeneratedKeyPair = (key: string): void => {
	publicKey = key;
	confirmation = key;
	delete errors.publicKey;
	delete errors.confirmation;
	generatingKeyPair = false;
};
</script>

<form class="card bg-surface-100-900 p-4 w-full max-w-3xl mx-auto space-y-4" onsubmit={preventSubmit}>
	<header>
		<h1 class="text-base font-semibold text-primary-600-400">Reset user credentials</h1>
	</header>
	<hr class="border-primary-600-400">
	<div class="space-y-2">
		<label class="label"
			><span class="label-text">User name</span>
			<input class="input" bind:value={name} disabled={busy} aria-invalid={!!errors.name} aria-describedby="reset-user-name-error">
			{#if errors.name}
				<small id="reset-user-name-error" class="text-error-600-400">{errors.name}</small>
			{/if}</label
		>
		<label class="label"
			><span class="label-text">New public key</span>
			<div class="relative">
				<input
					class="input !pr-24"
					type={showPublicKey ? 'text' : 'password'}
					bind:value={publicKey}
					autocomplete="off"
					disabled={busy}
					aria-invalid={!!errors.publicKey}
					aria-describedby="reset-user-public-key-error"
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
						aria-label={showPublicKey ? 'Hide public key' : 'Show public key'}
						disabled={busy}
						onclick={() => (showPublicKey = !showPublicKey)}
					>
						{#if showPublicKey}
							<EyeOffIcon />
						{:else}
							<EyeIcon />
						{/if}
					</button>
				</div>
			</div>
			{#if errors.publicKey}
				<small id="reset-user-public-key-error" class="text-error-600-400">{errors.publicKey}</small>
			{/if}</label
		>
		<label class="label"
			><span class="label-text">Confirm public key</span>
			<div class="relative">
				<input
					class="input !pr-12"
					type={showConfirmation ? 'text' : 'password'}
					bind:value={confirmation}
					autocomplete="off"
					disabled={busy}
					aria-invalid={!!errors.confirmation}
					aria-describedby="reset-user-confirmation-error"
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
				<small id="reset-user-confirmation-error" class="text-error-600-400">{errors.confirmation}</small>
			{/if}</label
		>
	</div>
	<footer class="form-actions">
		<button class="btn preset-filled-warning-500" type="button" disabled={busy} onclick={requestReset}>Reset credentials</button>
	</footer>
</form>
{#if confirming}
	<ConfirmationDialog
		title="Reset user credentials"
		summary={`Replace credentials for “${name.trim()}”.`}
		{busy}
		oncancel={cancelReset}
		onconfirm={() => void confirmReset()}
	/>
{/if}
{#if generatingKeyPair}
	<KeyPairGeneratorDialog oncancel={() => (generatingKeyPair = false)} onuse={useGeneratedKeyPair} />
{/if}
