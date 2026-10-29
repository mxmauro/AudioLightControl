<script lang="ts">
import CirclePlusIcon from '@lucide/svelte/icons/circle-plus';
import CircleXIcon from '@lucide/svelte/icons/circle-x';
import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw';
import { tick } from 'svelte';
import ConfirmationDialog from '../components/ConfirmationDialog.svelte';
import { DMX_SPECIAL_CHANNELS, type DmxColoring, isDmxSpecialChannel } from '../lib/device-config';
import { type DmxFixturePreset, dmxFixtureManager, normalizeFixture, normalizeFixtureType } from '../lib/dmx/fixture-manager';
import { errorMessage, type Notice, preventSubmit } from '../lib/ui';

const EMPTY_FIXTURE: DmxFixturePreset = { type: '', coloring: ['Color.Red'] };

// -----------------------------------------------------------------------------

type PendingAction = { type: 'reload' | 'new' } | { type: 'select' | 'delete'; index: number };

// -----------------------------------------------------------------------------

let {
	onnotice,
	registerLeaveGuard
}: {
	onnotice: (notice: Notice) => void;
	registerLeaveGuard: (guard: () => Promise<boolean>) => () => void;
} = $props();
let fixtures = $state<DmxFixturePreset[]>([]);
let fixtureList = $state<HTMLUListElement>();
let channelMappingList = $state<HTMLDivElement>();
let selectedIndex = $state<number | null>(null);
let draft = $state<DmxFixturePreset>({ ...EMPTY_FIXTURE, coloring: [...EMPTY_FIXTURE.coloring] });
let persistedDraft = $state<DmxFixturePreset>({ ...EMPTY_FIXTURE, coloring: [...EMPTY_FIXTURE.coloring] });
let originalType = $state<string | null>(null);
let loading = $state(true);
let saving = $state(false);
let error = $state('');
let confirmation = $state<'replace' | 'delete' | 'save-before-action' | 'save-before-leave' | null>(null);
let pendingAction = $state<PendingAction | null>(null);
let leaveRequest = $state<{ resolve: (allowed: boolean) => void } | null>(null);
let isNew = $derived(originalType === null);
let isDirty = $derived(
	draft.type !== persistedDraft.type ||
		draft.coloring.length !== persistedDraft.coloring.length ||
		draft.coloring.some((channel, index) => channel !== persistedDraft.coloring[index])
);

$effect(() => {
	void load();
});

// -----------------------------------------------------------------------------

const load = async (): Promise<void> => {
	// Reload the shared preset collection and restore the current selection when possible.
	loading = true;
	error = '';
	confirmation = null;
	try {
		await dmxFixtureManager.load();
		fixtures = dmxFixtureManager.all();
		if (selectedIndex !== null && fixtures[selectedIndex]) {
			selectFixture(selectedIndex);
		} else if (fixtures.length > 0) {
			selectFixture(0);
		} else {
			startNewFixture();
		}
	} catch (reason) {
		error = errorMessage(reason);
	} finally {
		loading = false;
	}
};

const selectFixture = (index: number): void => {
	// Copy the selected preset into an independent draft so edits are not persisted until saved.
	const fixture = fixtures[index];
	if (!fixture) {
		return;
	}
	selectedIndex = index;
	draft = { ...fixture, coloring: [...fixture.coloring] };
	persistedDraft = { ...fixture, coloring: [...fixture.coloring] };
	originalType = fixture.type;
	error = '';
};

const startNewFixture = (): void => {
	// Reset the form to a minimal valid draft for a new predefined fixture.
	selectedIndex = null;
	draft = { ...EMPTY_FIXTURE, coloring: [...EMPTY_FIXTURE.coloring] };
	persistedDraft = { ...EMPTY_FIXTURE, coloring: [...EMPTY_FIXTURE.coloring] };
	originalType = null;
	error = '';
};

const revealNewFixture = (): void => {
	// New presets append to the collection, so move the list viewport directly to its bottom.
	if (!fixtureList) {
		return;
	}
	fixtureList.scrollTop = fixtureList.scrollHeight;
};

const updateType = (type: string): void => {
	draft = { ...draft, type };
	error = '';
};

