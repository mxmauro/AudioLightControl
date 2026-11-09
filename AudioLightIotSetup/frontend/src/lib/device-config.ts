import type {
	InputConfig,
	LightingConfig,
	DmxFixture as ProtocolDmxFixture,
	Rgb5050LightingConfig,
	Ws281xLightingConfig
} from '@mxmauro/iot-comm.js';

const DMX_UNIVERSE_SIZE = 128;
const DMX_MAX_FIXTURES = 16;
const DMX_MAX_FIXTURE_TYPE_BYTES = 32;
const DMX_MAX_FIXTURE_CHANNELS = 32;
const WS281X_MAX_LEDS = 300;

const DMX_SPECIAL_CHANNEL_ENTRIES = [
	['Dimmer', 0x0100],
	['Dimmer.Fine', 0x0101],
	['Color.Red', 0x0102],
	['Color.Red16', 0x0103],
	['Color.Green', 0x0104],
	['Color.Green16', 0x0105],
	['Color.Blue', 0x0106],
	['Color.Blue16', 0x0107],
	['Color.White', 0x0108],
	['Color.White16', 0x0109],
	['Color.UV', 0x010a],
	['Color.UV16', 0x010b],
	['Pan', 0x010c],
	['Pan.Fine', 0x010d],
	['Tilt', 0x010e],
	['Tilt.Fine', 0x010f],
	['Strobe', 0x0110]
] as const;

// -----------------------------------------------------------------------------

export type DmxSpecialChannel = (typeof DMX_SPECIAL_CHANNEL_ENTRIES)[number][0];

export type DmxColoring = number | DmxSpecialChannel;

export type DmxFixture = Omit<ProtocolDmxFixture, 'coloring'> & {
	coloring: DmxColoring[];
};

export type DeviceConfiguration =
	| { output: 'rgb5050'; redGpio: number; greenGpio: number; blueGpio: number; activeLow: boolean }
	| { output: 'dmx'; txGpio: number; fixtures: DmxFixture[] }
	| { output: 'ws281x'; dataGpio: number; mirrorGpio?: number; ledCount: number };

type GpioAssignment = [field: string, gpio: number | undefined, optional: boolean];

class DeviceConfigurationValidationError extends Error {
	public readonly field: string;

	public constructor(field: string, message: string) {
		super(message);
		this.field = field;
	}
}

// -----------------------------------------------------------------------------

export const GPIO_MIN = 0;
export const GPIO_MAX = 127;
export const DMX_SPECIAL_CHANNELS: readonly DmxSpecialChannel[] = DMX_SPECIAL_CHANNEL_ENTRIES.map(([channel]) => channel);

// -----------------------------------------------------------------------------

export const isDmxSpecialChannel = (value: unknown): value is DmxSpecialChannel => {
	return typeof value === 'string' && DMX_SPECIAL_CHANNELS.includes(value as DmxSpecialChannel);
};

export const toEditableLightingConfiguration = (configuration: LightingConfig): DeviceConfiguration[] => {
	return configuration.map((output) => {
		if (output.type === 'rgb5050') {
			return { ...output, output: 'rgb5050' };
		}
		if (output.type === 'ws281x') {
			return { ...output, output: 'ws281x' };
		}
		return {
			output: 'dmx',
			txGpio: output.txGpio,
			fixtures: output.fixtures.map((fixture) => ({
				...fixture,
				coloring: fixture.coloring.map(decodeColoring)
			}))
		};
	});
};

export const toLightingConfiguration = (configuration: DeviceConfiguration[]): LightingConfig => {
	validateLightingConfiguration(configuration);
	return configuration.map((output) => {
		if (output.output === 'rgb5050') {
			return {
				type: 'rgb5050',
				redGpio: output.redGpio,
				greenGpio: output.greenGpio,
				blueGpio: output.blueGpio,
				activeLow: output.activeLow
			} satisfies Rgb5050LightingConfig;
		}
		if (output.output === 'ws281x') {
			return {
				type: 'ws281x',
				dataGpio: output.dataGpio,
				mirrorGpio: output.mirrorGpio,
				ledCount: output.ledCount
			} satisfies Ws281xLightingConfig;
		}
		return {
			type: 'dmx',
			txGpio: output.txGpio,
			fixtures: output.fixtures.map((fixture) => ({ ...fixture, coloring: fixture.coloring.map((coloring) => encodeColoring(coloring)) }))
		};
	});
};

