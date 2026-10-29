import type { Client } from '@mxmauro/iot-comm.js';

const CONFIG_VERSION = 1;
const REPLY_VERSION = 1;
const DMX_UNIVERSE_SIZE = 128;
const DMX_MAX_FIXTURES = 16;
const DMX_MAX_FIXTURE_TYPE_BYTES = 32;
const DMX_MAX_FIXTURE_CHANNELS = 32;
const MAX_COMMAND_PAYLOAD_BYTES = 2000;

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

export type DmxFixture = {
	type: string;
	startChannel: number;
	coloring: DmxColoring[];
};

export type DeviceConfiguration =
	| { output: 'none' }
	| { output: 'rgb5050'; redGpio: number; greenGpio: number; blueGpio: number; activeLow: boolean }
	| { output: 'dmx'; txGpio: number; fixtures: DmxFixture[] };

export type DeviceConfigurationReply = {
	commandFlags: number;
	configuration: DeviceConfiguration;
};

// Carries the form field associated with a protocol-level configuration validation failure.
class DeviceConfigurationValidationError extends Error {
	public readonly field: string;

	public constructor(field: string, message: string) {
		super(message);
		this.field = field;
	}
}

// Incrementally decodes a bounded, big-endian device-configuration reply.
class Reader {
	private readonly bytes: Uint8Array;
	private offset = 0;

	public constructor(data: ArrayBuffer) {
		this.bytes = new Uint8Array(data);
	}

	public byte(): number {
		this.require(1);
		return this.bytes[this.offset++];
	}

	public uint16(): number {
		this.require(2);
		const value = (this.bytes[this.offset] << 8) | this.bytes[this.offset + 1];
		this.offset += 2;
		return value;
	}

	public uint32(): number {
		this.require(4);
		const value = new DataView(this.bytes.buffer, this.bytes.byteOffset + this.offset, 4).getUint32(0, false);
		this.offset += 4;
		return value;
	}

	public string(): string {
		const start = this.offset;
		while (this.offset < this.bytes.length && this.bytes[this.offset] !== 0) {
			this.offset++;
		}
		if (this.offset === this.bytes.length) {
			throw new Error('Configuration contains an unterminated string.');
		}
		const value = new TextDecoder('utf-8', { fatal: true }).decode(this.bytes.slice(start, this.offset));
		this.offset++;
		return value;
	}

	public assertFinished(): void {
		if (this.offset !== this.bytes.length) {
			throw new Error('Configuration reply contains trailing data.');
		}
	}

	private require(length: number): void {
		if (this.offset + length > this.bytes.length) {
			throw new Error('Configuration reply is truncated.');
		}
	}
}

// -----------------------------------------------------------------------------

export const GPIO_MIN = 0;
export const GPIO_MAX = 127;
export const CMD_GET_DEVICE_CONFIG = 0x7001;
export const CMD_SET_DEVICE_CONFIG = 0x7002;
export const DMX_SPECIAL_CHANNELS: readonly DmxSpecialChannel[] = DMX_SPECIAL_CHANNEL_ENTRIES.map(([channel]) => channel);

// -----------------------------------------------------------------------------

export const isDmxSpecialChannel = (value: unknown): value is DmxSpecialChannel => {
	return typeof value === 'string' && DMX_SPECIAL_CHANNELS.includes(value as DmxSpecialChannel);
};

export const getDeviceConfigurationValidationErrors = (configuration: DeviceConfiguration): Record<string, string> => {
	// Preserve independent RGB field errors so each pin input can show its own correction.
	if (configuration.output === 'rgb5050') {
		const errors: Record<string, string> = {};
		const redIsValid = isValidGpio(configuration.redGpio);
		const greenIsValid = isValidGpio(configuration.greenGpio);
		const blueIsValid = isValidGpio(configuration.blueGpio);

		if (!redIsValid) {
			errors.redGpio = `GPIO pin must be between ${GPIO_MIN} and ${GPIO_MAX}.`;
		}
		if (!greenIsValid) {
			errors.greenGpio = `GPIO pin must be between ${GPIO_MIN} and ${GPIO_MAX}.`;
		} else if (redIsValid && configuration.greenGpio === configuration.redGpio) {
			errors.greenGpio = 'Pin is already used by Red GPIO.';
		}
		if (!blueIsValid) {
			errors.blueGpio = `GPIO pin must be between ${GPIO_MIN} and ${GPIO_MAX}.`;
		} else if (redIsValid && configuration.blueGpio === configuration.redGpio) {
			errors.blueGpio = 'Pin is already used by Red GPIO.';
		} else if (greenIsValid && configuration.blueGpio === configuration.greenGpio) {
			errors.blueGpio = 'Pin is already used by Green GPIO.';
		}

		return errors;
	}

	// Convert shared configuration validation failures into the form field that needs attention.
	try {
		validateDeviceConfiguration(configuration);
		return {};
	} catch (error) {
		if (error instanceof DeviceConfigurationValidationError) {
			return { [error.field]: error.message };
		}
		return { configuration: error instanceof Error ? error.message : 'Configuration is invalid.' };
	}
};

