import initialize, {App} from './pkg/tankracer.js';

const status = document.getElementById('status');
export let app;
let running = true;
const held = new Set();
const canvas = document.getElementById('game');
canvas.title = 'Drag to look · WASD to move · Q/E down/up · Shift to move faster';
const movementKeys = new Set(['KeyW','KeyA','KeyS','KeyD','KeyQ','KeyE','ShiftLeft','ShiftRight']);
window.addEventListener('keydown',event=>{if(event.ctrlKey||event.metaKey||event.altKey)return;if(movementKeys.has(event.code)){held.add(event.code);event.preventDefault();}});
window.addEventListener('keyup',event=>held.delete(event.code));
window.addEventListener('blur',()=>held.clear());
document.addEventListener('visibilitychange',()=>held.clear());
let dragging = false;
canvas.addEventListener('pointerdown',event=>{if(event.button===0){dragging=true;canvas.setPointerCapture(event.pointerId);canvas.focus();}});
canvas.addEventListener('pointerup',event=>{dragging=false;if(canvas.hasPointerCapture(event.pointerId))canvas.releasePointerCapture(event.pointerId);});
canvas.addEventListener('pointercancel',()=>{dragging=false;});
canvas.addEventListener('lostpointercapture',()=>{dragging=false;});
window.addEventListener('blur',()=>{dragging=false;});
canvas.addEventListener('pointermove',event=>{if(dragging&&app)app.look(event.movementX,event.movementY);});
try {
  await initialize();
  app = new App(document.getElementById('game'));
  const response = await fetch('./assets/ENGLAND.DAT');
  if (!response.ok) throw Error(`Level download failed (${response.status})`);
  app.load_world(new Uint8Array(await response.arrayBuffer()));
  held.clear();canvas.focus();
  status.hidden = true;
  document.body.dataset.ready = 'true';
  let previous = performance.now();
  function frame(milliseconds) {
    if (!running) return;
    const seconds = (milliseconds-previous)/1000;previous=milliseconds;
    const axis = (positive,negative)=>Number(held.has(positive))-Number(held.has(negative));
    app.move_camera(seconds,axis('KeyD','KeyA'),axis('KeyE','KeyQ'),axis('KeyW','KeyS'),held.has('ShiftLeft')||held.has('ShiftRight'));
    app.render();
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
