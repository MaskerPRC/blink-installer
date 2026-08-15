/**
 * Full-screen entrance animation.
 *
 * Timeline, all inside one window:
 *
 *   0-1150ms   shards streak in from beyond the edges and converge
 *   300-2100   a halo swells where the card will land
 *   850-1250   the core flashes as the shards arrive
 *   950-1800   two rings pulse outward
 *   1050-1750  the wordmark rises
 *   1500-2050  the installer card fades up at its final position
 *   2250       the window shrinks to exactly that rectangle; splash removed
 *
 * The handoff is seamless because the card is already at its final *screen*
 * position: only the window's bounds change, nothing drawn moves.
 *
 * On the choice of JS over CSS keyframes, and on the 33 fps ceiling this is
 * designed around, see splash.css and native/test/FINDINGS.md.
 */
import { config, sys, win } from 'blink-installer-ui';

const SHARD_COUNT = 20;

/**
 * The renderer presents at ~33 fps no matter what, while rAF fires at ~59 Hz.
 * Updating faster than the screen changes only burns CPU, so the loop skips
 * work it knows will never be shown.
 */
const MIN_FRAME_MS = 28;

const PHASE = {
  shards: [0, 1150],
  halo: [300, 2100],
  core: [850, 1250],
  ring1: [950, 1750],
  ring2: [1120, 1900],
  mark: [1050, 1750],
  card: [1500, 2050],
  // The splash wordmark has to clear out as the card arrives, or the two sit
  // on top of each other — they occupy the same place by design.
  markOut: [1480, 1880],
  handoff: 2250,
};

function progress(phase, t) {
  const span = phase[1] - phase[0];
  if (span <= 0) return 1;
  const p = (t - phase[0]) / span;
  return p < 0 ? 0 : p > 1 ? 1 : p;
}

function easeOut(p) {
  return 1 - Math.pow(1 - p, 3);
}
function easeOutSoft(p) {
  return 1 - Math.pow(1 - p, 2);
}
/** Rises to 1 then falls back to 0; for one-shot flashes and pulses. */
function pulse(p) {
  return Math.sin(Math.max(0, Math.min(1, p)) * Math.PI);
}

/** Deterministic PRNG, so the animation is identical on every run. */
function makeRandom(seed) {
  let state = seed >>> 0;
  return function next() {
    state = (state * 1664525 + 1013904223) >>> 0;
    return state / 4294967296;
  };
}

function buildShards(container, width, height) {
  const radius = Math.sqrt(width * width + height * height) / 2 + 120;
  const random = makeRandom(0x5eed);
  const shards = [];
  const fragment = document.createDocumentFragment();

  for (let i = 0; i < SHARD_COUNT; i++) {
    const angle = (i / SHARD_COUNT) * Math.PI * 2 + (random() - 0.5) * 0.42;
    const distance = radius * (0.78 + random() * 0.22);
    const el = document.createElement('i');
    el.className = 'shard';
    // Long streaks: at 33 fps a short bar travelling this far turns into a row
    // of dots, a long one stays a streak.
    const length = Math.round(90 + random() * 130);
    el.style.width = length + 'px';
    el.style.marginLeft = -length / 2 + 'px';
    fragment.appendChild(el);

    shards.push({
      el,
      fromX: Math.cos(angle) * distance,
      fromY: Math.sin(angle) * distance,
      // Point along its own direction of travel.
      rotation: Math.round((angle * 180) / Math.PI),
      delay: random() * 0.3,
    });
  }
  container.appendChild(fragment);
  return shards;
}