const updateColoring = (index: number, value: string): void => {
	const coloring: DmxColoring = isDmxSpecialChannel(value) ? value : 0;
	draft = { ...draft, coloring: draft.coloring.map((channel, item) => (item === index ? coloring : channel)) };
	error = '';
};

const updateCustomColoring = (index: number, value: number): void => {
	draft = { ...draft, coloring: draft.coloring.map((channel, item) => (item === index ? value : channel)) };
	error = '';
};

const addChannel = async (): Promise<void> => {
	if (draft.coloring.length < 32) {
		draft = { ...draft, coloring: [...draft.coloring, 0] };
		await tick();
		if (channelMappingList) {
			channelMappingList.scrollTop = channelMappingList.scrollHeight;
		}
	}
};

const removeChannel = (index: number): void => {
	draft = { ...draft, coloring: draft.coloring.filter((_, item) => item !== index) };
};

const requestSave = (): void => {
	// Validate first, then require confirmation only when a different preset would be replaced.
	try {
		const normalized = normalizeFixture(draft);
		const existing = dmxFixtureManager.find(normalized.type);
		if (existing && (isNew || !sameType(originalType, normalized.type))) {
			confirmation = 'replace';
			return;
		}
		void save(false);
	} catch (reason) {
		error = errorMessage(reason);
	}
};

const save = async (replace: boolean): Promise<boolean> => {
	// Persist either a new preset or an atomic update, retaining the draft when storage fails.
	saving = true;
	error = '';
	try {
		const wasNew = originalType === null;
		const normalized = normalizeFixture(draft);
		const previousType = originalType;
		if (previousType === null) {
			await dmxFixtureManager.add(normalized, replace);
		} else {
			await dmxFixtureManager.update(previousType, normalized, replace);
		}
		// Refresh the list from the manager and select the stored, normalized fixture.
		fixtures = dmxFixtureManager.all();
		const index = fixtures.findIndex((fixture) => sameType(fixture.type, normalized.type));
		if (index >= 0) {
			selectFixture(index);
			if (wasNew) {
				await tick();
				revealNewFixture();
			}
		}
		onnotice({ tone: 'success', message: 'DMX fixture preset saved.' });
		return true;
	} catch (reason) {
		error = errorMessage(reason);
		return false;
	} finally {
		confirmation = null;
		saving = false;
	}
};

const requestAction = (action: PendingAction): void => {
	// Save a changed draft before navigation-like editor actions can replace its form data.
	if (isDirty) {
		pendingAction = action;
		confirmation = 'save-before-action';
		return;
	}
	executeAction(action);
};

const saveBeforeAction = async (): Promise<void> => {
	// Continue only after persistence succeeds, so the requested action never discards an unsaved draft.
	const action = pendingAction;
	if (!action || !(await save(false))) {
		return;
	}
	pendingAction = null;
	executeAction(action);
};

const requestLeave = async (): Promise<boolean> => {
	// Defer page replacement until the user saves or cancels a changed fixture draft.
	if (!isDirty) {
		return true;
	}
	return new Promise((resolve) => {
		leaveRequest = { resolve };
		confirmation = 'save-before-leave';
	});
};

const saveBeforeLeave = async (): Promise<void> => {
	// Resolve the page guard only after the current draft is stored successfully.
	const request = leaveRequest;
	const saved = await save(false);
	leaveRequest = null;
	request?.resolve(saved);
};

const cancelLeave = (): void => {
	// Allow the requested navigation to discard the draft by replacing or unmounting this editor.
	leaveRequest?.resolve(true);
	leaveRequest = null;
	confirmation = null;
};

const discardBeforeAction = (): void => {
	// Discard the changed draft by executing the pending action without persisting it.
	const action = pendingAction;
	pendingAction = null;
	confirmation = null;
	if (action) {
		executeAction(action);
	}
};

$effect(() => {
	// Register while mounted so the workspace asks this editor before it replaces the page.
	return registerLeaveGuard(requestLeave);
});

const executeAction = (action: PendingAction): void => {
	if (action.type === 'reload') {
		void load();
		return;
	}
	if (action.type === 'new') {
		startNewFixture();
		return;
	}
	if (action.type === 'select') {
		selectFixture(action.index);
		return;
	}
	if (action.type === 'delete') {
		selectFixture(action.index);
		requestDelete();
	}
};

const requestDelete = (): void => {
	if (!isNew) {
		confirmation = 'delete';
	}
};

