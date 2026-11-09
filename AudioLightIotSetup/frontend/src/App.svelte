<script lang="ts">
import type { Client, InputConfig, LightingConfig } from '@mxmauro/iot-comm.js';
import { createToaster, LocaleProvider, Toast } from '@skeletonlabs/skeleton-svelte';
import { Events, Window } from '@wailsio/runtime';
import { onMount } from 'svelte';
import { CloseControlService } from './bindings/AudioLightIotSetup/app';
import FingerprintDialog from './components/FingerprintDialog.svelte';
import TopBar from './components/TopBar.svelte';
import WorkspaceShell, { type View } from './components/WorkspaceShell.svelte';
import { connectClient } from './lib/session';
import { errorNotice, type Notice } from './lib/ui';
import ChangeCredentialsPage from './pages/ChangeCredentialsPage.svelte';
import ConnectionPage from './pages/ConnectionPage.svelte';
import CreateUserPage from './pages/CreateUserPage.svelte';
import DeleteUserPage from './pages/DeleteUserPage.svelte';
import DmxFixturesEditorPage from './pages/DmxFixturesEditorPage.svelte';
import FirmwareUpgradePage from './pages/FirmwareUpgradePage.svelte';
import HostnamePage from './pages/HostnamePage.svelte';
import InputConfigurationPage from './pages/InputConfigurationPage.svelte';
import LightingConfigurationPage from './pages/LightingConfigurationPage.svelte';
import ResetUserCredentialsPage from './pages/ResetUserCredentialsPage.svelte';
import TrustedDevicesPage from './pages/TrustedDevicesPage.svelte';

// -----------------------------------------------------------------------------

type LeaveGuard = () => Promise<boolean>;

// -----------------------------------------------------------------------------

const toaster = createToaster({ placement: 'bottom-end', duration: 5000 });

let client = $state<Client | null>(null);
let disconnectingClient = $state<Client | null>(null);
let activeView = $state<View>('lighting');
let busy = $state(false);
let firmwareUploading = $state(false);
let isAdministrator = $state(false);
let inputConfiguration = $state<InputConfig | null>(null);
let lightingConfiguration = $state<LightingConfig | null>(null);
let fingerprintRequest = $state<{ fingerprint: string; changed: boolean; resolve: (accepted: boolean) => void } | null>(null);
let connectionView = $state<'connect' | 'trusted-devices'>('connect');
let leaveGuard = $state<LeaveGuard | null>(null);

// -----------------------------------------------------------------------------

const confirmFingerprint = (fingerprint: string, changed: boolean): Promise<boolean> => {
	return new Promise((resolve) => (fingerprintRequest = { fingerprint, changed, resolve }));
};

const readConfiguration = async (activeClient: Client): Promise<{ input: InputConfig; lighting: LightingConfig }> => {
	const [input, lighting] = await Promise.all([activeClient.getInputConfigCommand(), activeClient.getLightingConfigCommand()]);
	return { input, lighting };
};

const refreshConfiguration = async (): Promise<void> => {
	if (!client) {
		throw new Error('No active device session.');
	}
	const activeClient = client;
	const configuration = await readConfiguration(activeClient);
	if (client === activeClient) {
		inputConfiguration = configuration.input;
		lightingConfiguration = configuration.lighting;
	}
};

const finishFingerprint = (accepted: boolean): void => {
	fingerprintRequest?.resolve(accepted);
	fingerprintRequest = null;
};

const showNotice = (nextNotice: Notice): void => {
	toaster.create({
		description: nextNotice.fingerprint ? `${nextNotice.message}\n${nextNotice.fingerprint}` : nextNotice.message,
		type: nextNotice.tone,
		closable: true
	});
};

const registerLeaveGuard = (guard: LeaveGuard): (() => void) => {
	// Keep only the active page's guard so navigation checks the page that is about to unmount.
	leaveGuard = guard;
	return () => {
		if (leaveGuard === guard) {
			leaveGuard = null;
		}
	};
};

