use super::Renderer;
use crate::material::{Compare, Cull, Equation, Factor, Pipeline};
use web_sys::WebGl2RenderingContext as Gl;

impl Renderer {
    pub(super) fn apply_pipeline(&mut self, next: Pipeline) {
        let previous = self.pipeline;
        if previous == Some(next) {
            return;
        }
        let gl = &self.gl;
        if previous.is_none_or(|p| p.depth_compare != next.depth_compare) {
            capability(gl, Gl::DEPTH_TEST, next.depth_compare.is_some());
            if let Some(compare) = next.depth_compare {
                gl.depth_func(depth(compare));
            }
        }
        if previous.is_none_or(|p| p.depth_write != next.depth_write) {
            gl.depth_mask(next.depth_write);
        }
        if previous.is_none_or(|p| p.blend != next.blend) {
            capability(gl, Gl::BLEND, next.blend.is_some());
            if let Some(blend) = next.blend {
                gl.blend_func_separate(
                    factor(blend.source_rgb),
                    factor(blend.destination_rgb),
                    factor(blend.source_alpha),
                    factor(blend.destination_alpha),
                );
                gl.blend_equation_separate(
                    equation(blend.rgb_equation),
                    equation(blend.alpha_equation),
                );
            }
        }
        if previous.is_none_or(|p| p.cull != next.cull) {
            capability(gl, Gl::CULL_FACE, next.cull != Cull::None);
            if next.cull != Cull::None {
                gl.cull_face(if next.cull == Cull::Back {
                    Gl::BACK
                } else {
                    Gl::FRONT
                });
            }
        }
        if previous.is_none_or(|p| p.color_write != next.color_write) {
            let [r, g, b, a] = next.color_write;
            gl.color_mask(r, g, b, a);
        }
        if previous.is_none_or(|p| p.depth_bias != next.depth_bias) {
            capability(gl, Gl::POLYGON_OFFSET_FILL, next.depth_bias.is_some());
            if let Some([slope, units]) = next.depth_bias {
                gl.polygon_offset(f32::from(slope), f32::from(units));
            }
        }
        self.pipeline = Some(next);
        self.stats.pipeline_changes += 1;
    }
}

fn capability(gl: &Gl, name: u32, enabled: bool) {
    if enabled {
        gl.enable(name);
    } else {
        gl.disable(name);
    }
}

fn depth(value: Compare) -> u32 {
    match value {
        Compare::Never => Gl::NEVER,
        Compare::Less => Gl::LESS,
        Compare::Equal => Gl::EQUAL,
        Compare::LessEqual => Gl::LEQUAL,
        Compare::Greater => Gl::GREATER,
        Compare::NotEqual => Gl::NOTEQUAL,
        Compare::GreaterEqual => Gl::GEQUAL,
        Compare::Always => Gl::ALWAYS,
    }
}

fn factor(value: Factor) -> u32 {
    match value {
        Factor::Zero => Gl::ZERO,
        Factor::One => Gl::ONE,
        Factor::SourceAlpha => Gl::SRC_ALPHA,
        Factor::OneMinusSourceAlpha => Gl::ONE_MINUS_SRC_ALPHA,
        Factor::SourceColor => Gl::SRC_COLOR,
        Factor::OneMinusSourceColor => Gl::ONE_MINUS_SRC_COLOR,
        Factor::DestinationColor => Gl::DST_COLOR,
        Factor::OneMinusDestinationColor => Gl::ONE_MINUS_DST_COLOR,
        Factor::DestinationAlpha => Gl::DST_ALPHA,
        Factor::OneMinusDestinationAlpha => Gl::ONE_MINUS_DST_ALPHA,
    }
}

fn equation(value: Equation) -> u32 {
    match value {
        Equation::Add => Gl::FUNC_ADD,
        Equation::Subtract => Gl::FUNC_SUBTRACT,
        Equation::ReverseSubtract => Gl::FUNC_REVERSE_SUBTRACT,
        Equation::Min => Gl::MIN,
        Equation::Max => Gl::MAX,
    }
}
