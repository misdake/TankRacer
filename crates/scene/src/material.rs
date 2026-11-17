//! Surface pass classification and explicit raster policies shared by all views.
use tankracer_render::material::{AlphaEncoding, Blend, Cull, Material, Pipeline};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BatchKind {
    Opaque,
    OpaqueTwoSided,
    Masked,
    MaskedTwoSided,
    Transparent,
}

impl BatchKind {
    pub const ALL: [Self; 5] = [
        Self::Opaque,
        Self::OpaqueTwoSided,
        Self::Masked,
        Self::MaskedTwoSided,
        Self::Transparent,
    ];
    pub const fn slot(self) -> usize {
        self as usize
    }
    pub(crate) fn for_surface(class: u32, double_sided: bool) -> Result<Self, &'static str> {
        match (class, double_sided) {
            (0, false) => Ok(Self::Opaque),
            (0, true) => Ok(Self::OpaqueTwoSided),
            (1, false) => Ok(Self::Masked),
            (1, true) => Ok(Self::MaskedTwoSided),
            (2, _) => Ok(Self::Transparent),
            _ => Err("Unsupported material class"),
        }
    }
}

pub fn surface(kind: BatchKind) -> Material {
    let transparent = kind == BatchKind::Transparent;
    let masked = matches!(kind, BatchKind::Masked | BatchKind::MaskedTwoSided);
    let double_sided = matches!(
        kind,
        BatchKind::OpaqueTwoSided | BatchKind::MaskedTwoSided | BatchKind::Transparent
    );
    Material {
        pipeline: Pipeline {
            depth_write: !transparent,
            blend: if transparent {
                Some(Blend::PREMULTIPLIED_ALPHA)
            } else {
                None
            },
            cull: if double_sided { Cull::None } else { Cull::Back },
            ..Pipeline::OPAQUE
        },
        alpha_cutoff: if masked { Some(0.5) } else { None },
        texture_alpha: AlphaEncoding::Premultiplied,
        output_alpha: if transparent {
            AlphaEncoding::Premultiplied
        } else {
            AlphaEncoding::Straight
        },
        geometry_culling: transparent,
        ..Material::opaque([1.; 4])
    }
}
