<script lang="ts">
import UploadIcon from '@lucide/svelte/icons/upload';
import type { Client, OtaProgress } from '@mxmauro/iot-comm.js';
import { Progress } from '@skeletonlabs/skeleton-svelte';
import { onMount } from 'svelte';
import { errorMessage, type Notice, preventSubmit } from '../lib/ui';

const IOT_COMM_HEADER_SIZE = 12;
const AES_GCM_TAG_SIZE = 16;

// -----------------------------------------------------------------------------

let {
	client,
	onnotice,
	oncomplete,
	onuploadingchange,
	registerLeaveGuard
}: {
	client: Client;
	onnotice: (notice: Notice) => void;
	oncomplete: () => Promise<void>;
	onuploadingchange: (uploading: boolean) => void;
	registerLeaveGuard: (guard: () => Promise<boolean>) => () => void;
} = $props();
let file = $state<File | null>(null);
let uploading = $state(false);
let sentBytes = $state(0);
let totalBytes = $state(0);
let errors = $state<Record<string, string>>({});
let controller = $state<AbortController | null>(null);

const percent = $derived(totalBytes > 0 ? Math.min(100, (sentBytes / totalBytes) * 100) : 0);

// -----------------------------------------------------------------------------

const formatBytes = (value: number): string => {
	if (value < 1024) {
		return `${value} B`;
	}
	if (value < 1024 * 1024) {
		return `${(value / 1024).toFixed(1)} KiB`;
	}
	return `${(value / (1024 * 1024)).toFixed(1)} MiB`;
};

const selectFile = (event: Event): void => {
	// Restrict manually supplied selections as well as the file-picker's extension filter.
	const selected = (event.currentTarget as HTMLInputElement).files?.[0] ?? null;
	errors = {};
	file = null;
	if (!selected) {
		return;
	}
	if (!selected.name.toLowerCase().endsWith('.bin')) {
		errors.file = 'Select a firmware image with a .bin extension.';
		return;
	}
	if (selected.size < 1) {
		errors.file = 'Firmware image must not be empty.';
		return;
	}
	file = selected;
};

const updateProgress = (progress: OtaProgress): void => {
	// Trust the library's reported total while retaining the selected file size as the initial value.
	sentBytes = progress.sentBytes;
	totalBytes = progress.totalBytes;
};

const upload = async (): Promise<void> => {
	if (!file) {
		errors.file = 'Select a firmware image to upload.';
		return;
	}
	const chunkSize = client.maxPacketSize - IOT_COMM_HEADER_SIZE - AES_GCM_TAG_SIZE;
	if (chunkSize < 1) {
		onnotice({ tone: 'error', message: 'The device advertised an invalid maximum packet size.' });
		return;
	}
	const activeController = new AbortController();
	controller = activeController;
	uploading = true;
	onuploadingchange(true);
	sentBytes = 0;
	totalBytes = file.size;
	try {
		await client.uploadFirmware({
			image: file,
			imageSize: file.size,
			chunkSize,
			signal: activeController.signal,
			onProgress: updateProgress
		});
		onnotice({ tone: 'success', message: 'Firmware uploaded. Disconnecting from device.' });
		await oncomplete();
	} catch (error) {
		if (!activeController.signal.aborted) {
			onnotice({ tone: 'error', message: errorMessage(error) });
		}
	} finally {
		if (controller === activeController) {
			controller = null;
			uploading = false;
			onuploadingchange(false);
		}
	}
};

const cancel = (): void => {
	controller?.abort();
};

const requestLeave = async (): Promise<boolean> => {
	return !uploading;
};

onMount(() => {
	return registerLeaveGuard(requestLeave);
});
</script>

<form class="card bg-surface-100-900 p-4 w-full max-w-3xl mx-auto space-y-4" onsubmit={preventSubmit}>
	<header>
		<h1 class="text-base font-semibold text-primary-600-400">Firmware upgrade</h1>
	</header>
	<hr class="border-primary-600-400">
	{#if uploading}
		<div class="space-y-3" aria-live="polite">
			<div class="flex items-baseline justify-between gap-3">
				<span class="font-medium">Uploading firmware</span>
				<span class="text-sm text-surface-600-400">{percent.toFixed(0)}%</span>
			</div>
			<Progress value={percent} max={100} aria-label="Firmware upload progress">
				<Progress.Track class="h-3 overflow-hidden rounded bg-surface-300-700">
					<Progress.Range class="h-full bg-primary-500 transition-[width]" />
				</Progress.Track>
			</Progress>
			<p class="text-sm text-surface-600-400">{formatBytes(sentBytes)} of {formatBytes(totalBytes)}</p>
		</div>
	{:else}
		<div class="space-y-2">
			<label class="label"
				><span class="label-text">Firmware image</span>
				<input
					class="input"
					type="file"
					accept=".bin"
					disabled={uploading}
					aria-invalid={!!errors.file}
					aria-describedby="firmware-image-error"
					onchange={selectFile}
				>
				{#if errors.file}
					<small id="firmware-image-error" class="text-error-600-400">{errors.file}</small>
				{/if}</label
			>
			{#if file}
				<small class="text-surface-600-400">{file.name} · {formatBytes(file.size)}</small>
			{/if}
		</div>
	{/if}
	<footer class="form-actions">
		<small class="mr-auto self-end">Do not close the app or disconnect the device during the upload.</small>
		{#if uploading}
			<button class="btn preset-filled-warning-500" type="button" onclick={cancel}>Cancel upload</button>
		{:else}
			<button class="btn preset-filled-primary-500" type="button" disabled={!file} onclick={() => void upload()}>
				<UploadIcon />
				Upload firmware
			</button>
		{/if}
	</footer>
</form>
