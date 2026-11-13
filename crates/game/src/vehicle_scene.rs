//! Vehicle after-pose components. The full active-scene scheduler is separate
//! work; individual kernels never stand in for an incomplete successful tick.
pub mod lights;
pub mod marks;
pub mod shield;
pub mod smoke;
pub mod spray;
pub mod temporary;
