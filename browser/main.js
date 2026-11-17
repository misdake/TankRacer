import initialize, {App} from './pkg/tankracer.js';
import {Keys} from './keys.js';

const status = document.getElementById('status');
export let app;
let running = true;
const held = new Keys();
const canvas = document.getElementById('game');
const inspecting = new URLSearchParams(location.search).has('inspect');
canvas.title = inspecting ? 'Drag to look · WASD to move · Q/E down/up · Shift to move faster'
 : 'Arrow keys to drive · A to handbrake · C to change view · V for rear view · F for free camera';
const movementKeys = new Set([
  'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'KeyC', 'KeyV', 'KeyF',
  'KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'ShiftLeft', 'ShiftRight',
]);
window.addEventListener('keydown', event => {
  if (event.ctrlKey || event.metaKey || event.altKey) return;
  if (!movementKeys.has(event.code)) return;
  held.down(event.code, app?.inspection_active() ?? inspecting);
  if (event.code === 'KeyF' && !event.repeat && app) app.toggle_camera();
  event.preventDefault();
});
window.addEventListener('keyup', event => held.up(event.code));
window.addEventListener('blur', () => held.clear());
document.addEventListener('visibilitychange', () => held.clear());
let dragging = false;
canvas.addEventListener('pointerdown', event => {
  if (event.button !== 0) return;
  dragging = true;
  canvas.setPointerCapture(event.pointerId);
  canvas.focus();
});
canvas.addEventListener('pointerup', event => {
  dragging = false;
  if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);
});
canvas.addEventListener('pointercancel', () => { dragging = false; });
canvas.addEventListener('lostpointercapture', () => { dragging = false; });
window.addEventListener('blur', () => { dragging = false; });
canvas.addEventListener('pointermove', event => {
  if (dragging && app) app.look(event.movementX, event.movementY);
});
try {
  await initialize();
  app = new App(document.getElementById('game'));
  const download = async name=>{
    const response = await fetch(`./assets/${name}.DAT`);
    if (!response.ok) throw Error(`${name} download failed (${response.status})`);
    return new Uint8Array(await response.arrayBuffer());
  };
  const world = await download('ENGLAND');
  app.load_world(world);
  if(!inspecting){
    status.textContent = 'Loading vehicle…';
    const files = await Promise.all(['DATA','BRONZE','INGAME'].map(download));
    app.load_player(...files,world);
    app.begin_race();
  }
  held.clear();
  canvas.focus();
  status.hidden = true;
  document.body.dataset.ready = 'true';
  let previous = performance.now();
  let accumulator = 0;
  document.addEventListener('visibilitychange', () => {
    previous = performance.now();
    accumulator = 0;
  });
  function frame(milliseconds) {
    if (!running) return;
    const seconds = document.hidden ? 0 : Math.max(0, Math.min((milliseconds - previous) / 1000, .1));
    previous = milliseconds;
    const axis = (positive,negative)=>Number(held.has(positive))-Number(held.has(negative));
    try {
      if (!inspecting) {
        accumulator += seconds * 1000;
        while (accumulator >= 16) {
          app.set_camera_keys(held.has('KeyC'), held.has('KeyV'));
          app.step_keys(16, held.has('ArrowLeft'), held.has('ArrowRight'),
            held.has('ArrowUp'), held.has('ArrowDown'), held.handbrake);
          accumulator -= 16;
        }
      }
      app.move_camera(seconds,axis('KeyD','KeyA'),axis('KeyE','KeyQ'),axis('KeyW','KeyS'),held.has('ShiftLeft')||held.has('ShiftRight'));
      app.render();
    } catch(error) {
      running = false;
      status.hidden = false;
      status.textContent = String(error);
      document.body.dataset.error = String(error);
      throw error;
    }
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
} catch (error) {
  status.textContent = String(error);
  document.body.dataset.error = String(error);
  throw error;
}

// Used by inspection/tests to take ownership of the render loop temporarily.
export function stop() { running = false; }