export const getInputConfigurationValidationErrors = (configuration: InputConfig, lighting: LightingConfig): Record<string, string> => {
	const errors: Record<string, string> = {};
	const usedGpios = getLightingGpioOwners(lighting);
	const reportedConflicts = new Set<number>();
	for (const [field, label, gpio] of [
		['resetGpio', 'Reset button GPIO', configuration.resetGpio],
		['irGpio', 'IR receiver GPIO', configuration.irGpio]
	] as const) {
		if (!isOptionalGpio(gpio)) {
			addError(errors, field, `${label} must be between ${GPIO_MIN} and ${GPIO_MAX}, or not connected.`);
			continue;
		}
		if (gpio !== undefined) {
			const owner = usedGpios.get(gpio);
			if (owner) {
				if (!reportedConflicts.has(gpio)) {
					addError(errors, field, `GPIO ${gpio} is already assigned to ${owner}.`);
					reportedConflicts.add(gpio);
				}
			} else {
				usedGpios.set(gpio, label);
			}
		}
	}
	return errors;
};

export const getLightingConfigurationValidationErrors = (
	configuration: DeviceConfiguration[],
	input: InputConfig
): Record<string, string> => {
	const errors: Record<string, string> = {};
	const usedGpios = new Map<number, string>();
	const reportedConflicts = new Set<number>();
	if (configuration.length > 2) {
		addError(errors, 'configuration', 'A device supports at most two lighting controllers.');
	}
	if (configuration.filter((output) => output.output === 'dmx').length > 1) {
		addError(errors, 'configuration', 'A device supports at most one DMX controller.');
	}

	for (const [label, gpio] of [
		['the reset-button input', input.resetGpio],
		['the IR-receiver input', input.irGpio]
	] as const) {
		if (isValidGpio(gpio) && !usedGpios.has(gpio)) {
			usedGpios.set(gpio, label);
		}
	}

	for (const [outputIndex, output] of configuration.entries()) {
		for (const [field, gpio, optional] of outputGpios(output)) {
			const errorField = `outputs.${outputIndex}.${field}`;
			if (optional && gpio === undefined) {
				continue;
			}
			if (!isValidGpio(gpio)) {
				addError(errors, errorField, `GPIO pin must be between ${GPIO_MIN} and ${GPIO_MAX}${optional ? ', or be omitted' : ''}.`);
				continue;
			}
			const owner = usedGpios.get(gpio);
			if (owner) {
				if (!reportedConflicts.has(gpio)) {
					addError(errors, errorField, `GPIO ${gpio} is already assigned to ${owner}.`);
					reportedConflicts.add(gpio);
				}
			} else {
				usedGpios.set(gpio, `controller ${outputIndex + 1}`);
			}
		}
		if (output.output === 'dmx') {
			collectDmxValidationErrors(output, outputIndex, errors);
		} else if (output.output === 'ws281x' && !isValidWs281xLedCount(output.ledCount)) {
			addError(errors, `outputs.${outputIndex}.ledCount`, `WS281x LED count must be between 1 and ${WS281X_MAX_LEDS}.`);
		}
	}

	return errors;
};

// -----------------------------------------------------------------------------

const validateLightingConfiguration = (configuration: DeviceConfiguration[]): void => {
	if (configuration.length > 2) {
		throw new DeviceConfigurationValidationError('configuration', 'A device supports at most two lighting outputs.');
	}
	if (configuration.filter((output) => output.output === 'dmx').length > 1) {
		throw new DeviceConfigurationValidationError('configuration', 'A device supports at most one DMX output.');
	}
	const usedGpios = new Map<number, string>();
	for (const [outputIndex, output] of configuration.entries()) {
		validateOutput(output, outputIndex, usedGpios);
	}
};