const remove = async (): Promise<void> => {
	// Delete the selected preset only after confirmation, then choose the nearest remaining item.
	if (isNew) {
		return;
	}
	saving = true;
	error = '';
	try {
		const fixtureType = originalType;
		if (fixtureType === null) {
			return;
		}
		await dmxFixtureManager.remove(fixtureType);
		fixtures = dmxFixtureManager.all();
		if (fixtures.length > 0) {
			selectFixture(Math.min(selectedIndex ?? 0, fixtures.length - 1));
		} else {
			startNewFixture();
		}
		onnotice({ tone: 'success', message: 'DMX fixture preset deleted.' });
	} catch (reason) {
		error = errorMessage(reason);
	} finally {
		confirmation = null;
		saving = false;
	}
};

const boundedInput = (input: HTMLInputElement, maximum: number): number => {
	const value = Number(input.value);
	if (value > maximum) {
		input.value = String(maximum);
		return maximum;
	}
	return value;
};

const sameType = (left: string | null, right: string): boolean => {
	return left !== null && normalizeFixtureType(left).localeCompare(normalizeFixtureType(right), undefined, { sensitivity: 'accent' }) === 0;
};
</script>

<form class="card bg-surface-100-900 mx-auto flex h-full min-h-0 w-full max-w-3xl flex-col gap-4 p-4" onsubmit={preventSubmit}>
	<header class="flex items-start justify-between gap-3">
		<div><h1 class="text-base font-semibold text-primary-600-400">DMX Fixtures editor</h1></div>
		<button
			class="btn btn-icon shrink-0 !border-0 !bg-transparent text-warning-500 hover:!bg-transparent hover:text-warning-400 active:!bg-transparent active:text-warning-600"
			type="button"
			disabled={loading || saving}
			aria-label="Reload predefined DMX fixtures"
			title="Reload predefined DMX fixtures"
			onclick={() => requestAction({ type: 'reload' })}
		>
			<RefreshCwIcon class={loading ? 'spin-360' : undefined} />
		</button>
	</header>
	<hr class="border-primary-600-400">
	{#if loading}
		<div class="flex flex-1 items-center justify-center gap-3 py-12" role="status">
			<RefreshCwIcon class="spin-360" />Loading DMX fixtures…
		</div>
	{:else}
		<div class="grid min-h-0 flex-1 gap-4 md:items-stretch md:grid-cols-[12rem_auto_minmax(0,1fr)]">
			<div class="flex min-h-0 min-w-0 flex-col">
				<div class="mb-2 flex items-center gap-2">
					<span class="label-text pl-1 text-surface-950-50">Fixtures</span>
					<button
						class="btn btn-icon-sm shrink-0 !border-0 !bg-transparent !p-0 text-white hover:!bg-transparent hover:text-surface-300 active:!bg-transparent active:text-surface-400"
						type="button"
						disabled={saving}
						aria-label="Add preset"
						title="Add preset"
						onclick={() => requestAction({ type: 'new' })}
					>
						<CirclePlusIcon />
					</button>
				</div>
				<ul bind:this={fixtureList} class="min-h-0 flex-1 space-y-1 overflow-y-auto" aria-label="Predefined DMX fixtures">
					{#each fixtures as fixture, index}
						<li>
							<div
								class={`flex min-w-0 items-center rounded-[var(--radius-base)] px-1 ${
									selectedIndex === index && !isNew ? 'preset-filled-primary-500' : 'hover:preset-filled-surface-500'
								}`}
							>
								<button
									class="min-w-0 flex-1 truncate py-1 text-left text-xs"
									type="button"
									disabled={saving}
									onclick={() => requestAction({ type: 'select', index })}
								>
									#{index + 1}: {fixture.type || 'Untitled fixture'}
								</button>
								<button
									class="btn btn-icon-sm shrink-0 !justify-end !border-0 !bg-transparent !p-0 text-error-500 hover:!bg-transparent hover:text-error-400 active:!bg-transparent active:text-error-600"
									type="button"
									disabled={saving}
									aria-label={`Remove fixture ${index + 1}`}
									title="Remove fixture"
									onclick={() => requestAction({ type: 'delete', index })}
								>
									<CircleXIcon />
								</button>
							</div>
						</li>
					{/each}
				</ul>
			</div>
			<span class="vr hidden h-auto self-stretch md:block" aria-hidden="true"></span>
			<section class="flex min-h-0 min-w-0 flex-col">
				<label class="label"
					><span class="label-text">{isNew ? 'New fixture type' : `Fixture #${(selectedIndex ?? 0) + 1} type`}</span>
					<input class="input" value={draft.type} disabled={saving} oninput={(event) => updateType(event.currentTarget.value)}></label
				>
				<div class="mt-4 flex min-h-0 flex-1 flex-col gap-2">
					<div class="flex items-center gap-3">
						<span class="label-text text-surface-950-50">Channel mappings</span>
						<button
							class="btn btn-icon-sm shrink-0 !border-0 !bg-transparent !p-0 text-white hover:!bg-transparent hover:text-surface-300 active:!bg-transparent active:text-surface-400"
							type="button"
							disabled={saving || draft.coloring.length >= 32}
							aria-label="Add channel"
							title="Add channel"
							onclick={() => void addChannel()}
						>
							<CirclePlusIcon />
						</button>
					</div>
					<div bind:this={channelMappingList} class="min-h-0 flex-1 space-y-2 overflow-y-auto pr-1">
						{#each draft.coloring as coloring, index}
							<div class="grid grid-cols-[3rem_minmax(0,1fr)_auto] items-start gap-3">
								<span class="pt-2 text-sm">#{index + 1}</span>
								<div class="flex min-w-0 flex-col gap-2 sm:flex-row">
									<select
										class="select"
										value={typeof coloring === 'number' ? 'custom' : coloring}
										disabled={saving}
										onchange={(event) => updateColoring(index, event.currentTarget.value)}
									>
										{#each DMX_SPECIAL_CHANNELS as channel}
											<option value={channel}>{channel}</option>
										{/each}
										<option value="custom">Custom</option>
									</select>
									{#if typeof coloring === 'number'}
										<input
											class="input !w-24"
											type="number"
											min="0"
											max="255"
											value={coloring}
											disabled={saving}
											aria-label="Custom mapping value"
											oninput={(event) => updateCustomColoring(index, boundedInput(event.currentTarget, 255))}
										>
									{/if}
								</div>
								<button
									class="btn btn-icon-sm shrink-0 self-center !border-0 !bg-transparent !p-0 text-error-500 hover:!bg-transparent hover:text-error-400 active:!bg-transparent active:text-error-600"
									type="button"
									disabled={saving}
									aria-label={`Remove channel ${index + 1}`}
									title="Remove channel"
									onclick={() => removeChannel(index)}
								>
									<CircleXIcon />
								</button>
							</div>
						{/each}
					</div>
				</div>
				{#if error}
					<p class="mt-4 text-sm text-error-600-400" role="alert">{error}</p>
				{/if}
				<footer class="form-actions mt-5">
					<button class="btn preset-filled-primary-500" type="button" disabled={saving || !isDirty} onclick={requestSave}>
						Save preset
					</button>
				</footer>
			</section>
		</div>
	{/if}
</form>

{#if confirmation === 'replace'}
	<ConfirmationDialog
		title="Replace DMX fixture preset"
		summary={`Replace the existing preset named ${normalizeFixtureType(draft.type) || 'Untitled fixture'}?`}
		busy={saving}
		oncancel={() => (confirmation = null)}
		onconfirm={() => void save(true)}
	/>
{:else if confirmation === 'save-before-action'}
	<ConfirmationDialog
		title="Save fixture preset changes"
		summary="Save the current fixture preset before continuing?"
		busy={saving}
		oncancel={discardBeforeAction}
		onconfirm={() => void saveBeforeAction()}
	/>
{:else if confirmation === 'save-before-leave'}
	<ConfirmationDialog
		title="Save fixture preset changes"
		summary="Save the current fixture preset before leaving this page?"
		busy={saving}
		oncancel={cancelLeave}
		onconfirm={() => void saveBeforeLeave()}
	/>
{:else if confirmation === 'delete'}
	<ConfirmationDialog
		title="Delete DMX fixture preset"
		summary={`Delete the preset named ${originalType || 'Untitled fixture'}?`}
		busy={saving}
		oncancel={() => (confirmation = null)}
		onconfirm={() => void remove()}
	/>
{/if}
