<script lang="ts">
import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw';
import type { Client, InputConfig, LightingConfig } from '@mxmauro/iot-comm.js';
import ConfirmationDialog from '../components/ConfirmationDialog.svelte';
import { GPIO_MAX, GPIO_MIN, getInputConfigurationValidationErrors } from '../lib/device-config';
import { errorMessage, type Notice, preventSubmit } from '../lib/ui';

const INPUT_FIELDS: readonly [keyof InputConfig, string][] = [
	['resetGpio', 'Reset button'],
	['irGpio', 'IR receiver']
];
const DEFAULT_INPUT_CONFIGURATION: InputConfig = {};

// -----------------------------------------------------------------------------

let {
	client,
	inputConfiguration,
	lightingConfiguration,
	onconfigurationchange,
	onrefresh,
	onnotice,
	registerLeaveGuard
}: {
	client: Client;
	inputConfiguration: InputConfig;
	lightingConfiguration: LightingConfig;
	onconfigurationchange: (configuration: InputConfig) => void;
	onrefresh: () => Promise<void>;
	onnotice: (notice: Notice) => void;
	registerLeaveGuard: (guard: () => Promise<boolean>) => () => void;
} = $props();
let draft = $state<InputConfig>({ ...DEFAULT_INPUT_CONFIGURATION });
let persistedDraft = $state<InputConfig>({ ...DEFAULT_INPUT_CONFIGURATION });
let busy = $state(false);
let errors = $state<Record<string, string>>({});
let discardConfirmation = $state<'leave' | 'reload' | null>(null);
let leaveRequest = $state<{ resolve: (allowed: boolean) => void } | null>(null);
let isDirty = $derived(JSON.stringify(draft) !== JSON.stringify(persistedDraft));

// -----------------------------------------------------------------------------

$effect(() => {
	inputConfiguration;
	draft = { ...inputConfiguration };
	persistedDraft = { ...inputConfiguration };
	errors = {};
});

const updateGpio = (field: keyof InputConfig, value: number): void => {
	errors = {};
	draft = { ...draft, [field]: Math.min(value, GPIO_MAX) };
};

const setConnected = (field: keyof InputConfig, connected: boolean): void => {
	errors = {};
	draft = { ...draft, [field]: connected ? GPIO_MIN : undefined };
};

const save = async (): Promise<void> => {
	errors = getInputConfigurationValidationErrors(draft, lightingConfiguration);
	if (Object.keys(errors).length > 0) {
		return;
	}
	busy = true;
	try {
		await client.setInputConfigCommand(draft);
		onconfigurationchange(draft);
		onnotice({ tone: 'success', message: 'Input configuration saved. Reboot the device to activate it.' });
	} catch (error) {
		onnotice({ tone: 'error', message: errorMessage(error) });
	} finally {
		busy = false;
	}
};

const requestReload = (): void => {
	if (isDirty) {
		discardConfirmation = 'reload';
		return;
	}
	void reload();
};

const reload = async (): Promise<void> => {
	busy = true;
	try {
		await onrefresh();
	} catch (error) {
		onnotice({ tone: 'error', message: errorMessage(error) });
	} finally {
		busy = false;
	}
};

const requestLeave = async (): Promise<boolean> => {
	if (!isDirty) {
		return true;
	}
	return new Promise((resolve) => {
		leaveRequest = { resolve };
		discardConfirmation = 'leave';
	});
};

const discardChanges = (): void => {
	const action = discardConfirmation;
	discardConfirmation = null;
	if (action === 'leave') {
		leaveRequest?.resolve(true);
		leaveRequest = null;
	} else {
		void reload();
	}
};

const keepChanges = (): void => {
	if (discardConfirmation === 'leave') {
		leaveRequest?.resolve(false);
		leaveRequest = null;
	}
	discardConfirmation = null;
};

$effect(() => {
	return registerLeaveGuard(requestLeave);
});
</script>

<form class="card bg-surface-100-900 p-4 w-full max-w-3xl mx-auto space-y-4" onsubmit={preventSubmit}>
	<header class="flex items-start justify-between gap-3">
		<h1 class="text-base font-semibold text-primary-600-400">Input configuration</h1>
		<button
			class="btn btn-icon shrink-0 !border-0 !bg-transparent text-warning-500 hover:!bg-transparent hover:text-warning-400"
			type="button"
			disabled={busy}
			aria-label="Reload input and lighting configuration"
			title="Reload input and lighting configuration"
			onclick={requestReload}
		>
			<RefreshCwIcon class={busy ? 'spin-360' : undefined} />
		</button>
	</header>
	<hr class="border-primary-600-400">
	<fieldset class="fieldset space-y-4">
		<legend class="legend">GPIO assignments</legend>
		{#each INPUT_FIELDS as field}
			<div class="grid items-end gap-3 sm:grid-cols-[minmax(0,1fr)_10rem]">
				<div class="space-y-2">
					<label class="flex items-center gap-2">
						<input
							class="checkbox"
							type="checkbox"
							checked={draft[field[0]] !== undefined}
							disabled={busy}
							onchange={(event) => setConnected(field[0], event.currentTarget.checked)}
						>
						{field[1]}
						connected
					</label>
					<small class="block text-surface-600-400">Leave unchecked when this input is not wired.</small>
				</div>
				<label class="label">
					<span class="label-text">GPIO</span>
					<input
						class="input"
						type="number"
						min={GPIO_MIN}
						max={GPIO_MAX}
						value={draft[field[0]] ?? ''}
						disabled={busy || draft[field[0]] === undefined}
						aria-invalid={!!errors[field[0]]}
						aria-describedby={`${field[0]}-error`}
						oninput={(event) => updateGpio(field[0], Number(event.currentTarget.value))}
					>
				</label>
			</div>
			{#if errors[field[0]]}
				<small id={`${field[0]}-error`} class="block text-error-600-400">{errors[field[0]]}</small>
			{/if}
		{/each}
	</fieldset>
	<footer class="form-actions">
		<small class="mr-auto self-end">Firmware validates board-specific and reserved GPIO pins.</small>
		<button class="btn preset-filled-primary-500" type="button" disabled={busy || !isDirty} onclick={() => void save()}>Save</button>
	</footer>
</form>

{#if discardConfirmation === 'leave'}
	<ConfirmationDialog
		title="Discard input configuration changes"
		summary="Your changes have not been saved. Discard them and leave this page?"
		oncancel={keepChanges}
		onconfirm={discardChanges}
	/>
{:else if discardConfirmation === 'reload'}
	<ConfirmationDialog
		title="Discard input configuration changes"
		summary="Your changes have not been saved. Discard them and reload the configuration?"
		oncancel={keepChanges}
		onconfirm={discardChanges}
	/>
{/if}
