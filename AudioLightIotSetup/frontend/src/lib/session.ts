import { Client } from '@mxmauro/iot-comm.js';

const LAST_HOST_KEY = 'audiolight.setup.last-host';
const TRUSTED_FINGERPRINTS_KEY = 'audiolight.setup.trusted-fingerprints';

// -----------------------------------------------------------------------------

export type TrustedDevice = {
	host: string;
	fingerprint: string;
	trusted: boolean;
};

type StoredDevice = {
	fingerprint: string;
	trusted: boolean;
};

// -----------------------------------------------------------------------------

export const getLastHost = (): string => {
	return localStorage.getItem(LAST_HOST_KEY) ?? '';
};

export const isValidHost = (value: string): boolean => {
	// Accept only a hostname-style URL without credentials, paths, queries, or fragments.
	try {
		const url = new URL(`http://${value.trim()}`);
		return !url.username && !url.password && url.pathname === '/' && !url.search && !url.hash && !!url.hostname;
	} catch {
		return false;
	}
};

export const normalizeHost = (value: string): string => {
	if (!isValidHost(value)) {
		throw new Error('Invalid device host.');
	}
	return new URL(`http://${value.trim()}`).host;
};

export const decodeBase64Key = (value: string, expectedLength: number, label: string): ArrayBuffer => {
	// Decode and size-check imported key material before it is handed to the connection client.
	try {
		const normalized = value.trim();
		const binary = atob(normalized);
		const bytes = Uint8Array.from(binary, (character) => character.charCodeAt(0));
		if (bytes.byteLength !== expectedLength) {
			throw new Error();
		}
		return bytes.buffer;
	} catch {
		throw new Error(`${label} must be a valid Base64 value with ${expectedLength} bytes.`);
	}
};

export const getTrustedDevices = (): TrustedDevice[] => {
	// Convert persisted fingerprints to display records in a stable hostname order.
	return Object.entries(getStoredDevices())
		.map(([host, device]) => ({ host, fingerprint: device.fingerprint, trusted: device.trusted }))
		.sort((left, right) => left.host.localeCompare(right.host));
};

export const forgetTrustedFingerprint = (host: string): void => {
	const devices = getStoredDevices();
	delete devices[host];
	localStorage.setItem(TRUSTED_FINGERPRINTS_KEY, JSON.stringify(devices));
};

export const connectClient = async (
	host: string,
	username: string,
	privateKey: ArrayBuffer,
	confirmFingerprint: (fingerprint: string, changed: boolean) => Promise<boolean>
): Promise<Client> => {
	// Ask before trusting changed fingerprints and persist the resulting trust decision.
	const client = new Client();
	await client.connect({
		hostname: host,
		username,
		privateKey,
		verifyServerFingerprint: async (fingerprint) => {
			const stored = getStoredDevices()[host];
			if (stored?.fingerprint === fingerprint) {
				return stored.trusted;
			}
			const accepted = await confirmFingerprint(fingerprint, !!stored);
			saveFingerprintStatus(host, fingerprint, accepted);
			return accepted;
		}
	});
	localStorage.setItem(LAST_HOST_KEY, host);
	return client;
};

// -----------------------------------------------------------------------------

const getStoredDevices = (): Record<string, StoredDevice> => {
	// Parse trusted-device storage defensively, including the legacy string-only entry format.
	try {
		const saved = JSON.parse(localStorage.getItem(TRUSTED_FINGERPRINTS_KEY) ?? '{}');
		if (typeof saved !== 'object' || saved === null) {
			return {};
		}
		// Retain only structurally valid records so corrupt browser storage never reaches the client.
		return Object.fromEntries(
			Object.entries(saved).flatMap(([host, entry]) => {
				if (typeof entry === 'string') {
					return [[host, { fingerprint: entry, trusted: true }]];
				}
				if (
					typeof entry === 'object' &&
					entry !== null &&
					'fingerprint' in entry &&
					'trusted' in entry &&
					typeof entry.fingerprint === 'string' &&
					typeof entry.trusted === 'boolean'
				) {
					return [[host, { fingerprint: entry.fingerprint, trusted: entry.trusted }]];
				}
				return [];
			})
		);
	} catch {
		return {};
	}
};

const saveFingerprintStatus = (host: string, fingerprint: string, trusted: boolean): void => {
	// Replace the host's stored decision after the user evaluates a server fingerprint.
	const devices = getStoredDevices();
	devices[host] = { fingerprint, trusted };
	localStorage.setItem(TRUSTED_FINGERPRINTS_KEY, JSON.stringify(devices));
};
