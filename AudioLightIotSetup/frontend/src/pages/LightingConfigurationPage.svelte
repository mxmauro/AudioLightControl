<script lang="ts">
import ChevronDownIcon from '@lucide/svelte/icons/chevron-down';
import CirclePlusIcon from '@lucide/svelte/icons/circle-plus';
import CircleXIcon from '@lucide/svelte/icons/circle-x';
import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw';
import type { Client, InputConfig, LightingConfig } from '@mxmauro/iot-comm.js';
import ConfirmationDialog from '../components/ConfirmationDialog.svelte';
import {
	type DeviceConfiguration,
	DMX_SPECIAL_CHANNELS,
	type DmxColoring,
	type DmxFixture,
	GPIO_MAX,
	GPIO_MIN,
	getLightingConfigurationValidationErrors,
	isDmxSpecialChannel,
	toEditableLightingConfiguration,
	toLightingConfiguration
} from '../lib/device-config';
import { type DmxFixturePreset, fixturesMatch, getDmxFixturePresets, normalizeFixtureType } from '../lib/dmx/fixture-manager';
import { errorMessage, type Notice, preventSubmit } from '../lib/ui';

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
	onconfigurationchange: (configuration: LightingConfig) => void;
	onrefresh: () => Promise<void>;
	onnotice: (notice: Notice) => void;
	registerLeaveGuard: (guard: () => Promise<boolean>) => () => void;
} = $props();
let draft = $state<DeviceConfiguration[]>([]);
let persistedDraft = $state<DeviceConfiguration[]>([]);
let fixtureTypes = $state<DmxFixturePreset[]>([]);
let expandedOutputIndex = $state<number | null>(null);
let selectedFixtureIndexes = $state<Record<number, number | null>>({});
let loading = $state(true);
let saving = $state(false);
let fixtureLoadError = $state('');
let errors = $state<Record<string, string>>({});
let confirming = $state(false);
let presetConfirmation = $state<{ outputIndex: number; fixtureIndex: number } | null>(null);
let discardConfirmation = $state<'leave' | 'reload' | null>(null);
let leaveRequest = $state<{ resolve: (allowed: boolean) => void } | null>(null);
let busy = $derived(loading || saving);
let isDirty = $derived(JSON.stringify(draft) !== JSON.stringify(persistedDraft));

// -----------------------------------------------------------------------------

$effect(() => {
	const nextDraft = toEditableLightingConfiguration(lightingConfiguration);
	draft = nextDraft;
	persistedDraft = toEditableLightingConfiguration(lightingConfiguration);
	expandedOutputIndex = nextDraft.length > 0 ? 0 : null;
	selectedFixtureIndexes = Object.fromEntries(
		nextDraft.map((output, outputIndex) => [outputIndex, output.output === 'dmx' && output.fixtures.length > 0 ? 0 : null])
	);
	errors = {};
});

$effect(() => {
	void loadFixtureTypes();
});

const loadFixtureTypes = async (): Promise<void> => {
	loading = true;
	fixtureLoadError = '';
	try {
		fixtureTypes = await getDmxFixturePresets();
	} catch (error) {
		fixtureLoadError = errorMessage(error);
		onnotice({ tone: 'error', message: `Unable to load DMX fixture presets: ${fixtureLoadError}` });
	} finally {
		loading = false;
	}
};

const defaultOutput = (type: DeviceConfiguration['output']): DeviceConfiguration => {
	if (type === 'dmx') {
		return { output: 'dmx', txGpio: 0, fixtures: [] };
	}
	if (type === 'ws281x') {
		return { output: 'ws281x', dataGpio: 0, ledCount: 1 };
	}
	return { output: 'rgb5050', redGpio: 0, greenGpio: 1, blueGpio: 2, activeLow: true };
};

const hasDmxOutput = (): boolean => {
	return draft.some((output) => output.output === 'dmx');
};

const addOutput = (type: DeviceConfiguration['output']): void => {
	if (draft.length >= 2 || (type === 'dmx' && hasDmxOutput())) {
		return;
	}
	errors = {};
	draft = [...draft, defaultOutput(type)];
	expandedOutputIndex = draft.length - 1;
};

