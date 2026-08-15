/**
 * Default installer page logic.
 *
 * Copy this directory, point `ui` at your copy, and change whatever you like —
 * it is an ordinary ES module using the documented SDK, with no framework and
 * no build step beyond what the CLI already does.
 */
import { installer, config, fs, proc, shell, win, ui } from 'blink-installer-ui';
import { playSplash } from './splash.js';

const $ = (id) => document.getElementById(id);

/**
 * Surfaces failures on screen.
 *
 * A page that throws during boot otherwise leaves the user staring at a
 * half-rendered installer with no clue why, and there is no console to open.
 */
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

window.addEventListener('error', (event) => fatal('uncaught error', event.error ?? event.message));
window.addEventListener('unhandledrejection', (event) => fatal('unhandled rejection', event.reason));

const state = {
  installDir: '',
  exeName: '',
  productName: 'Setup',
};

// ---------------------------------------------------------------------------
// Screens
// ---------------------------------------------------------------------------
function showScreen(name) {
  // Array.from, not for...of: this engine's NodeList has no [Symbol.iterator]
  // (that arrived in Chrome 51), so iterating one directly throws. Array.from
  // falls back to the array-like protocol and works either way.
  Array.from(document.querySelectorAll('.screen')).forEach((section) => {
    section.classList.toggle('is-active', section.dataset.screen === name);
  });
}

/**
 * The visible screen is a function of `mode`, which the NSIS script owns.
 *
 * Deriving it rather than switching screens as events arrive means a missed or
 * late `config` event cannot strand the user: the next sync puts the page back
 * where it belongs.
 */
const SCREEN_FOR_MODE = {
  install: 'welcome',
  installing: 'installing',
  done: 'done',
};

function applyMode(mode) {
  const screen = SCREEN_FOR_MODE[mode];
  if (!screen) return;
  showScreen(screen);
  if (mode === 'done') {
    $('done-subtitle').textContent = `${state.productName} 已安装到 ${state.installDir}`;
    win.setCloseGuard(false);
  }
}

// ---------------------------------------------------------------------------
// Window chrome
// ---------------------------------------------------------------------------
$('titlebar').addEventListener('mousedown', (event) => {
  if (event.target.closest('button')) return;
  win.startDrag();
});
$('btn-minimize').onclick = () => win.minimize();

// Route the X through us so a close mid-install can be confirmed rather than
// leaving a half-written install directory behind.
win.setCloseGuard(true);
installer.on('window-closing', async () => {
  const installing = document.querySelector('.screen[data-screen="installing"]').classList.contains('is-active');
  const answer = await ui.messageBox({
    title: state.productName,
    message: installing
      ? '安装正在进行中，确定要退出吗？'
      : '确定要退出安装程序吗？',
    buttons: 'yesNo',
    icon: 'question',
  });
  if (answer === 'yes') win.close(true);
});
$('btn-close').onclick = () => win.close(false);

// ---------------------------------------------------------------------------
// Welcome screen
// ---------------------------------------------------------------------------
function readableSize(bytes) {
  if (!Number.isFinite(bytes)) return '';
  const gb = bytes / 1024 ** 3;
  return gb >= 1 ? `${gb.toFixed(1)} GB` : `${(bytes / 1024 ** 2).toFixed(0)} MB`;
}

async function refreshSpace() {
  const hint = $('space-hint');
  try {
    const space = await fs.diskSpace(state.installDir);
    hint.textContent = `该磁盘可用空间 ${readableSize(space.available)}`;
    hint.classList.toggle('warn', space.available < 500 * 1024 * 1024);
  } catch {
    // An unreadable path is not worth blocking the install over — the Section
    // will report a real error if the directory genuinely cannot be used.
    hint.textContent = ' ';
    hint.classList.remove('warn');
  }
}

function setInstallDir(dir) {
  state.installDir = dir;
  $('install-dir').value = dir;
  config.set('installDir', dir);
  void refreshSpace();
}

$('btn-browse').onclick = async () => {
  const picked = await fs.pickDirectory({
    title: '选择安装位置',
    defaultPath: state.installDir,
  });
  if (picked) setInstallDir(`${picked}\\${state.productName}`);
};

$('opt-desktop').onchange = (event) => {
  config.set('createDesktopShortcut', event.target.checked);
};

$('btn-install').onclick = async () => {
  const button = $('btn-install');
  button.disabled = true;

  // If the app is already running it will hold locks on the files we are about
  // to overwrite, so deal with it before NSIS starts copying rather than
  // failing halfway through.
  if (state.exeName) {
    try {
      const running = await proc.exists(state.exeName);
      if (running.running) {
        const answer = await ui.messageBox({
          title: state.productName,
          message: `检测到 ${state.productName} 正在运行。\n需要关闭它才能继续安装。`,
          buttons: 'okCancel',
          icon: 'warning',
        });
        if (answer !== 'ok') {
          button.disabled = false;
          return;
        }
        await proc.kill({ name: state.exeName });
      }
    } catch (error) {
      console.warn('running-process check failed', error);
    }
  }

  showScreen('installing');
  await installer.begin();
};

// ---------------------------------------------------------------------------
// Progress
// ---------------------------------------------------------------------------
installer.on('progress', ({ percent }) => {
  $('progress-fill').style.width = `${percent}%`;
  $('progress-percent').textContent = `${percent}%`;
});

installer.on('log', ({ message }) => {
  $('install-detail').textContent = message;
});

installer.on('finish', () => {
  $('progress-fill').style.width = '100%';
  $('progress-percent').textContent = '100%';
});

installer.on('config', ({ key, value }) => {
  if (key === 'mode') applyMode(String(value));
  if (key === 'installDir' && value !== state.installDir) {
    state.installDir = String(value);
    $('install-dir').value = state.installDir;
  }
});

// Sent every time the page is shown, carrying the whole config. This is what
// catches the transition into the completion screen, which happens while the
// window is not pumping its own messages.
installer.on('sync', ({ config: snapshot }) => {
  if (snapshot.installDir) state.installDir = String(snapshot.installDir);
  applyMode(String(snapshot.mode || ''));
});

// ---------------------------------------------------------------------------
// Done screen
// ---------------------------------------------------------------------------
$('btn-finish').onclick = async () => {
  if ($('opt-launch').checked) {
    try {
      await installer.launch();
    } catch (error) {
      console.warn('launch failed', error);
    }
  }
  win.close(true);
};

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
function boot() {
  const all = config.all();
  state.productName = all.productName || 'Setup';
  state.exeName = all.exeName || '';
  state.installDir = all.installDir || '';

  document.title = state.productName;
  $('titlebar-name').textContent = state.productName;
  $('welcome-title').textContent = `安装 ${state.productName}`;
  $('welcome-subtitle').textContent = all.version ? `版本 ${all.version}` : '';
  $('logo-mark').textContent = state.productName.slice(0, 1).toUpperCase();
  $('logo-installing').textContent = $('logo-mark').textContent;

  if (all.website) {
    $('agreement').innerHTML =
      `点击“立即安装”即表示同意 <a href="#" id="link-website">用户协议</a>`;
    $('link-website').onclick = (event) => {
      event.preventDefault();
      void shell.openUrl(String(all.website));
    };
  }

  if (all.allowDirChange === '0') {
    $('btn-browse').disabled = true;
  }

  $('install-dir').value = state.installDir;
  void refreshSpace();
}

try {
  boot();
  // Runs only when the config enables it. Everything above is already wired,
  // so the user can interact the moment the card lands.
  void playSplash(state);
} catch (error) {
  fatal('boot() failed', error);
}
