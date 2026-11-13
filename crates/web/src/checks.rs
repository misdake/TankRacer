//! Pixel assertions against independent depth/blend equations, not screenshots.
use glam::Mat4;
use tankracer_render::{
    geometry::MeshData,
    gpu::{Frame, Renderer},
    material::{Blend, Compare, Cull, Equation, Factor, Material, Pipeline},
};
use wasm_bindgen::JsValue;

const BACKGROUND: [f32; 4] = [0.2, 0.4, 0.6, 128. / 255.];
const SOURCE: [f32; 4] = [0.6, 0.2, 0.8, 0.25];

fn expect(frame: &Frame<'_>, label: &str, expected: [f32; 4]) -> Result<(), JsValue> {
    let pixel = frame.read_pixel(2, 2)?;
    for i in 0..4 {
        let wanted = (expected[i].clamp(0., 1.) * 255.).round() as i32;
        if (i32::from(pixel[i]) - wanted).abs() > 2 {
            return Err(format!(
                "GPU {label}: channel {i}, actual {pixel:?}, expected {expected:?}"
            )
            .into());
        }
    }
    Ok(())
}

#[derive(Clone, Copy)]
enum Mode {
    Opaque,
    Alpha,
    Premultiplied,
    Additive,
    AlphaAdditive,
    Multiply,
    Subtract,
    ReverseSubtract,
    MinMax,
}

impl Mode {
    fn material(self) -> Material {
        let mut color = SOURCE;
        let blend = match self {
            Self::Opaque => None,
            Self::Alpha => Some(Blend::STRAIGHT_ALPHA),
            Self::Premultiplied => {
                for i in 0..3 {
                    color[i] *= color[3];
                }
                Some(Blend::PREMULTIPLIED_ALPHA)
            }
            Self::Additive => Some(Blend::ADDITIVE),
            Self::AlphaAdditive => Some(Blend::ADDITIVE_ALPHA),
            Self::Multiply => Some(Blend {
                source_rgb: Factor::DestinationColor,
                destination_rgb: Factor::Zero,
                source_alpha: Factor::Zero,
                destination_alpha: Factor::One,
                ..Blend::ADDITIVE
            }),
            Self::Subtract | Self::ReverseSubtract => Some(Blend {
                rgb_equation: if matches!(self, Self::Subtract) {
                    Equation::Subtract
                } else {
                    Equation::ReverseSubtract
                },
                ..Blend::ADDITIVE
            }),
            Self::MinMax => Some(Blend {
                rgb_equation: Equation::Min,
                alpha_equation: Equation::Max,
                ..Blend::ADDITIVE
            }),
        };
        Material {
            color,
            pipeline: Pipeline {
                blend,
                ..Pipeline::OVERLAY
            },
            alpha_cutoff: None,
        }
    }

    /// Independent reference, deliberately not derived from pipeline enum fields.
    fn over(self, destination: [f32; 4]) -> [f32; 4] {
        let mut out = [0.; 4];
        for i in 0..3 {
            let s = SOURCE[i];
            let d = destination[i];
            let a = SOURCE[3];
            out[i] = match self {
                Self::Opaque => s,
                Self::Alpha | Self::Premultiplied => s * a + d * (1. - a),
                Self::Additive => s + d,
                Self::AlphaAdditive => s * a + d,
                Self::Multiply => s * d,
                Self::Subtract => s - d,
                Self::ReverseSubtract => d - s,
                Self::MinMax => s.min(d),
            };
        }
        out[3] = match self {
            Self::Opaque => SOURCE[3],
            Self::Multiply => destination[3],
            Self::MinMax => SOURCE[3].max(destination[3]),
            _ => SOURCE[3] + destination[3] * (1. - SOURCE[3]),
        };
        // The intermediate framebuffer is RGBA8, so each draw is quantized.
        out.map(|v| (v.clamp(0., 1.) * 255.).round() / 255.)
    }
}

