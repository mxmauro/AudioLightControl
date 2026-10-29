<script lang="ts">
import ChevronDownIcon from '@lucide/svelte/icons/chevron-down';
import CirclePlusIcon from '@lucide/svelte/icons/circle-plus';
import CircleXIcon from '@lucide/svelte/icons/circle-x';
import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw';
import type { Client } from '@mxmauro/iot-comm.js';
import { onMount, tick } from 'svelte';
import ConfirmationDialog from '../components/ConfirmationDialog.svelte';
import {
	type DeviceConfiguration,
	DMX_SPECIAL_CHANNELS,
	type DmxColoring,
	type DmxFixture,
	GPIO_MAX,
	GPIO_MIN,
	getDeviceConfiguration,
	getDeviceConfigurationValidationErrors,
	isDmxSpecialChannel,
	setDeviceConfiguration
} from '../lib/device-config';
import { type DmxFixturePreset, fixturesMatch, getDmxFixturePresets, normalizeFixtureType } from '../lib/dmx/fixture-manager';
import { errorMessage, type Notice, preventSubmit } from '../lib/ui';

let {
	client,
	onnotice,
	registerLeaveGuard
}: {
	client: Client;
	onnotice: (notice: Notice) => void;
	registerLeaveGuard: (guard: () => Promise<boolean>) => () => void;
} = $props();
let draft = $state<DeviceConfiguration>({ output: 'none' });
let persistedDraft = $state<DeviceConfiguration>({ output: 'none' });
let noneDraft = $state<Extract<DeviceConfiguration, { output: 'none' }>>({ output: 'none' });
let rgbDraft = $state<Extract<DeviceConfiguration, { output: 'rgb5050' }>>({
	output: 'rgb5050',
	redGpio: 0,
	greenGpio: 1,
	blueGpio: 2,
	activeLow: true
});
let dmxDraft = $state<Extract<DeviceConfiguration, { output: 'dmx' }>>({ output: 'dmx', txGpio: 0, fixtures: [] });
let fixtureTypes = $state<DmxFixturePreset[]>([]);
let loading = $state(true);
let hasLoaded = $state(false);
let loadError = $state('');
let fixtureLoading = $state(true);
let fixtureLoadError = $state('');
let saving = $state(false);
let busy = $derived(loading || saving);
let fixtureControlsDisabled = $derived(busy || fixtureLoading || fixtureLoadError.length > 0);
let confirming = $state(false);
let discardConfirmation = $state<'leave' | 'reload' | null>(null);
let leaveRequest = $state<{ resolve: (allowed: boolean) => void } | null>(null);
let errors = $state<Record<string, string>>({});
let selectedFixtureIndex = $state<number | null>(null);
let presetPicker = $state<HTMLDivElement>();
let fixtureList = $state<HTMLUListElement>();
let channelMappingList = $state<HTMLDivElement>();
let presetMenuOpen = $state(false);
let selectedFixture = $derived(draft.output === 'dmx' && selectedFixtureIndex !== null ? draft.fixtures[selectedFixtureIndex] : undefined);
let isDirty = $derived(JSON.stringify(draft) !== JSON.stringify(persistedDraft));

$effect(() => {
	void client;
	void load();
});

// -----------------------------------------------------------------------------

const load = async (): Promise<void> => {
	// Fetch the device draft and shared preset list together so the page can report either failure independently.
	loading = true;
	hasLoaded = false;
	loadError = '';
	fixtureLoading = true;
	fixtureLoadError = '';
	confirming = false;
	const [configurationResult, fixtureResult] = await Promise.allSettled([getDeviceConfiguration(client), getDmxFixturePresets()]);
	if (fixtureResult.status === 'fulfilled') {
		fixtureTypes = fixtureResult.value;
	} else {
		fixtureLoadError = errorMessage(fixtureResult.reason);
		onnotice({ tone: 'error', message: `Unable to load DMX fixture presets: ${fixtureLoadError}` });
	}
	if (configurationResult.status === 'fulfilled') {
		const configuration = configurationResult.value.configuration;
		resetControllerDrafts(configuration);
		draft = configuration;
		persistedDraft = configuration;
		selectedFixtureIndex = configuration.output === 'dmx' && configuration.fixtures.length > 0 ? 0 : null;
		hasLoaded = true;
	} else {
		loadError = errorMessage(configurationResult.reason);
		onnotice({ tone: 'error', message: loadError });
	}
	loading = false;
	fixtureLoading = false;
};

