# Runtime architecture

The 97 C modules in `modules.json` are copied byte-for-byte from the qualified research implementation. Cleanup extracts the production host from the previous lifecycle test harness; gameplay algorithms and browser code are unchanged. `source-manifest.json` distinguishes unchanged files from extracted adapters.

## Game data and updates

Game data is an explicit 8 MiB little-endian address space. Preserved numeric addresses identify fields, objects, methods and resources. They are not original process pointers. `frame_data.h` access callbacks route all game reads/writes to owned memory. `frame_native.c` binds the original method identities to readable C methods; there is no instruction emulator or executable image.

The host mounts seven archives for a race: DATA, Bronze/Silver/Gold tanks, INGAME, the selected level and COMMON. GUI is mounted separately for the frontend. `frame_host.c` reads archive directories and bundles. Surface/texture modules decode original image resources into RGBA texture updates; host allocation owns those buffers. Level teardown/reload is performed by the C session/game code.

Each accepted update advances the external clock by 16 ms. Original time, input edges, menus, actor methods, opponent AI, physics, events and results are processed by the C schedule. JavaScript supplies input and update counts, rather than writing vehicle transforms or deciding gameplay. `host_step` publishes the vehicle/camera feedback consumed by the next update even when no GPU redraw occurs.

The physics modules preserve integer/fixed-point arithmetic, operation order and stored intermediate values. Terrain and obstacle queries feed contact and suspension, which feed drive/velocity, pose and collision response. Player controls, pickups, weapons and world entities use the shared method table. Comments and preserved identities in each module trace operations to the reconstructed methods. Browser camera/render changes do not substitute a physics engine.

The frontend is also C: `frame_menu.c` owns pages, selection and transitions; `frame_menu_draw.c` produces its drawing commands. The browser restricts the entry flow to CUP and provides external controls/info panels. `host/frontend.inc` composes GUI resources, game profile, audio and command output. Audio playback and device unlocking are host services; cue decisions are C.

## Public API

Include `host/game_host.h`, link `tankracer_host` and `tankracer_core`. `tank_create(map,cup,vehicle,mode,opponents)` creates a fresh instance; map indices follow ENGLAND, MEXICO, SWAMP, EAST, THEME, BIO, MONACO, CIRCUIT. CUP is 0/1/2 (Bronze/Silver/Gold); vehicle 0 selects the class default. Mount slots 0–6 in the archive order above, then call `tank_start()`.

`tank_step(frames,bits,focused)` returns status (zero means success), then `tank_packet()`/`tank_packet_size()` expose the drawing packet. `tank_audio()`/`tank_audio_count()` expose nine-word audio commands; `tank_audio_gain()` supplies the current gain. `tank_data()` exposes game data for read-only inspection; production consumers must not mutate it. Returned storage is owned by the instance and may change on the next call. Calls are serial; multiple independent simulations require separate Wasm instances or processes.

Actions published in packet metadata are 1 restart, 2 exit and 3 race complete. Restart runs on the next step. Results and subsequent CUP tracks are handled by menu/result APIs and `tank_advance`; qualification checks remain in C. Call `tank_destroy()` for race teardown. Browser instances reclaim frontend storage when the worker/instance is discarded.

| Input bit | Action |
| --- | --- |
| 0, 1 | Throttle, brake/reverse |
| 2, 3 | Steer left, right |
| 4 | Fire |
| 5 | Esc / pause |
| 6–9 | Pause menu select, up, down, alternate select |
| 10, 11 | Request restart, exit (host/lifecycle integration) |
| 12, 13 | Turret left, right |
| 14 | Rear view |
| 15 | Toggle camera |
| 16 | Handbrake |
| 17 | Horn |

The browser bindings are arrows, Space, Esc, Z/X, C, V, A and H. Menu keyboard mapping is separate and feeds the original C menu input API. `web/src/keyboard.js` and `reconstruction.js` are the authoritative external bindings.

## Drawing and audio

Presentation projects and lights geometry, traverses scene visibility and emits original draw order/material state into TCB1. The browser does not rebuild a scene graph. `native-renderer.js` uploads TL vertices and applies the state described by each draw through `gl-state.js`: depth test/write/function, blend factors, alpha test, culling, textures and fog. The logical coordinate system remains 640 × 480 with proportional canvas scaling and black borders.

NWF1 begins with five little-endian 32-bit words: magic `NWF1`, total byte count, UTF-8 JSON byte count, TCB1 byte count, texture record count. JSON is followed by TCB1, then texture records consisting of token, width, height, RGBA byte count (four 32-bit words) and RGBA bytes. Full decoding and range checks are in `web/src/native-packet.js` and `command-buffer.js`. Textures are sent when created/reloaded. Audio commands are transferred separately by the Wasm worker.

Wasm compilation exports the same C API used by native. A worker executes C; the main thread receives copied command/audio buffers and renders. No server-side game state is needed. The Node server only serves static files and negotiates gzip.

## Verification boundary

`tests/parity.mjs` compares all 8 MiB of game data and exact TCB1/texture bytes across native/Wasm. `tests/baseline.mjs` additionally checks menu memory and audio against an earlier release. JSON timing fields measure host wall-clock work and are intentionally excluded from equality. The resource test verifies downloaded bytes after gzip decompression and the SHA-256 of each packed sample.

These tests detect cleanup/backend regressions. They do not claim pixel equality with every original GPU/driver. The research workspace retains disassembly, original-reference tests and longer recorded traces; production builds have no dependency on those files.