const canLeave = async (): Promise<boolean> => {
	return (await leaveGuard?.()) ?? true;
};

const navigate = async (view: View): Promise<void> => {
	// Ask the current view before replacing it with another workspace page.
	if (view === activeView || !(await canLeave())) {
		return;
	}
	activeView = view;
};

const requestClose = async (): Promise<void> => {
	// Permit the single native close only after the active page has approved it.
	if (await canLeave()) {
		await CloseControlService.AllowClose();
		await Window.Close();
	}
};

const connect = async (host: string, username: string, privateKey: ArrayBuffer): Promise<void> => {
	// Establish the device session, then keep global UI state aligned with its lifetime and credential policy.
	busy = true;
	try {
		const nextClient = await connectClient(host, username, privateKey, confirmFingerprint);
		let configuration: { input: InputConfig; lighting: LightingConfig };
		try {
			configuration = await readConfiguration(nextClient);
		} catch (error) {
			try {
				await nextClient.close();
			} catch {
				// A failed configuration read can already have closed the temporary session.
			}
			throw error;
		}
		client = nextClient;
		inputConfiguration = configuration.input;
		lightingConfiguration = configuration.lighting;
		isAdministrator = nextClient.isAdmin;
		nextClient.on('close', (event) => {
			if (client === nextClient) {
				client = null;
				isAdministrator = false;
				inputConfiguration = null;
				lightingConfiguration = null;
				if (disconnectingClient !== nextClient) {
					showNotice({ tone: 'error', message: `Connection closed${event.reason ? `: ${event.reason}` : '.'}` });
				}
			}
		});
		if (nextClient.mustChangeCredentials) {
			activeView = 'change-credentials';
		} else {
			activeView = isAdministrator ? 'lighting' : 'change-credentials';
		}
	} catch (error) {
		isAdministrator = false;
		inputConfiguration = null;
		lightingConfiguration = null;
		showNotice(errorNotice(error));
	} finally {
		busy = false;
	}
};

const disconnect = async (): Promise<void> => {
	// Close the active session and return the workspace to its connection state.
	if (!client || !(await canLeave())) {
		return;
	}
	const activeClient = client;
	busy = true;
	disconnectingClient = activeClient;
	try {
		await activeClient.close();
		if (client === activeClient) {
			client = null;
			isAdministrator = false;
			inputConfiguration = null;
			lightingConfiguration = null;
		}
		showNotice({ tone: 'info', message: 'Disconnected from device.' });
	} catch (error) {
		showNotice(errorNotice(error));
	} finally {
		if (disconnectingClient === activeClient) {
			disconnectingClient = null;
		}
		busy = false;
	}
};

const reboot = async (): Promise<void> => {
	if (!client || !(await canLeave())) {
		return;
	}
	const activeClient = client;
	busy = true;
	disconnectingClient = activeClient;
	try {
		await activeClient.rebootCommand();
		if (client === activeClient) {
			client = null;
			isAdministrator = false;
			inputConfiguration = null;
			lightingConfiguration = null;
		}
		try {
			await activeClient.close();
		} catch {
			// The reboot normally closes the WebSocket before the explicit local close completes.
		}
		showNotice({ tone: 'info', message: 'Device is rebooting. Reconnect when it is available.' });
	} catch (error) {
		showNotice(errorNotice(error));
	} finally {
		if (disconnectingClient === activeClient) {
			disconnectingClient = null;
		}
		busy = false;
	}
};

const completeFirmwareUpgrade = async (): Promise<void> => {
	// The device may restart after accepting its image, so leave the workspace even if its close request cannot complete.
	if (!client) {
		return;
	}
	const activeClient = client;
	disconnectingClient = activeClient;
	try {
		await activeClient.close();
	} catch {
		// Firmware activation can close the session before the explicit client close reaches the device.
	} finally {
		if (client === activeClient) {
			client = null;
			isAdministrator = false;
			inputConfiguration = null;
			lightingConfiguration = null;
		}
		if (disconnectingClient === activeClient) {
			disconnectingClient = null;
		}
	}
};