export const validateDeviceConfiguration = (configuration: DeviceConfiguration): void => {
	// Disabled output has no hardware fields to validate.
	if (configuration.output === 'none') {
		return;
	}

	// Validate dedicated RGB pins and their uniqueness before accepting an RGB configuration.
	if (configuration.output === 'rgb5050') {
		requireGpio(configuration.redGpio, 'redGpio');
		requireGpio(configuration.greenGpio, 'greenGpio');
		if (configuration.greenGpio === configuration.redGpio) {
			throw new DeviceConfigurationValidationError('greenGpio', 'Pin is already used by Red GPIO.');
		}
		requireGpio(configuration.blueGpio, 'blueGpio');
		if (configuration.blueGpio === configuration.redGpio) {
			throw new DeviceConfigurationValidationError('blueGpio', 'Pin is already used by Red GPIO.');
		}
		if (configuration.blueGpio === configuration.greenGpio) {
			throw new DeviceConfigurationValidationError('blueGpio', 'Pin is already used by Green GPIO.');
		}
		return;
	}

	// Validate DMX transport, fixture bounds, and non-overlapping channel assignments.
	requireGpio(configuration.txGpio, 'txGpio');
	if (configuration.fixtures.length > DMX_MAX_FIXTURES) {
		throw new DeviceConfigurationValidationError('fixtures', `DMX supports at most ${DMX_MAX_FIXTURES} fixtures.`);
	}
	const usedChannels = new Set<number>();
	const encoder = new TextEncoder();
	for (const [index, fixture] of configuration.fixtures.entries()) {
		const name = `Fixture ${index + 1}`;
		const typeLength = encoder.encode(fixture.type).byteLength;
		if (typeLength < 1 || typeLength > DMX_MAX_FIXTURE_TYPE_BYTES || fixture.type.includes('\0')) {
			throw new DeviceConfigurationValidationError(
				`fixtures.${index}.type`,
				`${name} needs a type name between 1 and ${DMX_MAX_FIXTURE_TYPE_BYTES} characters.`
			);
		}
		if (!Number.isInteger(fixture.startChannel) || fixture.startChannel < 1 || fixture.startChannel > DMX_UNIVERSE_SIZE) {
			throw new DeviceConfigurationValidationError(
				`fixtures.${index}.startChannel`,
				`${name} start channel must be between 1 and ${DMX_UNIVERSE_SIZE}.`
			);
		}
		if (fixture.coloring.length < 1 || fixture.coloring.length > DMX_MAX_FIXTURE_CHANNELS) {
			throw new DeviceConfigurationValidationError(
				`fixtures.${index}.coloring`,
				`${name} must have 1 to ${DMX_MAX_FIXTURE_CHANNELS} channels.`
			);
		}
		if (fixture.startChannel + fixture.coloring.length - 1 > DMX_UNIVERSE_SIZE) {
			throw new DeviceConfigurationValidationError(
				`fixtures.${index}.startChannel`,
				`${name} extends beyond DMX channel ${DMX_UNIVERSE_SIZE}.`
			);
		}
		for (const [channel, coloring] of fixture.coloring.entries()) {
			encodeColoring(coloring, `fixtures.${index}.coloring.${channel}`);
			const channelNumber = fixture.startChannel + channel;
			if (usedChannels.has(channelNumber)) {
				throw new DeviceConfigurationValidationError(
					`fixtures.${index}.coloring.${channel}`,
					`DMX channel ${channelNumber} is used by more than one fixture.`
				);
			}
			usedChannels.add(channelNumber);
		}
	}
};

