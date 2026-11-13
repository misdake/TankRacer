import initialize, {App} from './pkg/tankracer.js';

const status = document.getElementById('status');
export let app;
let running = true;
try {
  await initialize();
  app = new App(document.getElementById('game'));
  status.hidden = true;
  document.body.dataset.ready = 'true';
  function frame(milliseconds) {
    if (!running) return;
    app.render(milliseconds / 1000);
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
