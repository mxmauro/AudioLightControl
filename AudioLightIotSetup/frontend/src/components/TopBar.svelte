<script lang="ts">
import ChevronDownIcon from '@lucide/svelte/icons/chevron-down';
import ChevronUpIcon from '@lucide/svelte/icons/chevron-up';
import ChevronsDownIcon from '@lucide/svelte/icons/chevrons-down';
import XIcon from '@lucide/svelte/icons/x';
import { Window } from '@wailsio/runtime';
import { onMount } from 'svelte';

// -----------------------------------------------------------------------------

type WailsWindow = globalThis.Window & { wails?: unknown };

// -----------------------------------------------------------------------------

let maximised = $state(false);
let runningInWails = $state(false);

// -----------------------------------------------------------------------------

const updateMaximisedState = async (): Promise<void> => {
	// Synchronize the visual maximize control with the native window state.
	maximised = await Window.IsMaximised();
};

const minimise = (): void => {
	void Window.Minimise();
};

const toggleMaximise = async (): Promise<void> => {
	// Toggle the native desktop window and keep the icon synchronized with the resulting state.
	if (await Window.IsMaximised()) {
		await Window.UnMaximise();
		maximised = false;
		return;
	}
	await Window.Maximise();
	maximised = true;
};

const quit = (): void => {
	// Closing through the runtime enters the same native close guard as Alt+F4 and the window's close button.
	void Window.Close();
};

const updateRuntimeAvailability = (): void => {
	// Wails injects window.wails before page scripts; the event covers a runtime that finishes configuring after mount.
	runningInWails = (window as WailsWindow).wails !== undefined;
	if (runningInWails) {
		void updateMaximisedState();
	}
};

onMount(() => {
	// Enable native window controls only when this frontend is hosted by Wails.
	updateRuntimeAvailability();
	window.addEventListener('wails:runtime-config-ready', updateRuntimeAvailability);
	return () => window.removeEventListener('wails:runtime-config-ready', updateRuntimeAvailability);
});
</script>

<header class="fixed inset-x-0 top-0 z-40 border-b border-surface-200-800 bg-surface-100-900" style="--wails-draggable: drag">
	<div class="flex h-14 w-full items-center justify-between px-3">
		<strong class="cursor-default text-lg" style="--wails-draggable: drag">AudioLight IoT Setup</strong>
		{#if runningInWails}
			<div class="flex items-center gap-1" style="--wails-draggable: no-drag">
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