const removeOutput = (outputIndex: number): void => {
	errors = {};
	draft = draft.filter((_, index) => index !== outputIndex);
	expandedOutputIndex = draft.length === 0 ? null : Math.min(outputIndex, draft.length - 1);
	selectedFixtureIndexes = Object.fromEntries(
		Object.entries(selectedFixtureIndexes)
			.filter(([key]) => Number(key) !== outputIndex)
			.map(([key, value]) => [Number(key) > outputIndex ? Number(key) - 1 : Number(key), value])
	);
};

const updateOutput = (outputIndex: number, update: Partial<DeviceConfiguration>): void => {
	errors = {};
	draft = draft.map((output, index) => (index === outputIndex ? ({ ...output, ...update } as DeviceConfiguration) : output));
};

const updateRgb = (outputIndex: number, field: 'redGpio' | 'greenGpio' | 'blueGpio', value: number): void => {
	const output = draft[outputIndex];
	if (output?.output === 'rgb5050') {
		updateOutput(outputIndex, { [field]: value });
	}
};

const updateDmxTx = (outputIndex: number, value: number): void => {
	const output = draft[outputIndex];
	if (output?.output === 'dmx') {
		updateOutput(outputIndex, { txGpio: value });
	}
};

const updateWs281x = (outputIndex: number, update: { dataGpio?: number; mirrorGpio?: number; ledCount?: number }): void => {
	const output = draft[outputIndex];
	if (output?.output === 'ws281x') {
		updateOutput(outputIndex, update);
	}
};

const setWs281xMirrorConnected = (outputIndex: number, connected: boolean): void => {
	updateWs281x(outputIndex, { mirrorGpio: connected ? GPIO_MIN : undefined });
};

const updateFixture = (outputIndex: number, fixtureIndex: number, update: Partial<DmxFixture>): void => {
	const output = draft[outputIndex];
	if (output?.output !== 'dmx') {
		return;
	}
	updateOutput(outputIndex, {
		fixtures: output.fixtures.map((fixture, index) => (index === fixtureIndex ? { ...fixture, ...update } : fixture))
	});
};

const getMaximumBoundedInputValue = (input: HTMLInputElement, maximum: number): number => {
	const value = Number(input.value);
	if (value > maximum) {
		input.value = String(maximum);
		return maximum;
	}
	return value;
};

const getFixtureType = (type: string): DmxFixturePreset | undefined => {
	const normalized = normalizeFixtureType(type);
	return fixtureTypes.find((fixtureType) => fixtureType.type.localeCompare(normalized, undefined, { sensitivity: 'accent' }) === 0);
};

const loadFixturePreset = (outputIndex: number, fixtureIndex: number): void => {
	const output = draft[outputIndex];
	if (output?.output !== 'dmx') {
		return;
	}
	const preset = getFixtureType(output.fixtures[fixtureIndex].type);
	if (preset) {
		updateFixture(outputIndex, fixtureIndex, { type: preset.type, coloring: [...preset.coloring] });
	}
};

const confirmLoadFixturePreset = (): void => {
	if (!presetConfirmation) {
		return;
	}
	const { outputIndex, fixtureIndex } = presetConfirmation;
	presetConfirmation = null;
	loadFixturePreset(outputIndex, fixtureIndex);
};

const presetConfirmationSummary = (): string => {
	if (!presetConfirmation) {
		return '';
	}
	const output = draft[presetConfirmation.outputIndex];
	const fixture = output?.output === 'dmx' ? output.fixtures[presetConfirmation.fixtureIndex] : undefined;
	return `Replace the channel mappings for ${fixture?.type || 'this fixture'} with its predefined preset?`;
};

const addFixture = (outputIndex: number): void => {
	const output = draft[outputIndex];
	if (output?.output !== 'dmx' || output.fixtures.length >= 16) {
		return;
	}
	const startChannel =
		output.fixtures.length === 0
			? 1
			: Math.min(128, Math.max(...output.fixtures.map((fixture) => fixture.startChannel + fixture.coloring.length)));
	const preset = fixtureTypes[0];
	updateOutput(outputIndex, {
		fixtures: [
			...output.fixtures,
			preset ? { type: preset.type, startChannel, coloring: [...preset.coloring] } : { type: '', startChannel, coloring: [0] }
		]
	});
	selectedFixtureIndexes = { ...selectedFixtureIndexes, [outputIndex]: output.fixtures.length };
};

