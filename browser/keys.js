// Physical keys belong to the action selected on keydown until release.
// This prevents A from becoming a handbrake just because F changes the view.
export class Keys {
  #held = new Set();
  #handbrake = false;

  down(code, inspecting) {
    if (code === 'KeyA' && !this.#held.has(code)) {
      this.#handbrake = !inspecting;
    }
    this.#held.add(code);
  }

  up(code) {
    this.#held.delete(code);
    if (code === 'KeyA') this.#handbrake = false;
  }

  has(code) { return this.#held.has(code); }
  get handbrake() { return this.#handbrake; }

  clear() {
    this.#held.clear();
    this.#handbrake = false;
  }
}
