/**
 * The in-page replacement for Win32's MessageBox.
 *
 * A native message box is the one thing that will always give away that an
 * installer is a Win32 program wearing a web page: grey chrome, system font,
 * OS-drawn buttons, dropped in the middle of a dark rounded card. It also
 * cannot be themed, cannot animate, and on a transparent window it appears
 * detached from the surface it belongs to.
 *
 * So this draws the dialog in the page. Three levels of control, in the order
 * most people need them:
 *
 *   1. Do nothing. There are default styles and they match a dark card.
 *   2. Override CSS custom properties (--bk-dialog-*) for colours and radius.
 *   3. Restyle or replace `.bk-dialog*` classes outright, or hand over a whole
 *      renderer with `setDialogRenderer` and draw it yourself.
 *
 * The default stylesheet is injected as the *first* thing in <head>, so any
 * rule the page writes wins on source order without needing !important.
 *
 * Engine constraints (miniblink, Blink ~57) apply here as everywhere: no
 * `inset`, no flex `gap`, no `:is()`, and no Object.entries / String.padStart.
 */

export type DialogAnswer = 'ok' | 'cancel' | 'yes' | 'no' | 'retry';
export type DialogButtons =
  | 'ok'
  | 'okCancel'
  | 'yesNo'
  | 'yesNoCancel'
  | 'retryCancel';
export type DialogIcon = 'info' | 'warning' | 'error' | 'question' | 'none';

export interface DialogOptions {
  message: string;
  title?: string;
  buttons?: DialogButtons;
  icon?: DialogIcon;
  /** Per-dialog button text, e.g. `{ yes: 'Uninstall' }`. */
  labels?: { [K in DialogAnswer]?: string };
  /** Extra class on the dialog root, when one dialog needs to look different. */
  className?: string;
}

/** Draw the dialog yourself; resolve with the answer. */
export type DialogRenderer = (options: DialogOptions) => Promise<DialogAnswer>;

const BUTTON_SETS: { [K in DialogButtons]: DialogAnswer[] } = {
  ok: ['ok'],
  okCancel: ['ok', 'cancel'],
  yesNo: ['yes', 'no'],
  yesNoCancel: ['yes', 'no', 'cancel'],
  retryCancel: ['retry', 'cancel'],
};

/**
 * Escape closes the dialog, but only where that is not destructive: on a
 * yes/no question there is no safe assumption, so Escape does nothing and the
 * user has to answer.
 */
const ESCAPE_ANSWER: { [K in DialogButtons]: DialogAnswer | null } = {
  ok: 'ok',
  okCancel: 'cancel',
  yesNo: null,
  yesNoCancel: 'cancel',
  retryCancel: 'cancel',
};

const GLYPHS: { [K in DialogIcon]: string } = {
  info: 'i',
  warning: '!',
  error: '×',
  question: '?',
  none: '',
};

const EN: { [K in DialogAnswer]: string } = {
  ok: 'OK',
  cancel: 'Cancel',
  yes: 'Yes',
  no: 'No',
  retry: 'Retry',
};

const ZH: { [K in DialogAnswer]: string } = {
  ok: '确定',
  cancel: '取消',
  yes: '是',
  no: '否',
  retry: '重试',
};

/**
 * The native message box got OS-localised buttons for free and this does not,
 * so pick a default from the engine's locale rather than shipping English to
 * everyone. Anything beyond these two goes through setDialogLabels.
 */
function defaultLabels(): { [K in DialogAnswer]: string } {
  const language = (navigator && navigator.language) || '';
  return language.toLowerCase().indexOf('zh') === 0 ? ZH : EN;
}

let labelOverrides: { [K in DialogAnswer]?: string } = {};
let customRenderer: DialogRenderer | null = null;
let stylesInjected = false;

/** Global button text, e.g. `setDialogLabels({ ok: 'Got it' })`. */
export function setDialogLabels(labels: { [K in DialogAnswer]?: string }): void {
  labelOverrides = labels || {};
}