const setOutput = (output: DeviceConfiguration['output']): void => {
	// Keep each controller draft intact while the user temporarily switches to another controller type.
	errors = {};
	presetMenuOpen = false;
	storeCurrentControllerDraft();
	if (output === 'none') {
		draft = noneDraft;
	} else if (output === 'rgb5050') {
		draft = rgbDraft;
	} else {
		draft = dmxDraft;
		selectedFixtureIndex = draft.fixtures.length > 0 ? Math.min(selectedFixtureIndex ?? 0, draft.fixtures.length - 1) : null;
	}
};

const updateRgb = (field: 'redGpio' | 'greenGpio' | 'blueGpio', value: number): void => {
	errors = {};
	if (draft.output === 'rgb5050') {
		draft = { ...draft, [field]: value };
	}
};

const getMaximumBoundedInputValue = (input: HTMLInputElement, maximum: number): number => {
	const value = Number(input.value);
	if (value > maximum) {
		input.value = String(maximum);
		return maximum;
	}
	return value;
};

const setActiveLow = (activeLow: boolean): void => {
	errors = {};
	if (draft.output === 'rgb5050') {
		draft = { ...draft, activeLow };
	}
};

const updateDmxTx = (value: number): void => {
	errors = {};
	if (draft.output === 'dmx') {
		draft = { ...draft, txGpio: value };
	}
};

const updateFixture = (index: number, update: Partial<DmxFixture>): void => {
	errors = {};
	if (draft.output === 'dmx') {
		draft = { ...draft, fixtures: draft.fixtures.map((fixture, item) => (item === index ? { ...fixture, ...update } : fixture)) };
	}
};

const getFixtureType = (type: string): DmxFixturePreset | undefined => {
	// Match fixture types with the same trimmed, case-insensitive rules used by preset persistence.
	const normalized = normalizeFixtureType(type);
	return fixtureTypes.find((fixtureType) => fixtureType.type.localeCompare(normalized, undefined, { sensitivity: 'accent' }) === 0);
};

const updateFixtureType = (index: number, type: string): void => updateFixture(index, { type });

const selectFixtureType = (index: number, fixtureType: DmxFixturePreset): void => {
	// Only an explicit picker selection replaces the fixture's channel mappings.
	updateFixture(index, { type: fixtureType.type, coloring: [...fixtureType.coloring] });
	presetMenuOpen = false;
};

const togglePresetMenu = (): void => {
	presetMenuOpen = !presetMenuOpen;
};

const resetControllerDrafts = (configuration: DeviceConfiguration): void => {
	// Reset all cached drafts only when fresh device configuration is loaded or reapplied.
	noneDraft = { output: 'none' };
	rgbDraft = { output: 'rgb5050', redGpio: 0, greenGpio: 1, blueGpio: 2, activeLow: true };
	dmxDraft = { output: 'dmx', txGpio: 0, fixtures: [] };
	if (configuration.output === 'none') {
		noneDraft = configuration;
	} else if (configuration.output === 'rgb5050') {
		rgbDraft = configuration;
	} else {
		dmxDraft = configuration;
	}
};

const storeCurrentControllerDraft = (): void => {
	if (draft.output === 'none') {
		noneDraft = draft;
	} else if (draft.output === 'rgb5050') {
		rgbDraft = draft;
	} else {
		dmxDraft = draft;
	}
};

const closePresetMenuOnOutside = (event: PointerEvent): void => {
	if (presetPicker && event.target instanceof Node && !presetPicker.contains(event.target)) {
		presetMenuOpen = false;
	}
};

onMount(() => {
	// Close the picker when focus moves to any control outside its input-and-menu container.
	document.addEventListener('pointerdown', closePresetMenuOnOutside);
	return () => document.removeEventListener('pointerdown', closePresetMenuOnOutside);
});

const fixtureDiffersFromPreset = (fixture: DmxFixture): boolean => {
	// Flag a matching preset name whose editable device channels no longer match the preset mapping.
	const preset = getFixtureType(fixture.type);
	return preset !== undefined && !fixturesMatch(preset, fixture.coloring);
};

const scrollToBottom = (element: HTMLElement | undefined): void => {
	if (element) {
		element.scrollTop = element.scrollHeight;
	}
};

