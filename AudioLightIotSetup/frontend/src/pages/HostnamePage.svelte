<script lang="ts">
import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw';
import type { Client } from '@mxmauro/iot-comm.js';
import { errorMessage, type Notice, preventSubmit } from '../lib/ui';

let { client, onnotice }: { client: Client; onnotice: (notice: Notice) => void } = $props();
let hostname = $state('');
let busy = $state(false);
let errors = $state<Record<string, string>>({});

// -----------------------------------------------------------------------------

const save = async (): Promise<void> => {
	// Validate the mDNS-safe hostname before sending the update command.
	errors = {};
	if (!/^[a-zA-Z0-9-]{1,64}$/.test(hostname)) {
		errors.hostname = 'Hostname must be 1 to 64 letters, numbers, or hyphens.';
		return;
	}
	// Keep the typed hostname on failure so the user can correct it.
	busy = true;
	try {
		await client.setHostnameCommand(hostname);
		hostname = '';
		onnotice({ tone: 'success', message: 'Hostname updated.' });
	} catch (error) {
		onnotice({ tone: 'error', message: errorMessage(error) });
	} finally {
		busy = false;
	}
};
</script>

<form class="card bg-surface-100-900 p-4 w-full max-w-3xl mx-auto space-y-4" onsubmit={preventSubmit}>
	<header>
		<h1 class="text-base font-semibold text-primary-600-400">Device mDNS hostname</h1>
	</header>
	<hr class="border-primary-600-400">
	<div class="space-y-2">
		<label class="label"
			><span class="label-text">Hostname</span>
			<input
				class="input"
				bind:value={hostname}
				maxlength="64"
				placeholder="audiolight"
				disabled={busy}
				aria-invalid={!!errors.hostname}
				aria-describedby="hostname-error"
			>
			{#if errors.hostname}
				<small id="hostname-error" class="text-error-600-400">{errors.hostname}</small>
			{/if}</label
		>
	</div>
	<footer class="form-actions">
		{#if busy}
			<span class="mr-auto flex items-center gap-2 text-sm" role="status"><RefreshCwIcon class="spin-360" />Saving hostname…</span>
		{/if}
		<button class="btn preset-filled-primary-500" type="button" disabled={busy} onclick={() => void save()}>Save hostname</button>
	</footer>
</form>