const removeFixture = (outputIndex: number, fixtureIndex: number): void => {
	const output = draft[outputIndex];
	if (output?.output !== 'dmx') {
		return;
	}
	const fixtures = output.fixtures.filter((_, index) => index !== fixtureIndex);
	updateOutput(outputIndex, { fixtures });
	const selected = selectedFixtureIndexes[outputIndex];
	selectedFixtureIndexes = {
		...selectedFixtureIndexes,
		[outputIndex]:
			selected === null || selected === undefined ? null : fixtures.length === 0 ? null : Math.min(selected, fixtures.length - 1)
	};
};

const updateColoring = (outputIndex: number, fixtureIndex: number, channelIndex: number, value: string): void => {
	const output = draft[outputIndex];
	if (output?.output !== 'dmx') {
		return;
	}
	const coloring: DmxColoring = isDmxSpecialChannel(value) ? value : 0;
	updateFixture(outputIndex, fixtureIndex, {
		coloring: output.fixtures[fixtureIndex].coloring.map((color, index) => (index === channelIndex ? coloring : color))
	});
};

const updateCustomColoring = (outputIndex: number, fixtureIndex: number, channelIndex: number, value: number): void => {
	const output = draft[outputIndex];
	if (output?.output !== 'dmx') {
		return;
	}
	updateFixture(outputIndex, fixtureIndex, {
		coloring: output.fixtures[fixtureIndex].coloring.map((color, index) => (index === channelIndex ? value : color))
	});
};

const addChannel = (outputIndex: number, fixtureIndex: number): void => {
	const output = draft[outputIndex];
	if (output?.output === 'dmx' && output.fixtures[fixtureIndex].coloring.length < 32) {
		updateFixture(outputIndex, fixtureIndex, { coloring: [...output.fixtures[fixtureIndex].coloring, 0] });
	}
};

const removeChannel = (outputIndex: number, fixtureIndex: number, channelIndex: number): void => {
	const output = draft[outputIndex];
	if (output?.output === 'dmx' && output.fixtures[fixtureIndex].coloring.length > 1) {
		updateFixture(outputIndex, fixtureIndex, {
			coloring: output.fixtures[fixtureIndex].coloring.filter((_, index) => index !== channelIndex)
		});
	}
};

const requestApply = (): void => {
	errors = getLightingConfigurationValidationErrors(draft, inputConfiguration);
	const invalidOutput = draft.findIndex((_, index) => Object.keys(errors).some((field) => field.startsWith(`outputs.${index}.`)));
	if (invalidOutput >= 0) {
		expandedOutputIndex = invalidOutput;
		const output = draft[invalidOutput];
		if (output.output === 'dmx') {
			const invalidFixture = output.fixtures.findIndex((_, fixtureIndex) =>
				Object.keys(errors).some((field) => field.startsWith(`outputs.${invalidOutput}.fixtures.${fixtureIndex}.`))
			);
			if (invalidFixture >= 0) {
				selectedFixtureIndexes = { ...selectedFixtureIndexes, [invalidOutput]: invalidFixture };
			}
		}
	}
	if (Object.keys(errors).length === 0) {
		confirming = true;
	}
};

