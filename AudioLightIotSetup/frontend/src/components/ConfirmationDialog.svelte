<script lang="ts">
import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw';

let {
	title,
	summary,
	busy = false,
	busyLabel = 'Saving…',
	oncancel,
	onconfirm
}: {
	title: string;
	summary: string;
	busy?: boolean;
	busyLabel?: string;
	oncancel: () => void;
	onconfirm: () => void;
} = $props();
</script>

<div class="fixed inset-0 z-50 grid place-items-center bg-black/50 p-4" role="presentation">
	<div
		class="card bg-surface-100-900 mx-auto w-full max-w-3xl space-y-4 p-4"
		role="dialog"
		aria-modal="true"
		aria-labelledby="confirmation-title"
		tabindex="-1"
	>
		<header><h2 id="confirmation-title" class="text-base font-semibold text-primary-600-400">{title}</h2></header>
		<p>{summary}</p>
		<footer class="form-actions">
			{#if busy}
				<span class="mr-auto flex items-center gap-2 text-sm" role="status"><RefreshCwIcon class="spin-360" />{busyLabel}</span>
			{/if}
			<button class="btn preset-filled-surface-500" type="button" disabled={busy} onclick={oncancel}>Cancel</button>
			<button class="btn preset-filled" type="button" disabled={busy} onclick={onconfirm}>Confirm</button>
		</footer>
	</div>
</div>
