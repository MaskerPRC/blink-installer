/**
 * The API an installer page talks to.
 *
 * The native side exposes three bare globals (__blinkInvoke, __blinkInvokeAsync,
 * __blinkDispatch). Everything here is a thin, typed wrapper over them.
 *
 * Wrapping them is what lets a page be an ordinary module: it imports what it
 * needs, gets types and completion, and never has to know the names of the
 * globals or hand-assemble the JSON they take.
 */

import {
  setDialogLabels,
  setDialogRenderer,
  showDialog,
  type DialogAnswer,
  type DialogButtons,
  type DialogIcon,
  type DialogOptions,
} from './dialog.js';

export {
  setDialogLabels,
  setDialogRenderer,
  showDialog,
  type DialogAnswer,
  type DialogButtons,
  type DialogIcon,
  type DialogOptions,
  type DialogRenderer,
} from './dialog.js';

declare global {
  interface Window {
    __blinkInvoke?(name: string, argsJson: string): string;
    __blinkInvokeAsync?(name: string, argsJson: string, callId: string): void;
    __blinkSetCloseGuard?(enabled: string): void;
    __blinkDispatch?(event: NativeEvent): void;
    __BLINK_DEFINES__?: Record<string, unknown>;
  }
}

interface NativeEvent {
  type: string;
  [key: string]: unknown;
}

interface Envelope<T> {
  ok: boolean;
  value?: T;
  error?: string;
}

export class AbilityError extends Error {
  constructor(
    readonly ability: string,
    message: string,
  ) {
    super(`${ability}: ${message}`);
    this.name = 'AbilityError';
  }
}

/** True when running inside the installer rather than a plain browser. */
export function isNative(): boolean {
  return typeof window !== 'undefined' && typeof window.__blinkInvoke === 'function';
}

function requireNative(ability: string): void {
  if (!isNative()) {
    throw new AbilityError(
      ability,
      'not running inside the installer — the native bridge is unavailable. ' +
        'Open the page through the built setup.exe, not in a browser.',
    );
  }
}

/** Calls an ability and returns immediately. Throws AbilityError on failure. */
export function invoke<T = unknown>(name: string, args: Record<string, unknown> = {}): T {
  requireNative(name);
  const raw = window.__blinkInvoke!(name, JSON.stringify(args));
  const envelope = JSON.parse(raw) as Envelope<T>;
  if (!envelope.ok) throw new AbilityError(name, envelope.error ?? 'unknown error');
  return envelope.value as T;
}

const pending = new Map<string, (envelope: Envelope<unknown>) => void>();
let nextCallId = 0;

/**
 * Calls an ability on a worker thread. Prefer this for anything that can block —
 * dialogs, process termination, nsis.call — so the window keeps repainting.
 */
