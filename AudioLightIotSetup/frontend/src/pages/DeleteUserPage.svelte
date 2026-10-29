<script lang="ts">
import type { Client } from '@mxmauro/iot-comm.js';
import ConfirmationDialog from '../components/ConfirmationDialog.svelte';
import { errorMessage, type Notice, preventSubmit } from '../lib/ui';

let { client, onnotice }: { client: Client; onnotice: (notice: Notice) => void } = $props();
let name = $state('');
let errors = $state<Record<string, string>>({});
let confirming = $state(false);
let busy = $state(false);

// -----------------------------------------------------------------------------

const requestDelete = (): void => {
	// Require a target user name before opening the destructive-action confirmation.
	errors = {};
	if (!name.trim()) {
		errors.name = 'Enter the user name to delete.';
		return;
	}
	confirming = true;
};

const confirmDelete = async (): Promise<void> => {
	// Execute the confirmed deletion and close the dialog for either success or failure.
	busy = true;
	try {
		await client.deleteUserCommand(name.trim());
		name = '';
		confirming = false;
		onnotice({ tone: 'success', message: 'User deleted.' });
	} catch (error) {
		confirming = false;
		onnotice({ tone: 'error', message: errorMessage(error) });
	} finally {
		busy = false;
	}
};
</script>

<form class="card bg-surface-100-900 p-4 w-full max-w-3xl mx-auto space-y-4" onsubmit={preventSubmit}>
	<header>
		<h1 class="text-base font-semibold text-primary-600-400">Delete user</h1>
	</header>
	<hr class="border-primary-600-400">
	<div class="space-y-2">
		<label class="label"
			><span class="label-text">User name</span>
			<input class="input" bind:value={name} disabled={busy} aria-invalid={!!errors.name} aria-describedby="delete-user-name-error">
			{#if errors.name}
				<small id="delete-user-name-error" class="text-error-600-400">{errors.name}</small>
			{/if}</label
		>
	</div>
	<footer class="form-actions">
		<button class="btn preset-filled-error-500" type="button" disabled={busy} onclick={requestDelete}>Delete user</button>
	</footer>
</form>
{#if confirming}
	<ConfirmationDialog
		title="Delete user"
		summary={`Delete the user “${name.trim()}”. They will no longer be able to connect.`}
		{busy}
		oncancel={() => (confirming = false)}
		onconfirm={() => void confirmDelete()}
	/>
{/if}
