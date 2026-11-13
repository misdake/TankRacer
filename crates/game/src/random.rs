//! The single shared game RNG; callers own the seed and exact consumption order.
pub fn next(seed: &mut u32) -> u32 {
    *seed = seed.wrapping_mul(1664525).wrapping_add(1013904223);
    *seed
}
