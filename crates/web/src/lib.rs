//! Browser adapter; all game/render work stays in Rust.

#[cfg(target_arch = "wasm32")]
mod app;
#[cfg(all(target_arch = "wasm32", feature = "gpu-tests"))]
mod checks;
#[cfg(target_arch = "wasm32")]
mod scenery;
#[cfg(target_arch = "wasm32")]
mod vehicle;
#[cfg(target_arch = "wasm32")]
mod world;

#[cfg(target_arch = "wasm32")]
pub use app::App;
