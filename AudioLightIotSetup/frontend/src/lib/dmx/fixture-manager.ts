import type { storage } from '../../generated/wailsjs/go/models';
import { Load, Save } from '../../generated/wailsjs/go/storage/Storage';
import { DMX_SPECIAL_CHANNELS, type DmxColoring, type DmxSpecialChannel, isDmxSpecialChannel } from '../device-config';

const PREDEFINED_FIXTURES_FILENAME = 'predefined_dmx.json';
const MAX_CHANNELS = 32;

// -----------------------------------------------------------------------------

export type DmxFixturePreset = {
	type: string;
	coloring: DmxColoring[];
};

type StoredFixture = {
	type: string;
	channels: string[];
};

type StoredFixtures = {
	fixtures: StoredFixture[];
};

export class DmxFixtureManager {
	private fixtures: DmxFixturePreset[] = [];

	public async load(): Promise<void> {
		// Read the persisted preset collection before deciding whether first-run defaults are needed.
		const result = (await Load(PREDEFINED_FIXTURES_FILENAME)) as storage.LoadResult;
		if (result.Error === 'not found') {
			// Make the first-run fixture available immediately and persist it for later editing.
			this.fixtures = [defaultFixture()];
			await this.save();
			return;
		}

		// Surface storage failures without treating them as an empty fixture collection.
		if (result.Error) {
			throw new Error(`Load predefined DMX fixtures: ${result.Error}`);
		}

		// Decode and validate the persisted fixture data before exposing it to the UI.
		this.fixtures = parseFixtures(result.Content);
	}

	public all(): DmxFixturePreset[] {
		return this.fixtures.map(cloneFixture);
	}

	public find(type: string): DmxFixturePreset | undefined {
		const fixture = this.fixtures.find((item) => equalTypes(item.type, normalizeFixtureType(type)));
		return fixture ? cloneFixture(fixture) : undefined;
	}

	public async add(fixture: DmxFixturePreset, replace = false): Promise<void> {
		// Normalize the candidate and reject duplicate fixture types unless replacement was requested.
		const normalized = normalizeFixture(fixture);
		const index = this.findIndex(normalized.type);
		if (index >= 0 && !replace) {
			throw new Error(`DMX fixture type ${JSON.stringify(normalized.type)} already exists.`);
		}
		const previous = this.all();

		// Update the in-memory collection first so one shared manager reflects the pending change.
		if (index >= 0) {
			this.fixtures[index] = normalized;
		} else {
			this.fixtures = [...this.fixtures, normalized];
		}
		// Persist the replacement atomically from the caller's perspective by restoring on failure.
		try {
			await this.save();
		} catch (error) {
			this.fixtures = previous;
			throw error;
		}
	}

	public async remove(type: string): Promise<void> {
		// Resolve the type using the manager's normalized, case-insensitive lookup.
		const index = this.findIndex(type);
		if (index < 0) {
			throw new Error(`DMX fixture type ${JSON.stringify(normalizeFixtureType(type))} does not exist.`);
		}
		const previous = this.all();

		// Keep the previous collection so a failed disk write does not lose a preset in memory.
		this.fixtures = this.fixtures.filter((_, item) => item !== index);
		try {
			await this.save();
		} catch (error) {
			this.fixtures = previous;
			throw error;
		}
	}

	public async update(previousType: string, fixture: DmxFixturePreset, replaceDuplicate = false): Promise<void> {
		// Validate the existing preset and any conflicting destination type before mutating the collection.
		const previousIndex = this.findIndex(previousType);
		if (previousIndex < 0) {
			throw new Error(`DMX fixture type ${JSON.stringify(normalizeFixtureType(previousType))} does not exist.`);
		}
		const normalized = normalizeFixture(fixture);
		const duplicateIndex = this.findIndex(normalized.type);
		if (duplicateIndex >= 0 && duplicateIndex !== previousIndex && !replaceDuplicate) {
			throw new Error(`DMX fixture type ${JSON.stringify(normalized.type)} already exists.`);
		}
		const previous = this.all();

		// Replace the selected fixture and discard a confirmed duplicate, then roll back if saving fails.
		this.fixtures[previousIndex] = normalized;
		if (duplicateIndex >= 0 && duplicateIndex !== previousIndex) {
			this.fixtures = this.fixtures.filter((_, index) => index !== duplicateIndex);
		}
		try {
			await this.save();
		} catch (error) {
			this.fixtures = previous;
			throw error;
		}
	}

	private async save(): Promise<void> {
		// Store the canonical wrapper shape used by the Go fixture manager's saver.
		const fixtures: StoredFixtures = {
			fixtures: this.fixtures.map((fixture) => ({ type: fixture.type, channels: fixture.coloring.map(formatChannel) }))
		};
		const error = await Save(PREDEFINED_FIXTURES_FILENAME, JSON.stringify(fixtures, null, '\t'));
		if (error) {
			throw new Error(`Save predefined DMX fixtures: ${error}`);
		}
	}

	private findIndex(type: string): number {
		const normalized = normalizeFixtureType(type);
		return this.fixtures.findIndex((fixture) => equalTypes(fixture.type, normalized));
	}
}

// -----------------------------------------------------------------------------

export const dmxFixtureManager = new DmxFixtureManager();

// -----------------------------------------------------------------------------