export async function runSplash(state) {
  const body = document.body;
  const splash = document.getElementById('splash');
  if (!splash) return;

  const cardWidth = Number(config.get('windowWidth')) || 800;
  const cardHeight = Number(config.get('windowHeight')) || 560;

  // Read the monitor rather than the page: this engine reports
  // window.innerWidth as 0 for a transparent window, which once produced a
  // negative, off-screen position.
  let work = { x: 0, y: 0, width: 0, height: 0 };
  try {
    work = (await sys.screen()).work;
  } catch (error) {
    console.warn('sys.screen failed; the splash will not reposition', error);
  }

  const card = document.querySelector('.shell');
  if (card) {
    // Inline, not custom properties: var() in a margin is unreliable here and
    // silently left the card half its own size off-centre.
    card.style.width = cardWidth + 'px';
    card.style.height = cardHeight + 'px';
    card.style.marginLeft = -Math.round(cardWidth / 2) + 'px';
    card.style.marginTop = -Math.round(cardHeight / 2) + 'px';
  }

  body.classList.add('is-splash');

  const viewWidth = document.documentElement.clientWidth || 1280;
  const viewHeight = document.documentElement.clientHeight || 800;

  const glyph = document.querySelector('.splash-mark .glyph');
  const name = document.querySelector('.splash-mark .name');
  if (glyph) glyph.textContent = state.productName.slice(0, 1).toUpperCase();
  if (name) name.textContent = state.productName;

  const shards = buildShards(document.getElementById('shards'), viewWidth, viewHeight);
  const halo = document.querySelector('.halo');
  const core = document.querySelector('.core');
  const rings = [document.querySelector('.ring-1'), document.querySelector('.ring-2')];
  const mark = document.querySelector('.splash-mark');

  const started = Date.now();
  let frames = 0;
  let lastDrawn = -1;

  await new Promise((resolve) => {
    function frame() {
      const t = Date.now() - started;

      if (t >= PHASE.handoff) {
        resolve();
        return;
      }
      // Skip frames the screen will never show.
      if (t - lastDrawn < MIN_FRAME_MS) {
        requestAnimationFrame(frame);
        return;
      }
      lastDrawn = t;
      frames++;

      // --- shards -------------------------------------------------------
      const shardBase = progress(PHASE.shards, t);
      for (let i = 0; i < shards.length; i++) {
        const s = shards[i];
        let p = (shardBase - s.delay) / (1 - s.delay);
        p = p < 0 ? 0 : p > 1 ? 1 : p;
        const eased = easeOut(p);
        const x = s.fromX * (1 - eased);
        const y = s.fromY * (1 - eased);
        // Shrink as they arrive so they appear to be swallowed by the core.
        const scale = 1 - 0.7 * eased;
        s.el.style.transform =
          'translate(' + x.toFixed(1) + 'px,' + y.toFixed(1) + 'px) ' +
          'rotate(' + s.rotation + 'deg) scaleX(' + scale.toFixed(3) + ')';
        s.el.style.opacity = (p < 0.15 ? p / 0.15 : 1 - Math.pow((p - 0.15) / 0.85, 2)).toFixed(3);
      }

      // --- halo ---------------------------------------------------------
      const haloP = progress(PHASE.halo, t);
      if (halo) {
        halo.style.transform = 'scale(' + (0.4 + 0.75 * easeOutSoft(haloP)).toFixed(3) + ')';
        halo.style.opacity = (haloP < 0.3 ? haloP / 0.3 : 1 - (haloP - 0.3) * 0.35).toFixed(3);
      }

      // --- core flash ---------------------------------------------------
      const coreP = progress(PHASE.core, t);
      if (core) {
        core.style.transform = 'scale(' + (0.3 + 2.2 * easeOut(coreP)).toFixed(3) + ')';
        core.style.opacity = pulse(coreP).toFixed(3);
      }

      // --- rings --------------------------------------------------------
      [PHASE.ring1, PHASE.ring2].forEach((phase, index) => {
        const ring = rings[index];
        if (!ring) return;
        const p = progress(phase, t);
        ring.style.transform = 'scale(' + (0.25 + (2.9 + index * 0.7) * easeOut(p)).toFixed(3) + ')';
        ring.style.opacity = (pulse(p) * (index === 0 ? 0.9 : 0.55)).toFixed(3);
      });

      // --- wordmark -----------------------------------------------------
      const markP = progress(PHASE.mark, t);
      const markOut = easeOutSoft(progress(PHASE.markOut, t));
      if (mark) {
        const e = easeOut(markP);
        // Lifts and shrinks slightly on the way out, so it reads as making way
        // for the card rather than simply vanishing.
        mark.style.transform =
          'translateY(' + (26 * (1 - e) - 22 * markOut).toFixed(1) + 'px) ' +
          'scale(' + (0.88 + 0.12 * e - 0.06 * markOut).toFixed(3) + ')';
        mark.style.opacity = (e * (1 - markOut)).toFixed(3);
      }

      // --- card ---------------------------------------------------------
      const cardP = progress(PHASE.card, t);
      if (card) {
        const e = easeOut(cardP);
        card.style.transform = 'scale(' + (0.92 + 0.08 * e).toFixed(3) + ')';
        card.style.opacity = e.toFixed(3);
      }

      requestAnimationFrame(frame);
    }
    requestAnimationFrame(frame);
  });

  const elapsed = Date.now() - started;
  console.log(
    'splash: ' + frames + ' frames in ' + elapsed + 'ms (' +
      Math.round((frames * 1000) / elapsed) + ' drawn/s) at ' + viewWidth + 'x' + viewHeight,
  );

  // Hand the window back at the card's rectangle. The card is centred in the
  // page and the window is centred on the monitor, so the two coincide and
  // nothing appears to move.
  if (work.width > 0 && work.height > 0) {
    win.setBounds({
      x: work.x + Math.round((work.width - cardWidth) / 2),
      y: work.y + Math.round((work.height - cardHeight) / 2),
      width: cardWidth,
      height: cardHeight,
    });
  } else {
    win.resize(cardWidth, cardHeight);
    win.center();
  }

  // Drop the inline styles so the card is governed by style.css alone again.
  if (card) {
    card.style.transform = '';
    card.style.opacity = '';
    card.style.width = '';
    card.style.height = '';
    card.style.marginLeft = '';
    card.style.marginTop = '';
  }
  body.classList.remove('is-splash');
  if (splash.parentNode) splash.parentNode.removeChild(splash);
}

/**
 * Wraps runSplash with the backstop from config.
 *
 * A full-screen transparent window that never goes away is a genuinely bad
 * failure: the user cannot click past it and may not even see what to close.
 * If anything throws or hangs, tear it down and show the installer.
 */
export async function playSplash(state) {
  if (config.get('splash') !== '1') return;

  const timeoutMs = Number(config.get('splashTimeoutMs')) || 6000;
  let settled = false;

  const recover = () => {
    const splash = document.getElementById('splash');
    if (splash && splash.parentNode) splash.parentNode.removeChild(splash);
    document.body.classList.remove('is-splash');
    const card = document.querySelector('.shell');
    if (card) card.style.cssText = '';
    win.resize(Number(config.get('windowWidth')) || 800, Number(config.get('windowHeight')) || 560);
    win.center();
  };

  const guard = new Promise((resolve) => setTimeout(resolve, timeoutMs)).then(() => {
    if (!settled) {
      console.warn('splash exceeded its timeout; showing the installer');
      recover();
    }
  });

  try {
    await Promise.race([runSplash(state), guard]);
    settled = true;
  } catch (error) {
    settled = true;
    console.error('splash failed', error);
    recover();
  }
}