const apply = async (): Promise<void> => {
	saving = true;
	try {
		const configuration = toLightingConfiguration(draft);
		await client.setLightingConfigCommand(configuration);
		onconfigurationchange(configuration);
		confirming = false;
		onnotice({ tone: 'success', message: 'Lighting configuration saved. Reboot the device to activate it.' });
	} catch (error) {
		confirming = false;
		onnotice({ tone: 'error', message: errorMessage(error) });
	} finally {
		saving = false;
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
	loading = true;
	try {
		await Promise.all([onrefresh(), loadFixtureTypes()]);
	} catch (error) {
		onnotice({ tone: 'error', message: errorMessage(error) });
	} finally {
		loading = false;
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

const outputTitle = (output: DeviceConfiguration, outputIndex: number): string => {
	const name = output.output === 'dmx' ? 'DMX' : output.output === 'ws281x' ? 'WS281x' : 'RGB5050';
	return `Controller ${outputIndex + 1}: ${name}`;
};

const fixtureDiffersFromPreset = (fixture: DmxFixture): boolean => {
	const preset = getFixtureType(fixture.type);
	return preset !== undefined && !fixturesMatch(preset, fixture.coloring);
};

const summary = (): string => {
	if (draft.length === 0) {
		return 'Remove all lighting outputs.';
	}
	return draft
		.map((output, index) =>
			output.output === 'dmx'
				? `Controller ${index + 1}: DMX on GPIO ${output.txGpio} with ${output.fixtures.length} fixture(s)`
				: output.output === 'ws281x'
					? `Controller ${index + 1}: WS281x on GPIO ${output.dataGpio} with ${output.ledCount} LED(s)${
							output.mirrorGpio === undefined ? '' : ` and mirror GPIO ${output.mirrorGpio}`
						}`
					: `Controller ${index + 1}: RGB5050 on GPIOs ${output.redGpio}, ${output.greenGpio}, and ${output.blueGpio}`
		)
		.join('. ');
};

$effect(() => {
	return registerLeaveGuard(requestLeave);
});
</script>

<form class="card bg-surface-100-900 mx-auto flex h-full min-h-0 w-full max-w-3xl flex-col gap-4 p-4" onsubmit={preventSubmit}>
	<header class="flex items-start justify-between gap-3">
		<h1 class="text-base font-semibold text-primary-600-400">Lighting configuration</h1>
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
	{#if loading}
		<div class="flex flex-1 items-center justify-center gap-3 py-12" role="status">
			<RefreshCwIcon class="spin-360" />
			<span>Loading lighting configuration…</span>
		</div>
	{:else}
		<div class="min-h-0 flex-1 space-y-4 overflow-y-auto pr-1">
			{#each draft as output, outputIndex}
				<section class="rounded-container-token border border-surface-300-700 p-2">
					<div class="flex items-center gap-2">
						<button
							class="flex min-w-0 flex-1 items-center gap-2 rounded-[var(--radius-base)] px-2 py-2 text-left hover:preset-filled-surface-500"
							type="button"
							aria-expanded={expandedOutputIndex === outputIndex}
							onclick={() => (expandedOutputIndex = expandedOutputIndex === outputIndex ? null : outputIndex)}
						>
							<ChevronDownIcon class={expandedOutputIndex === outputIndex ? undefined : '-rotate-90'} />
							<span class="font-medium">{outputTitle(output, outputIndex)}</span>
						</button>
						<button
							class="btn btn-icon-sm !border-0 !bg-transparent !p-0 text-error-500 hover:!bg-transparent hover:text-error-400"
							type="button"
							disabled={busy}
							aria-label={`Remove controller ${outputIndex + 1}`}
							title="Remove controller"
							onclick={() => removeOutput(outputIndex)}
						>
							<CircleXIcon />
						</button>
					</div>
					{#if expandedOutputIndex === outputIndex}
						<div class="space-y-4 pt-4">
							{#if output.output === 'rgb5050'}
								<div class="grid grid-cols-3 gap-4">
									{#each [
										['redGpio', 'Red GPIO'],
										['greenGpio', 'Green GPIO'],
										['blueGpio', 'Blue GPIO']
									] as field}
										<label class="label">
											<span class="label-text">{field[1]}</span>
											<input
												class="input"
												type="number"
												min={GPIO_MIN}
												max={GPIO_MAX}
												value={output[field[0] as 'redGpio' | 'greenGpio' | 'blueGpio']}
												disabled={busy}
												aria-invalid={!!errors[`outputs.${outputIndex}.${field[0]}`]}
												oninput={(event) =>
													updateRgb(
														outputIndex,
														field[0] as 'redGpio' | 'greenGpio' | 'blueGpio',
														getMaximumBoundedInputValue(event.currentTarget, GPIO_MAX)
													)}
											>
											{#if errors[`outputs.${outputIndex}.${field[0]}`]}
												<small class="text-error-600-400">{errors[`outputs.${outputIndex}.${field[0]}`]}</small>
											{/if}
										</label>
									{/each}
								</div>
								<label class="flex items-center gap-2">
									<input
										class="checkbox"
										type="checkbox"
										checked={output.activeLow}
										disabled={busy}
										onchange={(event) => updateOutput(outputIndex, { activeLow: event.currentTarget.checked })}
									>
									Active low output
								</label>
							{:else if output.output === 'dmx'}
								<div class="space-y-4">
									{#if fixtureLoadError}
										<p class="rounded-container-token border border-error-500 p-3 text-error-600-400" role="alert">
											Unable to load DMX fixture presets.
										</p>
									{/if}
									<label class="label block w-48">
										<span class="label-text">DMX TX GPIO</span>
										<input
											class="input"
											type="number"
											min={GPIO_MIN}
											max={GPIO_MAX}
											value={output.txGpio}
											disabled={busy}
											aria-invalid={!!errors[`outputs.${outputIndex}.txGpio`]}
											oninput={(event) => updateDmxTx(outputIndex, getMaximumBoundedInputValue(event.currentTarget, GPIO_MAX))}
										>
										{#if errors[`outputs.${outputIndex}.txGpio`]}
											<small class="text-error-600-400">{errors[`outputs.${outputIndex}.txGpio`]}</small>
										{/if}
									</label>
									<div class="flex items-center gap-2">
										<span class="label-text">Fixtures</span>
										<button
											class="btn btn-icon-sm !border-0 !bg-transparent !p-0"
											type="button"
											disabled={busy || output.fixtures.length >= 16}
											aria-label="Add fixture"
											title="Add fixture"
											onclick={() => addFixture(outputIndex)}
										>
											<CirclePlusIcon />
										</button>
									</div>
									{#each output.fixtures as fixture, fixtureIndex}
										<section class="space-y-3 rounded-container-token border border-surface-300-700 p-2">
											<div class="flex items-center gap-2">
												<button
													class="flex min-w-0 flex-1 items-center gap-2 rounded-[var(--radius-base)] px-2 py-2 text-left font-medium hover:preset-filled-surface-500"
													type="button"
													aria-expanded={selectedFixtureIndexes[outputIndex] === fixtureIndex}
													onclick={() =>
														(selectedFixtureIndexes = {
															...selectedFixtureIndexes,
															[outputIndex]: selectedFixtureIndexes[outputIndex] === fixtureIndex ? null : fixtureIndex
														})}
												>
													<ChevronDownIcon class={selectedFixtureIndexes[outputIndex] === fixtureIndex ? undefined : '-rotate-90'} />
													<span class="min-w-0 truncate">Fixture {fixtureIndex + 1}: {fixture.type || 'Custom fixture'}</span>
												</button>
												<button
													class="btn btn-icon-sm !border-0 !bg-transparent !p-0 text-error-500 hover:!bg-transparent hover:text-error-400"
													type="button"
													disabled={busy}
													aria-label={`Remove fixture ${fixtureIndex + 1}`}
													title="Remove fixture"
													onclick={() => removeFixture(outputIndex, fixtureIndex)}
												>
													<CircleXIcon />
												</button>
											</div>
											{#if selectedFixtureIndexes[outputIndex] === fixtureIndex}
												<div class="grid gap-3 md:grid-cols-[minmax(0,1fr)_7rem]">
													<label class="label">
														<span class="label-text">Fixture type</span>
														<input
															class="input"
															list={`fixture-types-${outputIndex}`}
															value={fixture.type}
															disabled={busy}
															aria-invalid={!!errors[`outputs.${outputIndex}.fixtures.${fixtureIndex}.type`]}
															oninput={(event) => updateFixture(outputIndex, fixtureIndex, { type: event.currentTarget.value })}
														>
														<datalist id={`fixture-types-${outputIndex}`}>
															{#each fixtureTypes as fixtureType}
																<option value={fixtureType.type}></option>
															{/each}
														</datalist>
														{#if errors[`outputs.${outputIndex}.fixtures.${fixtureIndex}.type`]}
															<small class="text-error-600-400">{errors[`outputs.${outputIndex}.fixtures.${fixtureIndex}.type`]}</small>
														{/if}
													</label>
													<label class="label">
														<span class="label-text">Start channel</span>
														<input
															class="input"
															type="number"
															min="1"
															max="128"
															value={fixture.startChannel}
															disabled={busy}
															aria-invalid={!!errors[`outputs.${outputIndex}.fixtures.${fixtureIndex}.startChannel`]}
															oninput={(event) =>
																updateFixture(outputIndex, fixtureIndex, {
																	startChannel: getMaximumBoundedInputValue(event.currentTarget, 128)
																})}
														>
														{#if errors[`outputs.${outputIndex}.fixtures.${fixtureIndex}.startChannel`]}
															<small class="text-error-600-400"
																>{errors[`outputs.${outputIndex}.fixtures.${fixtureIndex}.startChannel`]}</small
															>
														{/if}
													</label>
												</div>
												<div class="space-y-2">
													<div class="flex items-center gap-2">
														<span class="label-text">Channel mappings</span>
														<button
															class="btn btn-icon-sm !border-0 !bg-transparent !p-0"
															type="button"
															disabled={busy || fixture.coloring.length >= 32}
															aria-label="Add channel"
															title="Add channel"
															onclick={() => addChannel(outputIndex, fixtureIndex)}
														>
															<CirclePlusIcon />
														</button>
														<span class="ml-auto" aria-hidden="true"></span>
														{#if fixtureDiffersFromPreset(fixture)}
															<small class="text-warning-600-400">Preset channels differ from this fixture.</small>
														{/if}
														<button
															class="btn btn-sm preset-filled-surface-500 text-xs"
															type="button"
															disabled={busy || !getFixtureType(fixture.type)}
															onclick={() => (presetConfirmation = { outputIndex, fixtureIndex })}
														>
															Load preset channels
														</button>
													</div>
													{#if errors[`outputs.${outputIndex}.fixtures.${fixtureIndex}.coloring`]}
														<small class="text-error-600-400">{errors[`outputs.${outputIndex}.fixtures.${fixtureIndex}.coloring`]}</small>
													{/if}
													{#each fixture.coloring as coloring, channelIndex}
														<div class="grid grid-cols-[3rem_minmax(0,1fr)_auto] items-start gap-3">
															<span class="pt-2 text-sm">#{fixture.startChannel + channelIndex}</span>
															<div class="flex min-w-0 gap-2">
																<select
																	class="select min-w-0 flex-1"
																	value={typeof coloring === 'number' ? 'custom' : coloring}
																	disabled={busy}
																	onchange={(event) => updateColoring(outputIndex, fixtureIndex, channelIndex, event.currentTarget.value)}
																>
																	{#each DMX_SPECIAL_CHANNELS as channel}
																		<option value={channel}>{channel}</option>
																	{/each}
																	<option value="custom">Custom</option>
																</select>
																{#if typeof coloring === 'number'}
																	<input
																		class="input !w-20"
																		type="number"
																		min="0"
																		max="255"
																		value={coloring}
																		disabled={busy}
																		oninput={(event) =>
																			updateCustomColoring(
																				outputIndex,
																				fixtureIndex,
																				channelIndex,
																				getMaximumBoundedInputValue(event.currentTarget, 255)
																			)}
																	>
																{/if}
															</div>
															<button
																class="btn btn-icon-sm !border-0 !bg-transparent !p-0 text-error-500 hover:!bg-transparent hover:text-error-400"
																type="button"
																disabled={busy || fixture.coloring.length === 1}
																aria-label={`Remove channel ${fixture.startChannel + channelIndex}`}
																title="Remove channel"
																onclick={() => removeChannel(outputIndex, fixtureIndex, channelIndex)}
															>
																<CircleXIcon />
															</button>
														</div>
														{#if errors[`outputs.${outputIndex}.fixtures.${fixtureIndex}.coloring.${channelIndex}`]}
															<small class="ml-12 block text-error-600-400"
																>{errors[`outputs.${outputIndex}.fixtures.${fixtureIndex}.coloring.${channelIndex}`]}</small
															>
														{/if}
													{/each}
												</div>
											{/if}
										</section>
									{/each}
								</div>
							{:else}
								<div class="grid gap-4 sm:grid-cols-2">
									<label class="label">
										<span class="label-text">Data GPIO</span>
										<input
											class="input"
											type="number"
											min={GPIO_MIN}
											max={GPIO_MAX}
											value={output.dataGpio}
											disabled={busy}
											aria-invalid={!!errors[`outputs.${outputIndex}.dataGpio`]}
											oninput={(event) =>
												updateWs281x(outputIndex, { dataGpio: getMaximumBoundedInputValue(event.currentTarget, GPIO_MAX) })}
										>
										{#if errors[`outputs.${outputIndex}.dataGpio`]}
											<small class="text-error-600-400">{errors[`outputs.${outputIndex}.dataGpio`]}</small>
										{/if}
									</label>
									<label class="label">
										<span class="label-text">LED count</span>
										<input
											class="input"
											type="number"
											min="1"
											max="300"
											value={output.ledCount}
											disabled={busy}
											aria-invalid={!!errors[`outputs.${outputIndex}.ledCount`]}
											oninput={(event) => updateWs281x(outputIndex, { ledCount: getMaximumBoundedInputValue(event.currentTarget, 300) })}
										>
										{#if errors[`outputs.${outputIndex}.ledCount`]}
											<small class="text-error-600-400">{errors[`outputs.${outputIndex}.ledCount`]}</small>
										{/if}
									</label>
									<div class="space-y-2">
										<label class="flex items-center gap-2">
											<input
												class="checkbox"
												type="checkbox"
												checked={output.mirrorGpio !== undefined}
												disabled={busy}
												onchange={(event) => setWs281xMirrorConnected(outputIndex, event.currentTarget.checked)}
											>
											Mirror output connected
										</label>
										<small class="block text-surface-600-400">Leave unchecked when no matching strip is wired.</small>
									</div>
									<label class="label">
										<span class="label-text">Mirror GPIO</span>
										<input
											class="input"
											type="number"
											min={GPIO_MIN}
											max={GPIO_MAX}
											value={output.mirrorGpio ?? ''}
											disabled={busy || output.mirrorGpio === undefined}
											aria-invalid={!!errors[`outputs.${outputIndex}.mirrorGpio`]}
											oninput={(event) =>
												updateWs281x(outputIndex, { mirrorGpio: getMaximumBoundedInputValue(event.currentTarget, GPIO_MAX) })}
										>
										{#if errors[`outputs.${outputIndex}.mirrorGpio`]}
											<small class="text-error-600-400">{errors[`outputs.${outputIndex}.mirrorGpio`]}</small>
										{/if}
									</label>
								</div>
							{/if}
						</div>
					{/if}
				</section>
			{/each}
			{#if draft.length === 0}
				<p class="text-sm text-surface-600-400">No lighting outputs are configured.</p>
			{/if}
			{#if draft.length < 2}
				<div class="flex items-center gap-2">
					<span class="label-text">Add controller</span>
					<button class="btn preset-filled-surface-500" type="button" disabled={busy} onclick={() => addOutput('rgb5050')}>RGB5050</button>
					<button class="btn preset-filled-surface-500" type="button" disabled={busy || hasDmxOutput()} onclick={() => addOutput('dmx')}>
						DMX
					</button>
					<button class="btn preset-filled-surface-500" type="button" disabled={busy} onclick={() => addOutput('ws281x')}>WS281x</button>
				</div>
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
		title="Save lighting configuration"
		summary={summary()}
		busy={saving}
		busyLabel="Saving lighting configuration…"
		oncancel={() => (confirming = false)}
		onconfirm={() => void apply()}
	/>
{:else if presetConfirmation}
	<ConfirmationDialog
		title="Load preset channels"
		summary={presetConfirmationSummary()}
		oncancel={() => (presetConfirmation = null)}
		onconfirm={confirmLoadFixturePreset}
	/>
{:else if discardConfirmation === 'leave'}
	<ConfirmationDialog
		title="Discard lighting configuration changes"
		summary="Your changes have not been saved. Discard them and leave this page?"
		oncancel={keepChanges}
		onconfirm={discardChanges}
	/>
{:else if discardConfirmation === 'reload'}
	<ConfirmationDialog
		title="Discard lighting configuration changes"
		summary="Your changes have not been saved. Discard them and reload the configuration?"
		oncancel={keepChanges}
		onconfirm={discardChanges}
	/>
{/if}
