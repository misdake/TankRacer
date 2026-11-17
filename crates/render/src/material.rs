//! Complete raster state. No draw inherits unspecified material state.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Compare {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Factor {
    Zero,
    One,
    SourceAlpha,
    OneMinusSourceAlpha,
    SourceColor,
    OneMinusSourceColor,
    DestinationColor,
    OneMinusDestinationColor,
    DestinationAlpha,
    OneMinusDestinationAlpha,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Equation {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Blend {
    pub source_rgb: Factor,
    pub destination_rgb: Factor,
    pub source_alpha: Factor,
    pub destination_alpha: Factor,
    pub rgb_equation: Equation,
    pub alpha_equation: Equation,
}

impl Blend {
    pub const STRAIGHT_ALPHA: Self = Self::new(Factor::SourceAlpha, Factor::OneMinusSourceAlpha);
    pub const PREMULTIPLIED_ALPHA: Self = Self::new(Factor::One, Factor::OneMinusSourceAlpha);
    pub const ADDITIVE: Self = Self::new(Factor::One, Factor::One);
    pub const ADDITIVE_ALPHA: Self = Self::new(Factor::SourceAlpha, Factor::One);

    pub const fn new(source_rgb: Factor, destination_rgb: Factor) -> Self {
        Self {
            source_rgb,
            destination_rgb,
            source_alpha: Factor::One,
            destination_alpha: Factor::OneMinusSourceAlpha,
            rgb_equation: Equation::Add,
            alpha_equation: Equation::Add,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Cull {
    None,
    Front,
    Back,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Pipeline {
    pub depth_compare: Option<Compare>,
    pub depth_write: bool,
    pub blend: Option<Blend>,
    pub cull: Cull,
    pub color_write: [bool; 4],
    /// Polygon offset [slope factor, depth units], explicit for coplanar decals.
    pub depth_bias: Option<[i8; 2]>,
}

impl Pipeline {
    pub const OPAQUE: Self = Self {
        depth_compare: Some(Compare::LessEqual),
        depth_write: true,
        blend: None,
        cull: Cull::Back,
        color_write: [true; 4],
        depth_bias: None,
    };
    pub const TRANSPARENT: Self = Self {
        depth_write: false,
        blend: Some(Blend::STRAIGHT_ALPHA),
        ..Self::OPAQUE
    };
    pub const OVERLAY: Self = Self {
        depth_compare: None,
        depth_write: false,
        cull: Cull::None,
        ..Self::TRANSPARENT
    };
}

#[derive(Debug, Clone, Copy)]
pub enum AlphaEncoding {
    Straight,
    Premultiplied,
}

/// Q12 normal projection and the clamped BLOB region in a shared atlas.
#[derive(Debug, Clone, Copy)]
pub struct NormalLayer {
    pub rows: [[i32; 3]; 2],
    pub atlas: [f32; 4],
    pub gain: f32,
}

#[derive(Debug, Clone, Copy)]
pub struct Material {
    pub pipeline: Pipeline,
    pub color: [f32; 4],
    pub alpha_cutoff: Option<f32>,
    pub texture_alpha: AlphaEncoding,
    pub output_alpha: AlphaEncoding,
    pub geometry_culling: bool,
    /// Optional per-vertex V scrolling channels; zero for ordinary geometry.
    pub uv_phase: [f32; 2],
    pub shaded: bool,
    /// Fused additive RGB on an opaque/keyed base, preserving its depth/coverage.
    pub normal_layer: Option<NormalLayer>,
}

impl Material {
    pub const fn opaque(color: [f32; 4]) -> Self {
        Self {
            pipeline: Pipeline::OPAQUE,
            color,
            alpha_cutoff: None,
            texture_alpha: AlphaEncoding::Straight,
            output_alpha: AlphaEncoding::Straight,
            geometry_culling: false,
            uv_phase: [0.; 2],
            shaded: false,
            normal_layer: None,
        }
    }
}