export const getDmxFixturePresets = async (): Promise<DmxFixturePreset[]> => {
	// Refresh the shared manager before returning a defensive copy for a page load.
	await dmxFixtureManager.load();
	return dmxFixtureManager.all();
};

export const normalizeFixtureType = (type: string): string => type.trim();

export const normalizeFixture = (fixture: DmxFixturePreset): DmxFixturePreset => {
	// Enforce the fixture-manager channel limit while canonicalizing values for storage and matching.
	if (fixture.coloring.length > MAX_CHANNELS) {
		throw new Error('DMX fixture cannot contain more than 32 channels.');
	}
	return { type: normalizeFixtureType(fixture.type), coloring: fixture.coloring.map(normalizeColoring) };
};

export const fixturesMatch = (fixture: DmxFixturePreset, coloring: DmxColoring[]): boolean => {
	// Compare the normalized preset mapping with a device fixture without mutating either input.
	const normalized = normalizeFixture(fixture);
	return normalized.coloring.length === coloring.length && normalized.coloring.every((channel, index) => channel === coloring[index]);
};

// -----------------------------------------------------------------------------

const defaultFixture = (): DmxFixturePreset => ({
	type: 'PAR 54x3W RGB (3CH)',
	coloring: ['Color.Red', 'Color.Green', 'Color.Blue']
});

const cloneFixture = (fixture: DmxFixturePreset): DmxFixturePreset => ({ ...fixture, coloring: [...fixture.coloring] });

const parseFixtures = (content: string): DmxFixturePreset[] => {
	// Decode the persisted text while reporting malformed JSON as a user-actionable error.
	let parsed: unknown;
	try {
		parsed = JSON.parse(content);
	} catch {
		throw new Error('Decode predefined DMX fixtures: the file is not valid JSON.');
	}
	// Accept both the legacy array format and the current wrapper before validating each fixture.
	const stored = Array.isArray(parsed) ? parsed : isStoredFixtures(parsed) ? parsed.fixtures : undefined;
	if (!stored) {
		throw new Error('Decode predefined DMX fixtures: expected an array of fixtures.');
	}
	// Reject duplicate normalized types so preset selection remains unambiguous.
	const fixtures = stored.map(parseFixture);
	for (const [index, fixture] of fixtures.entries()) {
		if (fixtures.slice(0, index).some((previous) => equalTypes(previous.type, fixture.type))) {
			throw new Error(`Duplicated DMX fixture type ${JSON.stringify(fixture.type)}.`);
		}
	}
	return fixtures;
};

const isStoredFixtures = (value: unknown): value is StoredFixtures => {
	return typeof value === 'object' && value !== null && 'fixtures' in value && Array.isArray(value.fixtures);
};

const parseFixture = (value: unknown): DmxFixturePreset => {
	// Confirm the decoded item has the fixture fields expected by persisted data.
	if (typeof value !== 'object' || value === null || !('type' in value) || !('channels' in value)) {
		throw new Error('Decode predefined DMX fixtures: each fixture must contain a type and channels.');
	}
	// Require string channel values before converting them to the application representation.
	if (typeof value.type !== 'string' || !Array.isArray(value.channels) || !value.channels.every((channel) => typeof channel === 'string')) {
		throw new Error('Decode predefined DMX fixtures: each fixture must contain a string type and string channels.');
	}
	return normalizeFixture({ type: value.type, coloring: value.channels.map(parseChannel) });
};

const parseChannel = (channel: string): DmxColoring => {
	const normalized = trimAsciiWhitespace(channel).toLowerCase();
	const specialChannel = specialChannelFromToken(normalized);
	if (specialChannel) {
		return specialChannel;
	}
	if (!/^[\da-f]{2}$/.test(normalized)) {
		throw new Error('DMX channel must be a supported dynamic channel or a two-digit hexadecimal value.');
	}
	return Number.parseInt(normalized, 16);
};

const normalizeColoring = (coloring: DmxColoring): DmxColoring => {
	if (isDmxSpecialChannel(coloring)) {
		return coloring;
	}
	if (!Number.isInteger(coloring) || coloring < 0 || coloring > 255) {
		throw new Error('DMX channel must be a supported dynamic channel or a value from 0 to 255.');
	}
	return coloring;
};

const formatChannel = (coloring: DmxColoring): string => {
	return typeof coloring === 'number' ? coloring.toString(16).padStart(2, '0').toUpperCase() : coloring;
};

const specialChannelFromToken = (token: string): DmxSpecialChannel | undefined => {
	if (token === 'dimmer.coarse') {
		return 'Dimmer';
	}
	if (token === 'pan.coarse') {
		return 'Pan';
	}
	if (token === 'tilt.coarse') {
		return 'Tilt';
	}
	return DMX_SPECIAL_CHANNELS.find((channel) => channel.toLowerCase() === token);
};

const trimAsciiWhitespace = (value: string): string => {
	let start = 0;
	let end = value.length;
	while (start < end && isAsciiWhitespace(value.charCodeAt(start))) {
		start++;
	}
	while (end > start && isAsciiWhitespace(value.charCodeAt(end - 1))) {
		end--;
	}
	return value.slice(start, end);
};

const isAsciiWhitespace = (character: number): boolean => {
	return character === 0x20 || (character >= 0x09 && character <= 0x0d);
};

const equalTypes = (left: string, right: string): boolean => {
	return left.localeCompare(right, undefined, { sensitivity: 'accent' }) === 0;
};