onMount(() => {
	// Wails asks the frontend to resolve pending edits before allowing native window closure.
	return Events.On('app:close-requested', () => void requestClose());
});
</script>

<LocaleProvider locale="en-US">
	<TopBar />

	<div class="h-screen overflow-hidden pt-14">
		{#if !client}
			{#if connectionView === 'connect'}
				<ConnectionPage {busy} onconnect={connect} ontrusteddevices={() => (connectionView = 'trusted-devices')} />
			{:else}
				<TrustedDevicesPage onback={() => (connectionView = 'connect')} />
			{/if}
		{:else}
			<WorkspaceShell
				{client}
				{activeView}
				{isAdministrator}
				busy={busy || firmwareUploading}
				onnavigate={(view) => void navigate(view)}
				onlogout={() => void disconnect()}
				onreboot={() => void reboot()}
			>
				{#if client.mustChangeCredentials}
					<p class="rounded preset-filled-warning-500 p-3" role="alert">
						This account must change its credentials before other device actions are available.
					</p>
				{/if}
				{#if activeView === 'input' && inputConfiguration && lightingConfiguration}
					<InputConfigurationPage
						{client}
						{inputConfiguration}
						{lightingConfiguration}
						onconfigurationchange={(configuration) => (inputConfiguration = configuration)}
						onrefresh={refreshConfiguration}
						onnotice={showNotice}
						{registerLeaveGuard}
					/>
				{:else if activeView === 'lighting' && inputConfiguration && lightingConfiguration}
					<LightingConfigurationPage
						{client}
						{inputConfiguration}
						{lightingConfiguration}
						onconfigurationchange={(configuration) => (lightingConfiguration = configuration)}
						onrefresh={refreshConfiguration}
						onnotice={showNotice}
						{registerLeaveGuard}
					/>
				{:else if activeView === 'dmx-fixtures'}
					<DmxFixturesEditorPage onnotice={showNotice} {registerLeaveGuard} />
				{:else if activeView === 'hostname'}
					<HostnamePage {client} onnotice={showNotice} />
				{:else if activeView === 'firmware-upgrade'}
					<FirmwareUpgradePage
						{client}
						onnotice={showNotice}
						oncomplete={completeFirmwareUpgrade}
						onuploadingchange={(uploading) => (firmwareUploading = uploading)}
						{registerLeaveGuard}
					/>
				{:else if activeView === 'create-user'}
					<CreateUserPage {client} onnotice={showNotice} />
				{:else if activeView === 'delete-user'}
					<DeleteUserPage {client} onnotice={showNotice} />
				{:else if activeView === 'reset-user'}
					<ResetUserCredentialsPage {client} onnotice={showNotice} />
				{:else}
					<ChangeCredentialsPage {client} onnotice={showNotice} />
				{/if}
			</WorkspaceShell>
		{/if}
	</div>

	<Toast.Group {toaster} class="fixed right-4 bottom-4 z-50 flex w-[min(22rem,calc(100vw-2rem))] flex-col gap-3">
		{#snippet children(toast)}
			<Toast {toast} class="w-full rounded preset-filled-surface-700 px-3 py-2 shadow-xl">
				<div class="grid w-full grid-cols-[minmax(0,1fr)_auto] items-center gap-2">
					<Toast.Description class="min-w-0 whitespace-pre-line text-sm leading-5">{toast.description}</Toast.Description>
					<Toast.CloseTrigger class="btn-icon-sm justify-self-end" aria-label="Dismiss notification" />
				</div>
			</Toast>
		{/snippet}
	</Toast.Group>
</LocaleProvider>

{#if fingerprintRequest}
	<FingerprintDialog
		fingerprint={fingerprintRequest.fingerprint}
		changed={fingerprintRequest.changed}
		oncancel={() => finishFingerprint(false)}
		ontrust={() => finishFingerprint(true)}
	/>
{/if}
