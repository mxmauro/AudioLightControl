<script lang="ts">
import { formatFingerprint } from '../lib/ui';

let { fingerprint, compact = false }: { fingerprint: string; compact?: boolean } = $props();
let rows = $derived(formatFingerprint(fingerprint));
let value = $derived(rows.join(':'));
let fingerprintElement = $state<HTMLElement>();
let valueElement = $state<HTMLSpanElement>();
let compactCharacterLimit = $state(Number.POSITIVE_INFINITY);
let compactValue = $derived(
	compactCharacterLimit >= value.length
		? value
		: `${value.slice(0, Math.max(1, Math.floor((compactCharacterLimit - 1) / 2)))}…${value.slice(
				-Math.max(1, Math.floor((compactCharacterLimit - 1) / 2))
			)}`
);

$effect(() => {
	if (!compact || !fingerprintElement || !valueElement) {
		compactCharacterLimit = Number.POSITIVE_INFINITY;
		return;
	}

	const currentFingerprintElement = fingerprintElement;
	const currentValueElement = valueElement;
	const updateCharacterLimit = (): void => {
		const characterWidth = currentValueElement.offsetWidth / value.length;
		compactCharacterLimit = characterWidth > 0 ? Math.floor(currentFingerprintElement.clientWidth / characterWidth) : value.length;
	};
	const observer = new ResizeObserver(updateCharacterLimit);
	observer.observe(currentFingerprintElement);
	updateCharacterLimit();
	return () => observer.disconnect();
});
</script>

{#if compact}
	<code
		bind:this={fingerprintElement}
		class="relative block w-full overflow-hidden rounded bg-surface-50-950 px-2 py-0.5 text-center font-mono text-xs leading-6 tracking-wide"
		title={value}
	>
		<span bind:this={valueElement} class="invisible absolute whitespace-nowrap">{value}</span>
		<span class="block whitespace-nowrap">{compactValue}</span>
	</code>
{:else}
	<code class="block w-full overflow-hidden rounded bg-surface-50-950 p-3 text-center font-mono text-sm leading-7 tracking-wide">
		{#each rows as row}
			<span class="block truncate">{row}</span>
		{/each}
	</code>
{/if}
