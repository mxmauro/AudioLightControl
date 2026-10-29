<script lang="ts">
import LightbulbIcon from '@lucide/svelte/icons/lightbulb';
import LogOutIcon from '@lucide/svelte/icons/log-out';
import RotateCcwKeyIcon from '@lucide/svelte/icons/rotate-ccw-key';
import ServerCogIcon from '@lucide/svelte/icons/server-cog';
import SlidersHorizontalIcon from '@lucide/svelte/icons/sliders-horizontal';
import UploadIcon from '@lucide/svelte/icons/upload';
import UserMinusIcon from '@lucide/svelte/icons/user-minus';
import UserPlusIcon from '@lucide/svelte/icons/user-plus';
import UserRoundKeyIcon from '@lucide/svelte/icons/user-round-key';
import type { Client } from '@mxmauro/iot-comm.js';
import { Navigation } from '@skeletonlabs/skeleton-svelte';
import ConfirmationDialog from './ConfirmationDialog.svelte';

// -----------------------------------------------------------------------------

export type View =
	| 'lighting'
	| 'dmx-fixtures'
	| 'hostname'
	| 'firmware-upgrade'
	| 'create-user'
	| 'delete-user'
	| 'reset-user'
	| 'change-credentials';

// -----------------------------------------------------------------------------

const navigationGroups: { label: string; items: { view: View; label: string }[] }[] = [
	{ label: 'Device', items: [{ view: 'lighting', label: 'Lighting configuration' }] },
	{
		label: 'Users',
		items: [
			{ view: 'create-user', label: 'Create user' },
			{ view: 'delete-user', label: 'Delete user' },
			{ view: 'reset-user', label: 'Reset user credentials' },
			{ view: 'change-credentials', label: 'Change my credentials' }
		]
	},
	{
		label: 'Settings',
		items: [
			{ view: 'dmx-fixtures', label: 'DMX Fixtures editor' },
			{ view: 'hostname', label: 'mDNS Hostname' },
			{ view: 'firmware-upgrade', label: 'Firmware upgrade' }
		]
	}
];

// -----------------------------------------------------------------------------

let {
	client,
	activeView,
	isAdministrator,
	busy = false,
	onnavigate,
	onlogout,
	children
}: {
	client: Client;
	activeView: View;
	isAdministrator: boolean;
	busy?: boolean;
	onnavigate: (view: View) => void;
	onlogout: () => void;
	children: import('svelte').Snippet;
} = $props();
let confirmingLogout = $state(false);

// -----------------------------------------------------------------------------

const isNavigationDisabled = (view: View): boolean => {
	// Non-administrators may only manage their own credentials; mandatory credential rotation also takes precedence.
	return busy || (!isAdministrator && view !== 'change-credentials') || (client.mustChangeCredentials && view !== 'change-credentials');
};

const navigate = (event: MouseEvent, view: View): void => {
	// Keep navigation in the Svelte workspace after checking the current session restrictions.
	event.preventDefault();
	if (isNavigationDisabled(view)) {
		return;
	}
	onnavigate(view);
};

const requestLogout = (): void => {
	confirmingLogout = true;
};
</script>

<div class="flex h-full min-h-0 flex-col md:grid md:grid-cols-[17.5rem_1fr]">
	<aside class="shrink-0 border-b border-surface-200-800 bg-surface-100-900 md:min-h-0 md:overflow-y-auto md:border-r md:border-b-0">
		<Navigation layout="sidebar" class="flex h-full !w-full !p-0 flex-col gap-4">
			<Navigation.Content class="flex-1 p-6">
				{#each navigationGroups as group}
					<Navigation.Group>
						<Navigation.Label>{group.label}</Navigation.Label>
						<Navigation.Menu>
							{#each group.items as item}
								<Navigation.TriggerAnchor
									href={`#${item.view}`}
									class={isNavigationDisabled(item.view) ? 'pointer-events-none opacity-50' : undefined}
									aria-current={activeView === item.view ? 'page' : undefined}
									aria-disabled={isNavigationDisabled(item.view)}
									tabindex={isNavigationDisabled(item.view) ? -1 : undefined}
									onclick={(event: MouseEvent) => navigate(event, item.view)}
								>
									{#if item.view === 'lighting'}
										<LightbulbIcon class="shrink-0" />
									{:else if item.view === 'hostname'}
										<ServerCogIcon class="shrink-0" />
									{:else if item.view === 'dmx-fixtures'}
										<SlidersHorizontalIcon class="shrink-0" />
									{:else if item.view === 'firmware-upgrade'}
										<UploadIcon class="shrink-0" />
									{:else if item.view === 'create-user'}
										<UserPlusIcon class="shrink-0" />
									{:else if item.view === 'delete-user'}
										<UserMinusIcon class="shrink-0" />
									{:else if item.view === 'reset-user'}
										<RotateCcwKeyIcon class="shrink-0" />
									{:else}
										<UserRoundKeyIcon class="shrink-0" />
									{/if}
									<Navigation.TriggerText>{item.label}</Navigation.TriggerText>
								</Navigation.TriggerAnchor>
							{/each}
						</Navigation.Menu>
					</Navigation.Group>
				{/each}
			</Navigation.Content>
			<Navigation.Footer class="flex flex-col justify-end px-6 pb-4">
				<button class="btn preset-filled-error-500 w-full justify-start" type="button" disabled={busy} onclick={requestLogout}>
					<LogOutIcon />
					Logout
				</button>
			</Navigation.Footer>
		</Navigation>
	</aside>
	<main class="min-h-0 flex-1 overflow-y-auto p-4 md:p-8">
		{#if activeView === 'lighting' || activeView === 'dmx-fixtures'}
			<div class="mx-auto flex h-full min-h-0 w-full max-w-5xl flex-col">{@render children()}</div>
		{:else}
			<div class="mx-auto flex min-h-full w-full max-w-5xl flex-col justify-center gap-6">{@render children()}</div>
		{/if}
	</main>
</div>

{#if confirmingLogout}
	<ConfirmationDialog
		title="Close session"
		summary="Close the current session and return to the connection page?"
		busyLabel="Closing session…"
		{busy}
		oncancel={() => (confirmingLogout = false)}
		onconfirm={onlogout}
	/>
{/if}