const requestReload = (): void => {
	// Reloading replaces the editable draft, so ask before it discards unapplied changes.
	if (isDirty) {
		discardConfirmation = 'reload';
		return;
	}
	void load();
};

const requestLeave = async (): Promise<boolean> => {
	// Defer page replacement until the user explicitly discards their unapplied configuration.
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
	} else if (action === 'reload') {
		void load();
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
	// Register while mounted so workspace navigation and application close requests preserve this draft.
	return registerLeaveGuard(requestLeave);
});

const getNextFixtureStartChannel = (fixtures: DmxFixture[]): number => {
	if (fixtures.length === 0) {
		return 1;
	}

	// Place the new fixture at the highest existing channel allocation, without exceeding the DMX universe.
	const highestChannel = Math.max(...fixtures.map((fixture) => fixture.startChannel + fixture.coloring.length));
	return Math.min(highestChannel, 128);
};

const addFixture = async (): Promise<void> => {
	errors = {};
	if (draft.output === 'dmx' && draft.fixtures.length < 16) {
		const fixtureIndex = draft.fixtures.length;
		const fixtureType = fixtureTypes[0];
		const startChannel = getNextFixtureStartChannel(draft.fixtures);
		draft = {
			...draft,
			fixtures: [
				...draft.fixtures,
				fixtureType
					? { type: fixtureType.type, startChannel, coloring: [...fixtureType.coloring] }
					: { type: '', startChannel, coloring: [0] }
			]
		};
		selectedFixtureIndex = fixtureIndex;
		await tick();
		scrollToBottom(fixtureList);
	}
};

const removeFixture = (index: number): void => {
	errors = {};
	if (draft.output === 'dmx') {
		const fixtures = draft.fixtures.filter((_, item) => item !== index);
		draft = { ...draft, fixtures };
		if (selectedFixtureIndex === null) {
			return;
		}
		if (selectedFixtureIndex > index) {
			selectedFixtureIndex--;
		} else if (selectedFixtureIndex === index) {
			selectedFixtureIndex = fixtures.length > 0 ? Math.min(index, fixtures.length - 1) : null;
		}
	}
};

const updateColoring = (fixtureIndex: number, channelIndex: number, value: string): void => {
	if (draft.output !== 'dmx') {
		return;
	}
	errors = {};
	const coloring: DmxColoring = isDmxSpecialChannel(value) ? value : 0;
	const fixture = draft.fixtures[fixtureIndex];
	updateFixture(fixtureIndex, { coloring: fixture.coloring.map((color, item) => (item === channelIndex ? coloring : color)) });
};

const updateCustomColoring = (fixtureIndex: number, channelIndex: number, value: number): void => {
	if (draft.output !== 'dmx') {
		return;
	}
	errors = {};
	const fixture = draft.fixtures[fixtureIndex];
	updateFixture(fixtureIndex, { coloring: fixture.coloring.map((color, item) => (item === channelIndex ? value : color)) });
};

const addChannel = async (fixtureIndex: number): Promise<void> => {
	errors = {};
	if (draft.output === 'dmx' && draft.fixtures[fixtureIndex].coloring.length < 32) {
		updateFixture(fixtureIndex, { coloring: [...draft.fixtures[fixtureIndex].coloring, 0] });
		await tick();
		scrollToBottom(channelMappingList);
	}
};

const removeChannel = (fixtureIndex: number, channelIndex: number): void => {
	errors = {};
	if (draft.output === 'dmx' && draft.fixtures[fixtureIndex].coloring.length > 1) {
		updateFixture(fixtureIndex, { coloring: draft.fixtures[fixtureIndex].coloring.filter((_, item) => item !== channelIndex) });
	}
};

const requestApply = (): void => {
	errors = getDeviceConfigurationValidationErrors(draft);
	const invalidFixtureIndex = getFirstInvalidFixtureIndex();
	if (invalidFixtureIndex !== undefined) {
		selectedFixtureIndex = invalidFixtureIndex;
	}
	if (Object.keys(errors).length > 0) {
		return;
	}
	confirming = true;
};

const apply = async (): Promise<void> => {
	saving = true;
	try {
		await setDeviceConfiguration(client, draft);
		confirming = false;
		onnotice({ tone: 'success', message: 'Lighting configuration applied.' });
		await load();
	} catch (error) {
		confirming = false;
		onnotice({ tone: 'error', message: errorMessage(error) });
	} finally {
		saving = false;
	}
};