pub fn run(renderer: &mut Renderer) -> Result<String, JsValue> {
    let target = renderer.target(4, 4)?;
    let quad = renderer.upload_mesh(&MeshData::quad(0.))?;
    let red = Material {
        pipeline: Pipeline {
            cull: Cull::None,
            ..Pipeline::OPAQUE
        },
        ..Material::opaque([1., 0., 0., 1.])
    };
    let green = Material {
        color: [0., 1., 0., 1.],
        ..red
    };
    let mut cases = 0;
    for compare in [
        Compare::Never,
        Compare::Less,
        Compare::Equal,
        Compare::LessEqual,
        Compare::Greater,
        Compare::NotEqual,
        Compare::GreaterEqual,
        Compare::Always,
    ] {
        for z in [-0.5, 0., 0.5] {
            let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
            frame.draw(&quad, &red, None, Mat4::IDENTITY);
            let material = Material {
                pipeline: Pipeline {
                    depth_compare: Some(compare),
                    ..green.pipeline
                },
                ..green
            };
            frame.draw(
                &quad,
                &material,
                None,
                Mat4::from_translation(glam::Vec3::new(0., 0., z)),
            );
            let accepted = match compare {
                Compare::Never => false,
                Compare::Less => z < 0.,
                Compare::Equal => z == 0.,
                Compare::LessEqual => z <= 0.,
                Compare::Greater => z > 0.,
                Compare::NotEqual => z != 0.,
                Compare::GreaterEqual => z >= 0.,
                Compare::Always => true,
            };
            expect(
                &frame,
                &format!("depth {compare:?} {z}"),
                if accepted { green.color } else { red.color },
            )?;
            cases += 1;
        }
    }
    for write in [false, true] {
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        let material = Material {
            pipeline: Pipeline {
                depth_write: write,
                ..red.pipeline
            },
            ..red
        };
        frame.draw(
            &quad,
            &material,
            None,
            Mat4::from_translation(glam::Vec3::new(0., 0., -0.5)),
        );
        frame.draw(
            &quad,
            &green,
            None,
            Mat4::from_translation(glam::Vec3::new(0., 0., 0.5)),
        );
        expect(
            &frame,
            "depth write",
            if write { red.color } else { green.color },
        )?;
        cases += 1;
    }
    let modes = [
        Mode::Opaque,
        Mode::Alpha,
        Mode::Premultiplied,
        Mode::Additive,
        Mode::AlphaAdditive,
        Mode::Multiply,
        Mode::Subtract,
        Mode::ReverseSubtract,
        Mode::MinMax,
    ];
    for (i, first) in modes.iter().enumerate() {
        for (j, second) in modes.iter().enumerate() {
            let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
            frame.draw(&quad, &first.material(), None, Mat4::IDENTITY);
            frame.draw(&quad, &second.material(), None, Mat4::IDENTITY);
            expect(
                &frame,
                &format!("blend transition {i}->{j}"),
                second.over(first.over(BACKGROUND)),
            )?;
            cases += 1;
        }
    }
    // Discarded texels must not write depth; the later opaque draw must restore
    // texture usage, cutoff, blending and depth-write state.
    let invisible = renderer.upload_rgba(1, 1, &[255, 0, 255, 0])?;
    let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
    let cutout = Material {
        alpha_cutoff: Some(0.5),
        ..red
    };
    frame.draw(
        &quad,
        &cutout,
        Some(&invisible),
        Mat4::from_translation(glam::Vec3::new(0., 0., -0.5)),
    );
    expect(&frame, "cutout discard", BACKGROUND)?;
    cases += 1;
    frame.draw(&quad, &green, None, Mat4::IDENTITY);
    expect(&frame, "cutout to opaque", green.color)?;
    cases += 1;

    let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
    frame.draw(
        &quad,
        &Material {
            pipeline: Pipeline {
                cull: Cull::Front,
                ..red.pipeline
            },
            ..red
        },
        None,
        Mat4::IDENTITY,
    );
    expect(&frame, "front culling", BACKGROUND)?;
    cases += 1;
    frame.draw(
        &quad,
        &Material {
            pipeline: Pipeline {
                cull: Cull::Back,
                ..green.pipeline
            },
            ..green
        },
        None,
        Mat4::IDENTITY,
    );
    expect(&frame, "cull restoration", green.color)?;
    cases += 1;

    let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
    let masked = Material {
        pipeline: Pipeline {
            color_write: [false, true, false, false],
            depth_write: false,
            ..red.pipeline
        },
        ..green
    };
    frame.draw(&quad, &masked, None, Mat4::IDENTITY);
    expect(
        &frame,
        "color mask",
        [BACKGROUND[0], 1., BACKGROUND[2], BACKGROUND[3]],
    )?;
    cases += 1;
    frame.draw(&quad, &red, None, Mat4::IDENTITY);
    expect(&frame, "color mask restoration", red.color)?;
    cases += 1;

    // The last draw leaves masks disabled. Next frame must clear the old depth
    // and restore an identical material, rather than trust the stale cache.
    let blocked = Material {
        pipeline: Pipeline {
            color_write: [false; 4],
            ..Pipeline::OVERLAY
        },
        ..red
    };
    frame.draw(&quad, &blocked, None, Mat4::IDENTITY);
    let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
    frame.draw(
        &quad,
        &green,
        None,
        Mat4::from_translation(glam::Vec3::new(0., 0., 0.5)),
    );
    expect(&frame, "frame clear and mask restoration", green.color)?;
    cases += 1;
    renderer.check_error()?;
    Ok(format!(
        "{{\"passed\":true,\"pixel_cases\":{cases},\"depth_comparisons\":8,\"blend_transitions\":81}}"
    ))
}
