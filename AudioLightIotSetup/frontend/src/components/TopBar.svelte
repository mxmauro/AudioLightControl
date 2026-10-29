<script lang="ts">
import ChevronDownIcon from '@lucide/svelte/icons/chevron-down';
import ChevronUpIcon from '@lucide/svelte/icons/chevron-up';
import ChevronsDownIcon from '@lucide/svelte/icons/chevrons-down';
import XIcon from '@lucide/svelte/icons/x';
import { onMount } from 'svelte';

// -----------------------------------------------------------------------------

type WailsRuntime = {
	Quit: () => void;
	WindowIsMaximised: () => Promise<boolean>;
	WindowMaximise: () => void;
	WindowMinimise: () => void;
	WindowUnmaximise: () => void;
};
type WailsRuntimeWindow = Window & { runtime?: WailsRuntime };

// -----------------------------------------------------------------------------

let maximised = $state(false);
let runningInWails = $state(false);
let { onquit }: { onquit: () => void } = $props();

// -----------------------------------------------------------------------------

const getWailsRuntime = (): WailsRuntime | undefined => {
	// Access desktop-only window controls without assuming the page runs inside Wails.
	if (typeof window === 'undefined') {
		return undefined;
	}
	return (window as WailsRuntimeWindow).runtime;
};

const updateMaximisedState = async (): Promise<void> => {
	// Synchronize the visual maximize control with the native window state.
	const runtime = getWailsRuntime();
	if (!runtime) {
		maximised = false;
		return;
	}
	maximised = await runtime.WindowIsMaximised();
};

const minimise = (): void => {
	getWailsRuntime()?.WindowMinimise();
};

const toggleMaximise = async (): Promise<void> => {
	// Toggle the native desktop window and update local state only after the runtime is available.
	const runtime = getWailsRuntime();
	if (!runtime) {
		return;
	}
	if (await runtime.WindowIsMaximised()) {
		runtime.WindowUnmaximise();
		maximised = false;
		return;
	}
	runtime.WindowMaximise();
	maximised = true;
};

const quit = (): void => {
	onquit();
};

onMount(() => {
	// Enable native window controls only when this frontend is hosted by Wails.
	runningInWails = window.isWails();
	if (!runningInWails) {
		return;
	}
	void updateMaximisedState();
});
</script>

<header class="fixed inset-x-0 top-0 z-40 border-b border-surface-200-800 bg-surface-100-900" style="--wails-draggable: drag">
	<div class="flex h-14 w-full items-center justify-between px-3">
		<strong class="cursor-default text-lg" style="--wails-draggable: drag">AudioLight IoT Setup</strong>
		{#if runningInWails}
			<div class="flex items-center gap-1">
				<button class="btn btn-icon preset-filled-surface-500" type="button" aria-label="Minimise window" onclick={minimise}>
					<ChevronDownIcon />
				</button>
				<button
					class="btn btn-icon preset-filled-surface-500"
					type="button"
					aria-label={maximised ? 'Restore window' : 'Maximise window'}
					onclick={toggleMaximise}
				>
					{#if maximised}
						<ChevronsDownIcon />
					{:else}
						<ChevronUpIcon />
					{/if}
				</button>
				<button class="btn btn-icon preset-filled-error-500" type="button" aria-label="Close application" onclick={quit}>
					<XIcon />
				</button>
			</div>
		{/if}
	</div>
</header>