/** Replace the whole dialog implementation. Pass null to restore the default. */
export function setDialogRenderer(renderer: DialogRenderer | null): void {
  customRenderer = renderer;
}

const DEFAULT_CSS = `
.bk-dialog-mask {
  position: absolute;
  top: 0; right: 0; bottom: 0; left: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bk-dialog-scrim, rgba(0, 0, 0, 0.55));
  opacity: 0;
  transition: opacity 160ms ease;
  z-index: 9000;
}
.bk-dialog-mask.bk-is-open { opacity: 1; }

.bk-dialog {
  width: var(--bk-dialog-width, 340px);
  max-width: 82%;
  padding: 22px 24px 18px;
  border: 1px solid var(--bk-dialog-border, rgba(255, 255, 255, 0.12));
  border-radius: var(--bk-dialog-radius, 14px);
  background: var(--bk-dialog-surface, #1a2226);
  color: var(--bk-dialog-text, #e8f2ec);
  box-shadow: var(--bk-dialog-shadow, 0 22px 60px rgba(0, 0, 0, 0.6));
  text-align: center;
  transform: translateY(10px) scale(0.97);
  transition: transform 190ms ease;
}
.bk-dialog-mask.bk-is-open .bk-dialog { transform: translateY(0) scale(1); }

.bk-dialog-icon {
  /* Must be blockified: width and height do not apply to an inline box, so an
     <i> left as-is collapses to the width of its glyph and the round badge
     comes out as a narrow ellipse. */
  display: block;
  width: 44px;
  height: 44px;
  margin: 0 auto 12px;
  border-radius: 50%;
  font-size: 23px;
  font-weight: 700;
  font-style: normal;
  line-height: 44px;
  color: #fff;
  background: var(--bk-dialog-accent, #12b866);
}
.bk-dialog-icon.bk-is-warning { background: var(--bk-dialog-warning, #d99b31); }
.bk-dialog-icon.bk-is-error   { background: var(--bk-dialog-error, #d0574f); }

.bk-dialog-title {
  margin: 0 0 6px;
  font-size: 16px;
  font-weight: 600;
}
.bk-dialog-message {
  margin: 0;
  font-size: 14px;
  line-height: 1.65;
  color: var(--bk-dialog-text-dim, #a8bdb2);
  word-wrap: break-word;
}

.bk-dialog-actions {
  display: flex;
  justify-content: center;
  margin-top: 20px;
}
.bk-dialog-btn {
  height: 36px;
  min-width: 88px;
  padding: 0 18px;
  margin: 0 5px;
  border: 1px solid var(--bk-dialog-border, rgba(255, 255, 255, 0.12));
  border-radius: var(--bk-dialog-btn-radius, 9px);
  background: transparent;
  color: inherit;
  font: inherit;
  font-size: 13px;
  cursor: pointer;
}
.bk-dialog-btn:hover { background: rgba(255, 255, 255, 0.08); }
.bk-dialog-btn.bk-is-default {
  border-color: transparent;
  background: var(--bk-dialog-accent, #12b866);
  color: var(--bk-dialog-accent-text, #fff);
  font-weight: 600;
}
.bk-dialog-btn.bk-is-default:hover { filter: brightness(1.1); }
`;

function injectStyles(): void {
  if (stylesInjected) return;
  stylesInjected = true;
  const style = document.createElement('style');
  style.setAttribute('data-bk-dialog', '');
  style.appendChild(document.createTextNode(DEFAULT_CSS));
  const head = document.head || document.getElementsByTagName('head')[0];
  // First child, so the page's own rules come later and win.
  if (head.firstChild) head.insertBefore(style, head.firstChild);
  else head.appendChild(style);
}

/**
 * Dialogs are serialised rather than stacked.
 *
 * Two overlapping scrims read as a bug, and the second answer would resolve
 * against a dialog the user cannot see. A closing installer can easily raise
 * two confirmations at once, so this is not hypothetical.
 */