const validateOutput = (output: DeviceConfiguration, outputIndex: number, usedGpios: Map<number, string>): void => {
	for (const [field, gpio, optional] of outputGpios(output)) {
		if (optional && gpio === undefined) {
			continue;
		}
		const validatedGpio = requireGpio(gpio, `outputs.${outputIndex}.${field}`);
		const owner = usedGpios.get(validatedGpio);
		if (owner) {
			throw new DeviceConfigurationValidationError(`outputs.${outputIndex}.${field}`, `GPIO ${validatedGpio} is already used by ${owner}.`);
		}
		usedGpios.set(validatedGpio, `output ${outputIndex + 1}`);
	}
	if (output.output === 'ws281x') {
		if (!Number.isInteger(output.ledCount) || output.ledCount < 1 || output.ledCount > WS281X_MAX_LEDS) {
			throw new DeviceConfigurationValidationError(
				`outputs.${outputIndex}.ledCount`,
				`WS281x LED count must be between 1 and ${WS281X_MAX_LEDS}.`
			);
		}
		return;
	}
	if (output.output !== 'dmx') {
		return;
	}
	if (output.fixtures.length > DMX_MAX_FIXTURES) {
		throw new DeviceConfigurationValidationError(`outputs.${outputIndex}.fixtures`, `DMX supports at most ${DMX_MAX_FIXTURES} fixtures.`);
	}
	const usedChannels = new Set<number>();
	const encoder = new TextEncoder();
	for (const [fixtureIndex, fixture] of output.fixtures.entries()) {
		const prefix = `outputs.${outputIndex}.fixtures.${fixtureIndex}`;
		const name = `Fixture ${fixtureIndex + 1}`;
		const typeLength = encoder.encode(fixture.type).byteLength;
		if (typeLength < 1 || typeLength > DMX_MAX_FIXTURE_TYPE_BYTES || fixture.type.includes('\0')) {
			throw new DeviceConfigurationValidationError(
				`${prefix}.type`,
				`${name} needs a type name between 1 and ${DMX_MAX_FIXTURE_TYPE_BYTES} bytes.`
			);
		}
		if (!Number.isInteger(fixture.startChannel) || fixture.startChannel < 1 || fixture.startChannel > DMX_UNIVERSE_SIZE) {
			throw new DeviceConfigurationValidationError(
				`${prefix}.startChannel`,
				`${name} start channel must be between 1 and ${DMX_UNIVERSE_SIZE}.`
			);
		}
		if (fixture.coloring.length < 1 || fixture.coloring.length > DMX_MAX_FIXTURE_CHANNELS) {
			throw new DeviceConfigurationValidationError(`${prefix}.coloring`, `${name} must have 1 to ${DMX_MAX_FIXTURE_CHANNELS} channels.`);
		}
		if (fixture.startChannel + fixture.coloring.length - 1 > DMX_UNIVERSE_SIZE) {
			throw new DeviceConfigurationValidationError(`${prefix}.startChannel`, `${name} extends beyond DMX channel ${DMX_UNIVERSE_SIZE}.`);
		}
		for (const [channel, coloring] of fixture.coloring.entries()) {
			encodeColoring(coloring, `${prefix}.coloring.${channel}`);
			const channelNumber = fixture.startChannel + channel;
			if (usedChannels.has(channelNumber)) {
				throw new DeviceConfigurationValidationError(
					`${prefix}.coloring.${channel}`,
					`DMX channel ${channelNumber} is used by more than one fixture.`
				);
			}
			usedChannels.add(channelNumber);
		}
	}
};

const collectDmxValidationErrors = (
	output: Extract<DeviceConfiguration, { output: 'dmx' }>,
	outputIndex: number,
	errors: Record<string, string>
): void => {
	const prefix = `outputs.${outputIndex}`;
	if (output.fixtures.length > DMX_MAX_FIXTURES) {
		addError(errors, `${prefix}.fixtures`, `DMX supports at most ${DMX_MAX_FIXTURES} fixtures.`);
	}
	const channelAssignments = new Map<number, string[]>();
	const encoder = new TextEncoder();
	for (const [fixtureIndex, fixture] of output.fixtures.entries()) {
		const fixturePrefix = `${prefix}.fixtures.${fixtureIndex}`;
		const name = `Fixture ${fixtureIndex + 1}`;
		const typeLength = encoder.encode(fixture.type).byteLength;
		if (typeLength < 1 || typeLength > DMX_MAX_FIXTURE_TYPE_BYTES || fixture.type.includes('\0')) {
			addError(errors, `${fixturePrefix}.type`, `${name} needs a type name between 1 and ${DMX_MAX_FIXTURE_TYPE_BYTES} bytes.`);
		}
		const hasValidStart = Number.isInteger(fixture.startChannel) && fixture.startChannel >= 1 && fixture.startChannel <= DMX_UNIVERSE_SIZE;
		if (!hasValidStart) {
			addError(errors, `${fixturePrefix}.startChannel`, `${name} start channel must be between 1 and ${DMX_UNIVERSE_SIZE}.`);
		}
		if (fixture.coloring.length < 1 || fixture.coloring.length > DMX_MAX_FIXTURE_CHANNELS) {
			addError(errors, `${fixturePrefix}.coloring`, `${name} must have 1 to ${DMX_MAX_FIXTURE_CHANNELS} channels.`);
		}
		if (hasValidStart && fixture.startChannel + fixture.coloring.length - 1 > DMX_UNIVERSE_SIZE) {
			addError(errors, `${fixturePrefix}.startChannel`, `${name} extends beyond DMX channel ${DMX_UNIVERSE_SIZE}.`);
		}
		for (const [channelIndex, coloring] of fixture.coloring.entries()) {
			const channelField = `${fixturePrefix}.coloring.${channelIndex}`;
			if (!isValidDmxColoring(coloring)) {
				addError(errors, channelField, 'DMX channels must be fixed values from 0 to 255 or supported dynamic channels.');
			}
			if (hasValidStart && fixture.startChannel + channelIndex <= DMX_UNIVERSE_SIZE) {
				const channelNumber = fixture.startChannel + channelIndex;
				const assignments = channelAssignments.get(channelNumber) ?? [];
				assignments.push(channelField);
				channelAssignments.set(channelNumber, assignments);
			}
		}
	}
	for (const [channelNumber, assignments] of channelAssignments.entries()) {
		if (assignments.length > 1) {
			for (const field of assignments) {
				addError(errors, field, `DMX channel ${channelNumber} is used by more than one fixture.`);
			}
		}
	}
};