const summary = (): string => {
	if (draft.output === 'none') {
		return 'Turn off the lighting output.';
	}
	if (draft.output === 'rgb5050') {
		return `Set up RGB5050 lighting on pins ${draft.redGpio}, ${draft.greenGpio}, and ${draft.blueGpio} with ${
			draft.activeLow ? 'active-low' : 'active-high'
		} output.`;
	}
	return `Set up DMX lighting on GPIO ${draft.txGpio} with ${draft.fixtures.length} fixture(s).`;
};

const getFixtureLabel = (fixture: DmxFixture): string => fixture.type || 'Custom fixture';

const getFirstInvalidFixtureIndex = (): number | undefined => {
	if (draft.output !== 'dmx') {
		return undefined;
	}
	const fixtureIndex = draft.fixtures.findIndex((_, index) => Object.keys(errors).some((field) => field.startsWith(`fixtures.${index}.`)));
	return fixtureIndex >= 0 ? fixtureIndex : undefined;
};
</script>

<form class="card bg-surface-100-900 mx-auto flex h-full min-h-0 w-full max-w-3xl flex-col gap-4 p-4" onsubmit={preventSubmit}>
	<header class="flex items-start justify-between gap-3">
		<div>
			<h1 class="text-base font-semibold text-primary-600-400">Lighting configuration</h1>
		</div>
		<button
			class="btn btn-icon shrink-0 !border-0 !bg-transparent text-warning-500 hover:!bg-transparent hover:text-warning-400 active:!bg-transparent active:text-warning-600"
			type="button"
			disabled={busy}
			aria-label="Reload lighting configuration"
			title="Reload lighting configuration"
			onclick={requestReload}
		>
			<RefreshCwIcon class={loading ? 'spin-360' : undefined} />
		</button>
	</header>
	<hr class="border-primary-600-400">
	{#if loading}
		<div class="flex flex-1 items-center justify-center gap-3 py-12" role="status">
			<RefreshCwIcon class="spin-360" />
			<span>Loading lighting configuration…</span>
		</div>
	{:else if loadError}
		<p class="rounded-container-token border border-error-500 p-4 text-error-600-400" role="alert">{loadError}</p>
	{:else if hasLoaded}
		<div class="flex min-h-0 flex-1 flex-col gap-4">
			{#if saving}
				<div class="flex items-center gap-3" role="status">
					<RefreshCwIcon class="spin-360" />
					<span>Saving lighting configuration…</span>
				</div>
			{/if}
			<fieldset class="fieldset">
				<legend class="legend">Output controller</legend>
				<div class="space-y-2">
					<select
						class="select"
						value={draft.output}
						disabled={busy}
						aria-label="Output controller"
						aria-invalid={!!errors.configuration}
						aria-describedby="lighting-configuration-error"
						onchange={(event) => setOutput((event.currentTarget as HTMLSelectElement).value as DeviceConfiguration['output'])}
					>
						<option value="none">None</option>
						<option value="rgb5050">RGB5050</option>
						<option value="dmx">DMX</option>
					</select>
					{#if errors.configuration}
						<small id="lighting-configuration-error" class="text-error-600-400">{errors.configuration}</small>
					{/if}
				</div>
			</fieldset>
			{#if draft.output === 'rgb5050'}
				<fieldset class="fieldset">
					<legend class="legend">RGB5050 output</legend>
					<div class="space-y-4">
						<div class="grid grid-cols-3 gap-4">
							{#each [
								['redGpio', 'Red GPIO'],
								['greenGpio', 'Green GPIO'],
								['blueGpio', 'Blue GPIO']
							] as field}
								<label class="label"
									><span class="label-text">{field[1]}</span>
									<input
										class="input"
										type="number"
										min={GPIO_MIN}
										max={GPIO_MAX}
										value={draft[field[0] as 'redGpio' | 'greenGpio' | 'blueGpio']}
										disabled={busy}
										aria-invalid={!!errors[field[0]]}
										aria-describedby={`rgb-${field[0]}-error`}
										oninput={(event) =>
											updateRgb(
												field[0] as 'redGpio' | 'greenGpio' | 'blueGpio',
												getMaximumBoundedInputValue(event.currentTarget, GPIO_MAX)
											)}
									>
									{#if errors[field[0]]}
										<small id={`rgb-${field[0]}-error`} class="text-error-600-400">{errors[field[0]]}</small>
									{/if}</label
								>
							{/each}
						</div>
						<div class="space-y-1">
							<label class="flex items-center gap-2"
								><input
									class="checkbox"
									type="checkbox"
									checked={draft.activeLow}
									disabled={busy}
									onchange={(event) => setActiveLow(event.currentTarget.checked)}
								>Active low output</label
							>
							<small class="block text-surface-600-400">When enabled, a low GPIO level turns the output on.</small>
						</div>
					</div>
				</fieldset>
			{:else if draft.output === 'dmx'}
				<fieldset class="fieldset flex min-h-0 flex-1 flex-col">
					<legend class="legend">DMX output</legend>
					<div class="flex min-h-0 flex-1 flex-col gap-4">
						{#if fixtureLoadError}
							<p class="rounded-container-token border border-error-500 p-3 text-error-600-400" role="alert">
								Unable to load DMX fixture presets. Reload the lighting configuration to try again.
							</p>
						{/if}
						<div class="border-b border-surface-300-700 pb-4">
							<label class="label block w-60"
								><span class="label-text">DMX TX GPIO</span>
								<input
									class="input"
									type="number"
									min={GPIO_MIN}
									max={GPIO_MAX}
									value={draft.txGpio}
									disabled={busy}
									aria-invalid={!!errors.txGpio}
									aria-describedby="dmx-tx-gpio-error"
									oninput={(event) => updateDmxTx(getMaximumBoundedInputValue(event.currentTarget, GPIO_MAX))}
								>
								{#if errors.txGpio}
									<small id="dmx-tx-gpio-error" class="text-error-600-400">{errors.txGpio}</small>
								{/if}</label
							>
						</div>
						<div class="grid min-h-0 flex-1 gap-4 md:items-stretch md:grid-cols-[12rem_auto_minmax(0,1fr)]">
							<div class="flex min-h-0 min-w-0 flex-col">
								<div class="mb-2 flex items-center gap-2">
									<span class="label-text pl-1 text-surface-950-50">Fixtures</span>
									<button
										class="btn btn-icon-sm shrink-0 !border-0 !bg-transparent !p-0 text-white hover:!bg-transparent hover:text-surface-300 active:!bg-transparent active:text-surface-400"
										type="button"
										disabled={fixtureControlsDisabled || draft.fixtures.length >= 16}
										aria-label="Add fixture"
										title="Add fixture"
										onclick={() => void addFixture()}
									>
										<CirclePlusIcon />
									</button>
								</div>
								<ul bind:this={fixtureList} class="min-h-0 flex-1 space-y-1 overflow-y-auto" aria-label="Fixtures">
									{#each draft.fixtures as fixture, fixtureIndex}
										<li>
											<div
												class={`flex min-w-0 items-center rounded-[var(--radius-base)] px-1 ${
													selectedFixtureIndex === fixtureIndex ? 'preset-filled-primary-500' : 'hover:preset-filled-surface-500'
												}`}
											>
												<button
													class="min-w-0 flex-1 truncate py-1 text-left text-xs"
													type="button"
													disabled={busy}
													aria-pressed={selectedFixtureIndex === fixtureIndex}
													onclick={() => (selectedFixtureIndex = fixtureIndex)}
												>
													#{fixtureIndex + 1}: {getFixtureLabel(fixture)}
												</button>
												<button
													class="btn btn-icon-sm shrink-0 !justify-end !border-0 !bg-transparent !p-0 text-error-500 hover:!bg-transparent hover:text-error-400 active:!bg-transparent active:text-error-600"
													type="button"
													disabled={busy}
													aria-label={`Remove fixture ${fixtureIndex + 1}`}
													title="Remove fixture"
													onclick={() => removeFixture(fixtureIndex)}
												>
													<CircleXIcon />
												</button>
											</div>
										</li>
									{/each}
								</ul>
								{#if errors.fixtures}
									<small id="dmx-fixtures-error" class="mt-2 block text-error-600-400">{errors.fixtures}</small>
								{/if}
							</div>
							<span class="vr hidden h-auto self-stretch md:block" aria-hidden="true"></span>
							<section class="flex min-h-0 min-w-0 flex-col">
								{#each draft.fixtures as fixture, fixtureIndex}
									{#if selectedFixtureIndex === fixtureIndex}
										<div class="flex min-h-0 flex-1 flex-col">
											<div class="grid gap-3 md:grid-cols-[minmax(0,1fr)_6rem]">
												<div class="min-w-0 space-y-3">
													<div bind:this={presetPicker} class="relative">
														<label class="label" for={`fixture-${fixtureIndex}-type`}>
															<span class="label-text">Fixture #{fixtureIndex + 1} type</span>
														</label>
														<div
															class="mt-1 flex overflow-hidden rounded-[var(--radius-base)] border-2 border-surface-400-600 shadow-sm focus-within:border-primary-500 focus-within:ring-2 focus-within:ring-primary-500/40"
														>
															<input
																id={`fixture-${fixtureIndex}-type`}
																class="min-w-0 flex-1 border-0 bg-transparent px-3 py-1.5 text-sm/6 text-surface-950-50 shadow-none outline-none placeholder:text-surface-600-400 focus:ring-0"
																value={fixture.type}
																disabled={fixtureControlsDisabled}
																aria-invalid={!!errors[`fixtures.${fixtureIndex}.type`]}
																aria-describedby={`fixture-${fixtureIndex}-type-error`}
																oninput={(event) => updateFixtureType(fixtureIndex, event.currentTarget.value)}
															>
															<button
																class="grid w-10 shrink-0 place-items-center border-l-2 border-surface-400-600 text-surface-600-400 hover:text-surface-950-50 focus:outline-none"
																type="button"
																disabled={fixtureControlsDisabled}
																aria-label="Choose predefined fixture"
																aria-expanded={presetMenuOpen}
																aria-controls={`fixture-${fixtureIndex}-preset-menu`}
																onclick={togglePresetMenu}
															>
																<ChevronDownIcon class="size-4" />
															</button>
														</div>
														{#if presetMenuOpen}
															<div
																id={`fixture-${fixtureIndex}-preset-menu`}
																class="absolute z-10 mt-1 max-h-48 w-full overflow-y-auto rounded-container-token border border-surface-200-800 bg-surface-100-900 p-1 shadow-lg"
																role="listbox"
															>
																{#each fixtureTypes as fixtureType}
																	<button
																		class="block w-full rounded-container-token px-2 py-1 text-left text-sm hover:preset-filled-surface-500"
																		type="button"
																		role="option"
																		aria-selected={getFixtureType(fixture.type)?.type === fixtureType.type}
																		onclick={() => selectFixtureType(fixtureIndex, fixtureType)}
																	>
																		{fixtureType.type}
																	</button>
																{/each}
															</div>
														{/if}
													</div>
													{#if errors[`fixtures.${fixtureIndex}.type`]}
														<small id={`fixture-${fixtureIndex}-type-error`} class="text-error-600-400"
															>{errors[`fixtures.${fixtureIndex}.type`]}</small
														>
													{/if}
												</div>
												<label class="label min-w-0"
													><span class="label-text">Start channel</span>
													<input
														class="input"
														type="number"
														min="1"
														max="128"
														value={fixture.startChannel}
														disabled={busy}
														aria-invalid={!!errors[`fixtures.${fixtureIndex}.startChannel`]}
														aria-describedby={`fixture-${fixtureIndex}-start-channel-error`}
														oninput={(event) =>
															updateFixture(fixtureIndex, { startChannel: getMaximumBoundedInputValue(event.currentTarget, 128) })}
													>
													{#if errors[`fixtures.${fixtureIndex}.startChannel`]}
														<small id={`fixture-${fixtureIndex}-start-channel-error`} class="text-error-600-400">
															{errors[`fixtures.${fixtureIndex}.startChannel`]}
														</small>
													{/if}</label
												>
											</div>
											{#if fixtureDiffersFromPreset(fixture)}
												<small class="mt-1 text-xs text-warning-600-400"
													>A predefined fixture with this type exists, but its channels differ.</small
												>
											{/if}
											<div class="mt-4 flex min-h-0 flex-1 flex-col gap-2">
												<div class="flex items-center gap-3">
													<span class="label-text text-surface-950-50">Channel mappings</span>
													<button
														class="btn btn-icon-sm shrink-0 !border-0 !bg-transparent !p-0 text-white hover:!bg-transparent hover:text-surface-300 active:!bg-transparent active:text-surface-400"
														type="button"
														disabled={busy || fixture.coloring.length >= 32}
														aria-label="Add channel"
														title="Add channel"
														onclick={() => void addChannel(fixtureIndex)}
													>
														<CirclePlusIcon />
													</button>
												</div>
												{#if errors[`fixtures.${fixtureIndex}.coloring`]}
													<small class="text-error-600-400">{errors[`fixtures.${fixtureIndex}.coloring`]}</small>
												{/if}
												<div bind:this={channelMappingList} class="min-h-0 flex-1 space-y-2 overflow-y-auto pr-1">
													{#each fixture.coloring as coloring, channelIndex}
														<div class="grid grid-cols-[3rem_minmax(0,1fr)_auto] items-start gap-3">
															<span class="pt-2 text-sm">#{fixture.startChannel + channelIndex}</span>
															<div class="flex min-w-0 flex-col gap-2 sm:flex-row">
																<div class="min-w-0 flex-1 space-y-1">
																	<select
																		class="select"
																		value={typeof coloring === 'number' ? 'custom' : coloring}
																		disabled={busy}
																		aria-invalid={!!errors[`fixtures.${fixtureIndex}.coloring.${channelIndex}`]}
																		aria-describedby={`fixture-${fixtureIndex}-channel-${channelIndex}-error`}
																		onchange={(event) => updateColoring(fixtureIndex, channelIndex, event.currentTarget.value)}
																	>
																		{#each DMX_SPECIAL_CHANNELS as channel}
																			<option value={channel}>{channel}</option>
																		{/each}
																		<option value="custom">Custom</option>
																	</select>
																	{#if errors[`fixtures.${fixtureIndex}.coloring.${channelIndex}`]}
																		<small id={`fixture-${fixtureIndex}-channel-${channelIndex}-error`} class="text-error-600-400">
																			{errors[`fixtures.${fixtureIndex}.coloring.${channelIndex}`]}
																		</small>
																	{/if}
																</div>
																{#if typeof coloring === 'number'}
																	<input
																		class="input !w-24"
																		type="number"
																		min="0"
																		max="255"
																		value={coloring}
																		disabled={busy}
																		aria-label="Custom mapping value"
																		aria-invalid={!!errors[`fixtures.${fixtureIndex}.coloring.${channelIndex}`]}
																		aria-describedby={`fixture-${fixtureIndex}-channel-${channelIndex}-error`}
																		oninput={(event) =>
																			updateCustomColoring(
																				fixtureIndex,
																				channelIndex,
																				getMaximumBoundedInputValue(event.currentTarget, 255)
																			)}
																	>
																{/if}
															</div>
															<button
																class="btn btn-icon-sm shrink-0 self-center !border-0 !bg-transparent !p-0 text-error-500 hover:!bg-transparent hover:text-error-400 active:!bg-transparent active:text-error-600"
																type="button"
																disabled={busy || fixture.coloring.length === 1}
																aria-label={`Remove channel ${fixture.startChannel + channelIndex}`}
																title="Remove channel"
																onclick={() => removeChannel(fixtureIndex, channelIndex)}
															>
																<CircleXIcon />
															</button>
														</div>
													{/each}
												</div>
											</div>
										</div>
									{/if}
								{/each}
								{#if selectedFixture === undefined}
									<p class="text-sm text-surface-600-400">Select a fixture to edit its properties.</p>
								{/if}
							</section>
						</div>
					</div>
				</fieldset>
			{/if}
		</div>
		<footer class="form-actions">
			<small class="mr-auto self-end">Firmware validates board-specific and reserved GPIO pins.</small>
			<button class="btn preset-filled-primary-500" type="button" disabled={busy || !isDirty} onclick={requestApply}>Save</button>
		</footer>
	{/if}
</form>
{#if confirming}
	<ConfirmationDialog
		title="Apply lighting configuration"
		summary={summary()}
		busy={saving}
		oncancel={() => (confirming = false)}
		onconfirm={() => void apply()}
	/>
{:else if discardConfirmation === 'leave'}
	<ConfirmationDialog
		title="Discard lighting configuration changes"
		summary="Your changes have not been applied. Discard them and leave this page?"
		oncancel={keepChanges}
		onconfirm={discardChanges}
	/>
{:else if discardConfirmation === 'reload'}
	<ConfirmationDialog
		title="Discard lighting configuration changes"
		summary="Your changes have not been applied. Discard them and reload the lighting configuration?"
		oncancel={keepChanges}
		onconfirm={discardChanges}
	/>
{/if}
