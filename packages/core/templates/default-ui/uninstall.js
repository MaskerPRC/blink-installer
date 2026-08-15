/**
 * Default uninstaller page.
 *
 * Same SDK, same stylesheet, same three-screen shape as the installer — the
 * point is that uninstalling does not suddenly drop the user into a dialog from
 * a different decade.
 */
import { installer, config, nsis, win, ui } from 'blink-installer-ui';

const $ = (id) => document.getElementById(id);

const state = { productName: 'Setup', installDir: '' };

function fatal(context, error) {
  const message = error && error.stack ? error.stack : String(error);
  console.error(context, error);
  let box = $('fatal');
  if (!box) {
    box = document.createElement('pre');
    box.id = 'fatal';
    box.style.cssText =
      'position:fixed;left:14px;right:14px;bottom:14px;max-height:45%;overflow:auto;' +
      'margin:0;padding:12px;border-radius:8px;background:#2a1416;color:#ffb4ac;' +
      'font:11px/1.5 Consolas,monospace;white-space:pre-wrap;z-index:99;' +
      '-webkit-user-select:text;user-select:text';
    document.body.appendChild(box);
  }
  box.textContent += `${context}\n${message}\n\n`;
}
window.addEventListener('error', (e) => fatal('uncaught error', e.error ?? e.message));
window.addEventListener('unhandledrejection', (e) => fatal('unhandled rejection', e.reason));

function showScreen(name) {
  // Array.from rather than for...of — this engine's NodeList is not iterable.
  Array.from(document.querySelectorAll('.screen')).forEach((section) => {
    section.classList.toggle('is-active', section.dataset.screen === name);
  });
}

// The screen is derived from `mode`, which the uninstaller script owns, so a
// missed change event cannot leave the page stuck on a finished progress bar.
const SCREEN_FOR_MODE = {
  uninstall: 'confirm',
  uninstalling: 'removing',
  uninstalled: 'done',
};

function applyMode(mode) {
  const screen = SCREEN_FOR_MODE[mode];
  if (!screen) return;
  showScreen(screen);
  if (mode === 'uninstalled') {
    $('done-subtitle').textContent = `${state.productName} 已从这台电脑移除`;
    win.setCloseGuard(false);
  }
}

$('titlebar').addEventListener('mousedown', (event) => {
  if (event.target.closest('button')) return;
  win.startDrag();
});
$('btn-minimize').onclick = () => win.minimize();

win.setCloseGuard(true);
installer.on('window-closing', async () => {
  const removing = document
    .querySelector('.screen[data-screen="removing"]')
    .classList.contains('is-active');
  if (!removing) {
    win.close(true);
    return;
  }
  const answer = await ui.messageBox({
    title: state.productName,
    message: '卸载正在进行中，强行退出可能留下残留文件。确定要退出吗？',
    buttons: 'yesNo',
    icon: 'warning',
  });
  if (answer === 'yes') win.close(true);
});
$('btn-close').onclick = () => win.close(false);

$('btn-cancel').onclick = async () => {
  await nsis.call('cancel');
};

$('btn-uninstall').onclick = async () => {
  $('btn-uninstall').disabled = true;
  $('btn-cancel').disabled = true;

  // Record the reason before the store is torn down with the install.
  const reason = $('reason').value;
  if (reason) config.set('uninstallReason', reason);

  showScreen('removing');
  await installer.beginUninstall();
};

installer.on('progress', ({ percent }) => {
  $('progress-fill').style.width = `${percent}%`;
  $('progress-percent').textContent = `${percent}%`;
});

installer.on('log', ({ message }) => {
  $('remove-detail').textContent = message;
});

installer.on('config', ({ key, value }) => {
  if (key === 'mode') applyMode(String(value));
});

installer.on('sync', ({ config: snapshot }) => {
  applyMode(String(snapshot.mode || ''));
});

$('btn-finish').onclick = () => win.close(true);

function boot() {
  const all = config.all();
  state.productName = all.productName || 'Setup';
  state.installDir = all.installDir || '';

  document.title = `卸载 ${state.productName}`;
  $('titlebar-name').textContent = `卸载 ${state.productName}`;
  $('confirm-title').textContent = `卸载 ${state.productName}`;
  $('confirm-subtitle').textContent = state.installDir
    ? `将从 ${state.installDir} 移除`
    : '';
  $('logo-mark').textContent = state.productName.slice(0, 1).toUpperCase();
  $('logo-removing').textContent = $('logo-mark').textContent;
}

try {
  boot();
} catch (error) {
  fatal('boot() failed', error);
}