let queue: Promise<unknown> = Promise.resolve();

function render(options: DialogOptions): Promise<DialogAnswer> {
  injectStyles();

  const buttons = options.buttons || 'ok';
  const answers = BUTTON_SETS[buttons] || BUTTON_SETS.ok;
  // The leftmost button is the default: what Enter picks and what gets focus.
  const primary = answers[0];
  const icon = options.icon || 'none';
  const base = defaultLabels();
  const perCall = options.labels || {};

  const mask = document.createElement('div');
  mask.className = 'bk-dialog-mask';

  const dialog = document.createElement('div');
  dialog.className = options.className
    ? 'bk-dialog ' + options.className
    : 'bk-dialog';
  mask.appendChild(dialog);

  if (icon !== 'none') {
    const badge = document.createElement('i');
    badge.className =
      'bk-dialog-icon' +
      (icon === 'warning' ? ' bk-is-warning' : '') +
      (icon === 'error' ? ' bk-is-error' : '');
    badge.textContent = GLYPHS[icon];
    dialog.appendChild(badge);
  }

  if (options.title) {
    const title = document.createElement('h2');
    title.className = 'bk-dialog-title';
    // textContent, not innerHTML: a message can carry a path or an error
    // string from the system, and neither is trusted markup.
    title.textContent = options.title;
    dialog.appendChild(title);
  }

  const message = document.createElement('p');
  message.className = 'bk-dialog-message';
  message.textContent = options.message;
  dialog.appendChild(message);

  const actions = document.createElement('div');
  actions.className = 'bk-dialog-actions';
  dialog.appendChild(actions);

  return new Promise<DialogAnswer>((resolvePromise) => {
    let settled = false;

    function close(answer: DialogAnswer): void {
      if (settled) return;
      settled = true;
      document.removeEventListener('keydown', onKey, true);
      mask.className = 'bk-dialog-mask';
      // Let the fade finish before removing, but do not make the caller wait
      // on it — the answer is already known.
      setTimeout(function () {
        if (mask.parentNode) mask.parentNode.removeChild(mask);
      }, 200);
      resolvePromise(answer);
    }

    function onKey(event: KeyboardEvent): void {
      if (event.key === 'Escape' || event.keyCode === 27) {
        const escape = ESCAPE_ANSWER[buttons];
        if (escape) {
          event.preventDefault();
          close(escape);
        }
        return;
      }
      if (event.key === 'Enter' || event.keyCode === 13) {
        event.preventDefault();
        if (primary) close(primary);
      }
    }

    for (let i = 0; i < answers.length; i++) {
      const answer = answers[i];
      if (!answer) continue;
      const button = document.createElement('button');
      button.type = 'button';
      button.className =
        i === 0 ? 'bk-dialog-btn bk-is-default' : 'bk-dialog-btn';
      button.textContent =
        perCall[answer] || labelOverrides[answer] || base[answer];
      button.onclick = function () {
        close(answer);
      };
      actions.appendChild(button);
    }

    document.addEventListener('keydown', onKey, true);
    document.body.appendChild(mask);

    // Next frame, so the transition has an initial state to animate from.
    // A single rAF is not enough on this engine — the class lands in the same
    // paint as the insert and nothing animates.
    requestAnimationFrame(function () {
      requestAnimationFrame(function () {
        mask.className = 'bk-dialog-mask bk-is-open';
        const first = actions.getElementsByTagName('button')[0];
        if (first) first.focus();
      });
    });
  });
}

export function showDialog(options: DialogOptions): Promise<DialogAnswer> {
  const run = function (): Promise<DialogAnswer> {
    return customRenderer ? customRenderer(options) : render(options);
  };
  // Chain onto the queue, and keep the queue alive if a renderer throws.
  const result = queue.then(run, run);
  queue = result.then(
    function () {
      return null;
    },
    function () {
      return null;
    },
  );
  return result;
}