export const decodeDeviceConfigurationReply = (data: ArrayBuffer): DeviceConfigurationReply => {
	// Decode the reply envelope before dispatching to the output-specific configuration payload.
	const reader = new Reader(data);
	if (reader.byte() !== REPLY_VERSION) {
		throw new Error('Unsupported device configuration reply version.');
	}
	const commandFlags = reader.uint32();
	const version = reader.byte();
	if (version !== CONFIG_VERSION) {
		throw new Error('Unsupported device configuration version.');
	}
	const outputType = reader.byte();
	let configuration: DeviceConfiguration;
	// Decode and validate the selected output payload without accepting unsupported variants.
	if (outputType === 0) {
		configuration = { output: 'none' };
	} else if (outputType === 2) {
		const redGpio = reader.byte();
		const greenGpio = reader.byte();
		const blueGpio = reader.byte();
		const activeLow = reader.byte();
		if (activeLow > 1) {
			throw new Error('Configuration contains an invalid RGB5050 polarity.');
		}
		configuration = {
			output: 'rgb5050',
			redGpio,
			greenGpio,
			blueGpio,
			activeLow: activeLow === 1
		};
		validateDeviceConfiguration(configuration);
	} else if (outputType === 1) {
		if (reader.uint16() !== DMX_UNIVERSE_SIZE) {
			throw new Error('Unsupported DMX universe size.');
		}
		const txGpio = reader.byte();
		const fixtureCount = reader.byte();
		if (fixtureCount > DMX_MAX_FIXTURES) {
			throw new Error('Configuration has too many DMX fixtures.');
		}
		const fixtures: DmxFixture[] = [];
		for (let i = 0; i < fixtureCount; i++) {
			const type = reader.string();
			const startChannel = reader.uint16();
			const channelCount = reader.byte();
			const coloring = Array.from({ length: channelCount }, () => decodeColoring(reader.uint16()));
			fixtures.push({ type, startChannel, coloring });
		}
		configuration = { output: 'dmx', txGpio, fixtures };
		validateDeviceConfiguration(configuration);
	} else {
		throw new Error('Configuration contains an unsupported output type.');
	}
	// Reject trailing reply bytes before returning a fully validated configuration.
	reader.assertFinished();
	return { commandFlags, configuration };
};

export const encodeDeviceConfiguration = (configuration: DeviceConfiguration): ArrayBuffer => {
	// Validate first, then build the versioned command payload in the firmware's byte order.
	validateDeviceConfiguration(configuration);
	const bytes: number[] = [CONFIG_VERSION, configuration.output === 'none' ? 0 : configuration.output === 'dmx' ? 1 : 2];
	const write16 = (value: number): void => {
		bytes.push((value >>> 8) & 0xff, value & 0xff);
	};
	// Append only the fields required by the selected output controller.
	if (configuration.output === 'rgb5050') {
		bytes.push(configuration.redGpio, configuration.greenGpio, configuration.blueGpio);
		bytes.push(configuration.activeLow ? 1 : 0);
	}
	if (configuration.output === 'dmx') {
		write16(DMX_UNIVERSE_SIZE);
		bytes.push(configuration.txGpio);
		bytes.push(configuration.fixtures.length);
		for (const fixture of configuration.fixtures) {
			bytes.push(...new TextEncoder().encode(fixture.type), 0);
			write16(fixture.startChannel);
			bytes.push(fixture.coloring.length);
			for (const coloring of fixture.coloring) {
				write16(encodeColoring(coloring));
			}
		}
	}
	// Keep the resulting command within the transport's maximum payload size.
	if (bytes.length > MAX_COMMAND_PAYLOAD_BYTES) {
		throw new Error('Configuration is too large to send.');
	}
	return Uint8Array.from(bytes).buffer;
};

export const getDeviceConfiguration = async (client: Client): Promise<DeviceConfigurationReply> => {
	return decodeDeviceConfigurationReply(await client.sendCommandAndWaitReply(CMD_GET_DEVICE_CONFIG, new ArrayBuffer(0)));
};

export const setDeviceConfiguration = async (client: Client, configuration: DeviceConfiguration): Promise<void> => {
	const reply = await client.sendCommandAndWaitReply(CMD_SET_DEVICE_CONFIG, encodeDeviceConfiguration(configuration));
	assertCommandSuccess(reply);
};

// -----------------------------------------------------------------------------

const isValidGpio = (value: number): boolean => {
	return Number.isInteger(value) && value >= GPIO_MIN && value <= GPIO_MAX;
};

const requireGpio = (value: number, field: string): void => {
	if (!isValidGpio(value)) {
		throw new DeviceConfigurationValidationError(field, `GPIO pin must be between ${GPIO_MIN} and ${GPIO_MAX}.`);
	}
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

const assertCommandSuccess = (reply: ArrayBuffer): void => {
	// Decode the firmware status envelope and preserve its human-readable failure message.
	if (reply.byteLength < 4) {
		throw new Error('Device command returned an incomplete response.');
	}
	const view = new DataView(reply);
	const code = view.getUint32(0, false);
	if (code === 0) {
		return;
	}
	let end = 4;
	while (end < reply.byteLength && view.getUint8(end) !== 0) {
		end++;
	}
	const message = new TextDecoder('utf-8', { fatal: false }).decode(new Uint8Array(reply, 4, end - 4));
	throw new Error(message || `Device command failed with code ${code}.`);
};
