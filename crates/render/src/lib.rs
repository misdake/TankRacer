//! World-space geometry and explicit materials; WebGL only at the backend edge.

pub mod geometry;
pub mod material;

#[cfg(target_arch = "wasm32")]
pub mod gpu;
