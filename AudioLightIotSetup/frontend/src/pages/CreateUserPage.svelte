<script lang="ts">
import RectangleEllipsisIcon from '@lucide/svelte/icons/rectangle-ellipsis';
import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw';
import { Client } from '@mxmauro/iot-comm.js';
import KeyPairGeneratorDialog from '../components/KeyPairGeneratorDialog.svelte';
import { decodeBase64Key } from '../lib/session';
import { errorMessage, type Notice, preventSubmit } from '../lib/ui';

let { client, onnotice }: { client: Client; onnotice: (notice: Notice) => void } = $props();
let name = $state('');
let publicKey = $state('');
let confirmation = $state('');
let mustChange = $state(false);
let busy = $state(false);
let errors = $state<Record<string, string>>({});
let generatingKeyPair = $state(false);

// -----------------------------------------------------------------------------

const submit = async (): Promise<void> => {
	// Validate the user details and public-key confirmation before creating an account.
	errors = {};
	if (name.trim().length < 1 || name.trim().length > 255) {
		errors.name = 'User name must be 1 to 255 characters.';
	}
	if (publicKey !== confirmation) {
		errors.confirmation = 'Public-key confirmation does not match.';
	}
	let key: ArrayBuffer | null = null;
	try {
		key = decodeBase64Key(publicKey, 65, 'Public key');
	} catch (error) {
		errors.publicKey = errorMessage(error);
	}
	if (Object.keys(errors).length > 0 || !key) {
		return;
	}
	// Submit the device command and retain input values when the operation fails.
	busy = true;
	try {
		await client.createUserCommand(name.trim(), key, mustChange ? Client.USER_CREATE_FLAG_MUST_CHANGE_CREDENTIALS_ON_NEXT_LOGIN : 0);
		name = '';
		publicKey = '';
		confirmation = '';
		mustChange = false;
		onnotice({ tone: 'success', message: 'User created.' });
	} catch (error) {
		onnotice({ tone: 'error', message: errorMessage(error) });
	} finally {
		busy = false;
	}
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
		<h1 class="text-base font-semibold text-primary-600-400">Create user</h1>
	</header>
	<hr class="border-primary-600-400">
	<div class="space-y-2">
		<label class="label"
			><span class="label-text">User name</span>
			<input class="input" bind:value={name} disabled={busy} aria-invalid={!!errors.name} aria-describedby="create-user-name-error">
			{#if errors.name}
				<small id="create-user-name-error" class="text-error-600-400">{errors.name}</small>
			{/if}</label
		>
		<label class="label"
			><span class="label-text">Public key</span>
			<div class="relative">
				<input
					class="input !pr-12"
					type="password"
					bind:value={publicKey}
					placeholder="Base64-encoded 65-byte key"
					autocomplete="off"
					disabled={busy}
					aria-invalid={!!errors.publicKey}
					aria-describedby="create-user-public-key-error"
				>
				<button
					class="btn-icon absolute inset-y-0 right-0 my-auto mr-1"
					type="button"
					aria-label="Generate public and private key pair"
					disabled={busy}
					onclick={() => (generatingKeyPair = true)}
				>
					<RectangleEllipsisIcon />
				</button>
			</div>
			{#if errors.publicKey}
				<small id="create-user-public-key-error" class="text-error-600-400">{errors.publicKey}</small>
			{/if}</label
		>
		<label class="label"
			><span class="label-text">Confirm public key</span>
			<input
				class="input"
				type="password"
				bind:value={confirmation}
				autocomplete="off"
				disabled={busy}
				aria-invalid={!!errors.confirmation}
				aria-describedby="create-user-confirmation-error"
			>
			{#if errors.confirmation}
				<small id="create-user-confirmation-error" class="text-error-600-400">{errors.confirmation}</small>
			{/if}</label
		>
		<label class="flex items-center gap-2"
			><input class="checkbox" type="checkbox" bind:checked={mustChange} disabled={busy}>Require credential change at next login</label
		>
	</div>
	<footer class="form-actions">
		{#if busy}
			<span class="mr-auto flex items-center gap-2 text-sm" role="status"><RefreshCwIcon class="spin-360" />Creating user…</span>
		{/if}
		<button class="btn preset-filled-primary-500" type="button" disabled={busy} onclick={() => void submit()}>Create user</button>
	</footer>
</form>
{#if generatingKeyPair}
	<KeyPairGeneratorDialog oncancel={() => (generatingKeyPair = false)} onuse={useGeneratedKeyPair} />
{/if}