export function invokeAsync<T = unknown>(
  name: string,
  args: Record<string, unknown> = {},
): Promise<T> {
  requireNative(name);
  return new Promise<T>((resolvePromise, rejectPromise) => {
    const callId = `bk${nextCallId++}`;
    pending.set(callId, (envelope) => {
      if (envelope.ok) resolvePromise(envelope.value as T);
      else rejectPromise(new AbilityError(name, envelope.error ?? 'unknown error'));
    });
    window.__blinkInvokeAsync!(name, JSON.stringify(args), callId);
  });
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

export interface InstallerEvents {
  /** NSIS install progress, 0-100. */
  progress: { percent: number };
  /** One line from the NSIS detail log, e.g. "Extracting: app.exe". */
  log: { message: string };
  /** Install reached 100%. */
  finish: Record<string, never>;
  /** A config key changed, from any of the three sides. */
  config: { key: string; value: unknown };
  /**
   * The whole config, pushed whenever the page is (re)shown. Use it to derive
   * the current screen rather than relying on having caught every change.
   */
  sync: { config: Record<string, unknown> };
  /** The user tried to close the window while a close guard is set. */
  'window-closing': Record<string, never>;
}

type Listener<K extends keyof InstallerEvents> = (payload: InstallerEvents[K]) => void;

const listeners = new Map<string, Set<(payload: unknown) => void>>();

function emit(type: string, payload: unknown): void {
  const set = listeners.get(type);
  if (!set) return;
  // Snapshot first: a handler is allowed to unsubscribe itself.
  Array.from(set).forEach((listener) => {
    try {
      listener(payload);
    } catch (error) {
      // One bad handler must not stop the install from reporting progress.
      console.error(`blink-installer: listener for "${type}" threw`, error);
    }
  });
}

// Single entry point the native side calls. Installed on import so a page never
// has to remember to wire it up.
if (typeof window !== 'undefined') {
  window.__blinkDispatch = (event: NativeEvent) => {
    if (event.type === 'invoke-result') {
      const callId = event['callId'] as string;
      const resolver = pending.get(callId);
      if (resolver) {
        pending.delete(callId);
        resolver({
          ok: event['ok'] as boolean,
          value: event['value'],
          error: event['error'] as string | undefined,
        });
      }
      return;
    }
    emit(event.type, event);
  };
}

// ---------------------------------------------------------------------------
// Typed surface
// ---------------------------------------------------------------------------

export const installer = {
  on<K extends keyof InstallerEvents>(type: K, listener: Listener<K>): () => void {
    let set = listeners.get(type);
    if (!set) {
      set = new Set();
      listeners.set(type, set);
    }
    set.add(listener as (payload: unknown) => void);
    return () => set!.delete(listener as (payload: unknown) => void);
  },

  /** Build-time values baked in by the CLI (product name, version, publisher). */
  defines(): Record<string, unknown> {
    return (typeof window !== 'undefined' && window.__BLINK_DEFINES__) || {};
  },

  /**
   * Starts the installation.
   *
   * Runs the script's `beginInstall` function — which reads back the directory
   * you put in config — then hands control to NSIS so it can run the Section.
   * The window stays on screen; progress arrives as `progress` events.
   */
  async begin(): Promise<void> {
    await nsis.call('beginInstall');
    invoke('installer.next');
  },

  /**
   * Starts the uninstall. The uninstaller's mirror of `begin()`.
   */
  async beginUninstall(): Promise<void> {
    await nsis.call('beginUninstall');
    invoke('installer.next');
  },

  /** Aborts and quits the installer. */
  async cancel(): Promise<void> {
    await nsis.call('cancel');
  },

  /** Launches the installed application. */
  async launch(): Promise<void> {
    await nsis.call('launchApp');
  },
};

export const config = {
  get<T = unknown>(key: string): T {
    return invoke<T>('config.get', { key });
  },
  set(key: string, value: unknown): void {
    invoke('config.set', { key, value });
  },
  all<T = Record<string, unknown>>(): T {
    return invoke<T>('config.all');
  },
  persist(): void {
    invoke('config.persist');
  },
};

export interface DiskSpace {
  total: number;
  free: number;
  /** What this user may consume; lower than `free` when quotas apply. */
  available: number;
}

export interface DriveInfo extends Partial<DiskSpace> {
  root: string;
  letter: string;
}

export const fs = {
  /**
   * Opens a folder picker. Resolves to null when the user cancels.
   *
   * `legacy` forces the pre-Vista tree box for this call, overriding
   * `install.legacyFolderPicker`. Only worth reaching for against a known
   * failure of the explorer dialog on a particular machine.
   */
  pickDirectory(
    options: { title?: string; defaultPath?: string; legacy?: boolean } = {},
  ) {
    return invokeAsync<string | null>('fs.pickDirectory', options);
  },
  diskSpace(path: string) {
    return invokeAsync<DiskSpace>('fs.diskSpace', { path });
  },
  drives() {
    return invokeAsync<DriveInfo[]>('fs.drives');
  },
  exists(path: string) {
    return invokeAsync<false | { exists: true; directory: boolean }>('fs.exists', { path });
  },
};

export const proc = {
  exists(name: string) {
    return invokeAsync<{ running: boolean; pids: number[] }>('proc.exists', { name });
  },
  /** Terminates and waits for exit, so files can safely be replaced after. */
  kill(target: { pid?: number; name?: string; timeoutMs?: number }) {
    return invokeAsync<{ killed: number; failures: unknown[] }>('proc.kill', target);
  },
};

export const shell = {
  openUrl(url: string) {
    return invokeAsync<boolean>('shell.openUrl', { url });
  },
  exec(options: { path: string; args?: string; cwd?: string; elevated?: boolean; hidden?: boolean }) {
    return invokeAsync<boolean>('shell.exec', options);
  },
  showInFolder(path: string) {
    return invokeAsync<boolean>('shell.showInFolder', { path });
  },
};

export interface Rect {
  x: number;
  y: number;
  width: number;
  height: number;
}

export const sys = {
  /**
   * Geometry of the monitor the installer is on. `work` excludes the taskbar
   * and is what a full-screen splash should cover.
   */
  screen() {
    return invokeAsync<{ work: Rect; full: Rect; primary: boolean }>('sys.screen');
  },
  osVersion() {
    return invokeAsync<{
      major: number;
      minor: number;
      build: number;
      name: string;
      is64bit: boolean;
    }>('sys.osVersion');
  },
  fonts() {
    return invokeAsync<string[]>('sys.fonts');
  },
  expandEnv(value: string) {
    return invokeAsync<string>('sys.expandEnv', { value });
  },
};

export type RegistryRoot = 'HKLM' | 'HKCU' | 'HKCR' | 'HKU';
export type RegistryView = '64' | '32' | 'default';

export const registry = {
  read(options: { root?: RegistryRoot; key: string; name: string; view?: RegistryView }) {
    return invokeAsync<string | number | null>('reg.read', options);
  },
  write(options: {
    root?: RegistryRoot;
    key: string;
    name: string;
    value: string | number;
    view?: RegistryView;
  }) {
    return invokeAsync<boolean>('reg.write', options);
  },
  delete(options: { root?: RegistryRoot; key: string; name?: string; view?: RegistryView }) {
    return invokeAsync<boolean>('reg.delete', options);
  },
};

export const win = {
  close(force = false): void {
    invoke('win.close', { force });
  },
  minimize(): void {
    invoke('win.minimize');
  },
  resize(width: number, height: number): void {
    invoke('win.resize', { width, height });
  },
  /**
   * Moves and resizes in one step.
   *
   * Use this rather than resize+center when leaving the splash: two calls show
   * the window at an intermediate rectangle for a frame, which on a layered
   * window reads as a visible jump.
   */
  setBounds(bounds: Rect): void {
    invoke('win.setBounds', { ...bounds });
  },
  center(): void {
    invoke('win.center');
  },
  /** Call from mousedown on your title bar to drag the frameless window. */
  startDrag(): void {
    invoke('win.startDrag');
  },
  /**
   * Turns the title-bar X into a `window-closing` event so you can confirm
   * first, instead of the window vanishing mid-install.
   */
  setCloseGuard(enabled = true): void {
    invoke('win.setCloseGuard', { enabled });
  },
};

export const ui = {
  /**
   * Asks the user something, drawn in the page.
   *
   * Styled with CSS like everything else — see dialog.ts for the three levels
   * of control. Use `messageBoxNative` only where the page cannot draw.
   */
  messageBox(options: DialogOptions): Promise<DialogAnswer> {
    return showDialog(options);
  },

  /**
   * The Win32 MessageBox.
   *
   * Kept for the case the in-page dialog cannot serve: a failure early enough
   * that the document is not ready, or an error in the page itself, where
   * asking the page to draw the error is asking the broken thing to report
   * its own breakage.
   */
  messageBoxNative(options: {
    message: string;
    title?: string;
    buttons?: DialogButtons;
    icon?: DialogIcon;
  }): Promise<DialogAnswer> {
    return invokeAsync<DialogAnswer>('ui.messageBox', options);
  },

  /** Global button text, e.g. `ui.setDialogLabels({ ok: '知道了' })`. */
  setDialogLabels,
  /** Replace the dialog implementation entirely; null restores the default. */
  setDialogRenderer,

  taskbarProgress(percent: number, indeterminate = false): void {
    invoke('ui.taskbarProgress', { percent, indeterminate });
  },
};

export const nsis = {
  /** Runs a script function registered with blinkkit::RegisterAbility. */
  call(name: string) {
    return invokeAsync<boolean>('nsis.call', { name });
  },
  /** Names the script registered, so optional steps can be feature-detected. */
  functions() {
    return invokeAsync<string[]>('nsis.functions');
  },
};
