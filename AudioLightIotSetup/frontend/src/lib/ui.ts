export type Notice = {
	tone: 'success' | 'error' | 'info';
	message: string;
	fingerprint?: string;
};

// -----------------------------------------------------------------------------

export const errorMessage = (error: unknown): string => {
	return error instanceof Error ? error.message : 'The operation failed.';
};

export const errorNotice = (error: unknown): Notice => {
	const message = errorMessage(error);
	const fingerprint = message.match(/\b[\da-f]{64}\b/i)?.[0];
	if (!fingerprint) {
		return { tone: 'error', message };
	}
	return { tone: 'error', message: message.replace(fingerprint, '').trim(), fingerprint };
};

export const preventSubmit = (event: SubmitEvent): void => {
	event.preventDefault();
};

export const formatFingerprint = (value: string): string[] => {
	const bytes = value.toUpperCase().match(/.{1,2}/g) ?? [];
	return Array.from({ length: 2 }, (_, row) => bytes.slice(row * 16, row * 16 + 16).join(':'));
};