const outputGpios = (output: DeviceConfiguration): GpioAssignment[] => {
	if (output.output === 'rgb5050') {
		return [
			['redGpio', output.redGpio, false],
			['greenGpio', output.greenGpio, false],
			['blueGpio', output.blueGpio, false]
		];
	}
	if (output.output === 'ws281x') {
		return [
			['dataGpio', output.dataGpio, false],
			['mirrorGpio', output.mirrorGpio, true]
		];
	}
	return [['txGpio', output.txGpio, false]];
};

const getLightingGpioOwners = (configuration: LightingConfig): Map<number, string> => {
	const owners = new Map<number, string>();
	for (const [controllerIndex, controller] of configuration.entries()) {
		const gpios =
			controller.type === 'rgb5050'
				? [controller.redGpio, controller.greenGpio, controller.blueGpio]
				: controller.type === 'ws281x'
					? [controller.dataGpio, controller.mirrorGpio]
					: [controller.txGpio];
		for (const gpio of gpios) {
			if (!isValidGpio(gpio) || owners.has(gpio)) {
				continue;
			}
			owners.set(gpio, `controller ${controllerIndex + 1}`);
		}
	}
	return owners;
};

const isOptionalGpio = (value: unknown): boolean => {
	return value === undefined || isValidGpio(value);
};

const isValidGpio = (value: unknown): value is number => {
	return typeof value === 'number' && Number.isInteger(value) && value >= GPIO_MIN && value <= GPIO_MAX;
};

const isValidWs281xLedCount = (value: unknown): value is number => {
	return typeof value === 'number' && Number.isInteger(value) && value >= 1 && value <= WS281X_MAX_LEDS;
};

const isValidDmxColoring = (value: DmxColoring): boolean => {
	return (typeof value === 'number' && Number.isInteger(value) && value >= 0 && value <= 0xff) || isDmxSpecialChannel(value);
};

const addError = (errors: Record<string, string>, field: string, message: string): void => {
	if (!errors[field]) {
		errors[field] = message;
		return;
	}
	if (!errors[field].includes(message)) {
		errors[field] = `${errors[field]} ${message}`;
	}
};

const requireGpio = (value: unknown, field: string): number => {
	if (!isValidGpio(value)) {
		throw new DeviceConfigurationValidationError(field, `GPIO pin must be between ${GPIO_MIN} and ${GPIO_MAX}.`);
	}
	return value;
};

const decodeColoring = (value: number): DmxColoring => {
	if (value <= 0xff) {
		return value;
	}
	const channel = DMX_SPECIAL_CHANNEL_ENTRIES.find(([, channelValue]) => channelValue === value);
	if (channel) {
		return channel[0];
	}
	throw new Error('Configuration contains an unsupported DMX channel mapping.');
};

const encodeColoring = (value: DmxColoring, field = 'configuration'): number => {
	if (typeof value === 'number' && Number.isInteger(value) && value >= 0 && value <= 0xff) {
		return value;
	}
	const channel = DMX_SPECIAL_CHANNEL_ENTRIES.find(([channelName]) => channelName === value);
	if (channel) {
		return channel[1];
	}
	throw new DeviceConfigurationValidationError(field, 'DMX channels must be fixed values from 0 to 255 or supported dynamic channels.');
};
