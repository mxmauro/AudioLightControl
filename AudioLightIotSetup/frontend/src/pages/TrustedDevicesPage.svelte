<script lang="ts">
import CircleXIcon from '@lucide/svelte/icons/circle-x';
import { Pagination } from '@skeletonlabs/skeleton-svelte';
import ConfirmationDialog from '../components/ConfirmationDialog.svelte';
import FingerprintValue from '../components/FingerprintValue.svelte';
import { forgetTrustedFingerprint, getTrustedDevices, type TrustedDevice } from '../lib/session';

const DEVICES_PER_PAGE = 8;

// -----------------------------------------------------------------------------

let { onback }: { onback: () => void } = $props();
let devices = $state<TrustedDevice[]>(getTrustedDevices());
let pendingHost = $state<string | null>(null);
let page = $state(1);
let pagedDevices = $derived(devices.slice((page - 1) * DEVICES_PER_PAGE, page * DEVICES_PER_PAGE));

// -----------------------------------------------------------------------------

const forgetDevice = (): void => {
	// Remove only the confirmed host and reload the list from persisted trust decisions.
	if (!pendingHost) {
		return;
	}
	forgetTrustedFingerprint(pendingHost);
	devices = getTrustedDevices();
	page = Math.min(page, Math.max(1, Math.ceil(devices.length / DEVICES_PER_PAGE)));
	pendingHost = null;
};

const setPage = (details: { page: number }): void => {
	page = details.page;
};
</script>

<main class="mx-auto flex h-full min-h-0 max-w-5xl items-center overflow-y-auto p-4 md:p-8">
	<section class="card bg-surface-100-900 p-4 w-full max-w-3xl mx-auto space-y-4">
		<header>
			<h1 class="text-base font-semibold text-primary-600-400">Trusted Devices</h1>
		</header>
		<hr class="border-primary-600-400">
		{#if devices.length === 0}
			<p class="text-surface-600-400">No device fingerprints are stored.</p>
		{:else}
			<div class="overflow-x-auto">
				<table class="w-full table-fixed border-collapse text-left text-sm">
					<thead class="border-b border-surface-300-700 text-surface-600-400">
						<tr>
							<th class="w-1/5 px-3 py-2 font-medium" scope="col">IP address</th>
							<th class="w-28 px-3 py-2 font-medium" scope="col">Trust</th>
							<th class="px-3 py-2 font-medium" scope="col">Fingerprint</th>
							<th class="w-12 px-3 py-2" scope="col"><span class="sr-only">Actions</span></th>
						</tr>
					</thead>
					<tbody>
						{#each pagedDevices as device}
							<tr class="border-b border-surface-300-700 last:border-b-0 hover:bg-surface-50-950">
								<td class="truncate px-3 py-2 font-semibold" title={device.host}>{device.host}</td>
								<td class="px-3 py-2">
									<span class:preset-filled-success-500={device.trusted} class:preset-filled-error-500={!device.trusted} class="badge">
										{device.trusted ? 'Trusted' : 'Untrusted'}
									</span>
								</td>
								<td class="min-w-0 px-3 py-2"><FingerprintValue fingerprint={device.fingerprint} compact /></td>
								<td class="px-3 py-2 text-right">
									<button
										class="btn btn-icon-sm !border-0 !bg-transparent !p-0 text-error-500 hover:!bg-transparent hover:text-error-400 active:!bg-transparent active:text-error-600"
										type="button"
										aria-label={`Forget ${device.host}`}
										title="Remove trusted device"
										onclick={() => (pendingHost = device.host)}
									>
										<CircleXIcon />
									</button>
								</td>
							</tr>
						{/each}
					</tbody>
				</table>
			</div>
			{#if devices.length > DEVICES_PER_PAGE}
				<Pagination count={devices.length} pageSize={DEVICES_PER_PAGE} {page} onPageChange={setPage}>
					<Pagination.Context>
						{#snippet children(pagination)}
							<Pagination.PrevTrigger>Previous</Pagination.PrevTrigger>
							{#each pagination().pages as item, index}
								{#if item.type === 'page'}
									<Pagination.Item type="page" value={item.value}>{item.value}</Pagination.Item>
								{:else}
									<Pagination.Ellipsis {index}>…</Pagination.Ellipsis>
								{/if}
							{/each}
							<Pagination.NextTrigger>Next</Pagination.NextTrigger>
						{/snippet}
					</Pagination.Context>
				</Pagination>
			{/if}
		{/if}
		<footer class="form-actions">
			<button class="btn preset-filled-surface-500" type="button" onclick={onback}>Back to connection</button>
		</footer>
	</section>
</main>

{#if pendingHost}
	<ConfirmationDialog
		title="Forget trusted device"
		summary={`Forget the fingerprint stored for “${pendingHost}”? You will need to verify it again before connecting.`}
		oncancel={() => (pendingHost = null)}
		onconfirm={forgetDevice}
	/>
{/if}
