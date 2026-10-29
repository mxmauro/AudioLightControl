<script lang="ts">
import FingerprintValue from './FingerprintValue.svelte';

let {
	fingerprint,
	changed = false,
	oncancel,
	ontrust
}: {
	fingerprint: string;
	changed?: boolean;
	oncancel: () => void;
	ontrust: () => void;
} = $props();
</script>

<div class="fixed inset-0 z-50 grid place-items-center bg-black/50 p-4" role="presentation">
	<div
		class="card bg-surface-100-900 mx-auto w-full max-w-3xl space-y-4 p-4"
		role="dialog"
		aria-modal="true"
		aria-labelledby="fingerprint-title"
		tabindex="-1"
	>
		<header>
			<h2 id="fingerprint-title" class="text-base font-semibold text-primary-600-400">
				{changed ? 'Fingerprint changed' : 'Trust this device?'}
			</h2>
		</header>
		{#if changed}
			<p class="notice-error rounded p-3" role="alert">
				This device presented a different fingerprint than the one previously trusted. Verify it before replacing the saved trust.
			</p>
		{:else}
			<p>Verify this fingerprint with the device owner before trusting it.</p>
		{/if}
		<FingerprintValue {fingerprint} />
		<footer class="form-actions">
			<button class="btn preset-filled-surface-500" type="button" onclick={oncancel}>Do not trust</button>
			<button class="btn preset-filled" type="button" onclick={ontrust}>{changed ? 'Replace trust' : 'Trust device'}</button>
		</footer>
	</div>
</div>
