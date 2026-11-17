//! Pixel assertions against independent depth/blend equations, not screenshots.
use glam::Mat4;
use tankracer_render::{
    geometry::MeshData,
    gpu::{Frame, Renderer},
    material::{AlphaEncoding, Blend, Compare, Cull, Equation, Factor, Material, Pipeline},
};
use wasm_bindgen::JsValue;

const BACKGROUND: [f32; 4] = [0.2, 0.4, 0.6, 128. / 255.];
const SOURCE: [f32; 4] = [0.6, 0.2, 0.8, 0.25];

fn expect(frame: &Frame<'_>, label: &str, expected: [f32; 4]) -> Result<(), JsValue> {
    expect_at(frame, label, [2, 2], expected)
}

fn expect_at(
    frame: &Frame<'_>,
    label: &str,
    point: [i32; 2],
    expected: [f32; 4],
) -> Result<(), JsValue> {
    let pixel = frame.read_pixel(point[0], point[1])?;
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
        let color = SOURCE;
        let blend = match self {
            Self::Opaque => None,
            Self::Alpha => Some(Blend::STRAIGHT_ALPHA),
            Self::Premultiplied => Some(Blend::PREMULTIPLIED_ALPHA),
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
            output_alpha: if matches!(self, Self::Premultiplied) {
                AlphaEncoding::Premultiplied
            } else {
                AlphaEncoding::Straight
            },
            ..Material::opaque(color)
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

    // Premultiplication is an explicit input/output convention. All four
    // combinations must produce the same correctly filtered white silhouette.
    for input in [AlphaEncoding::Straight, AlphaEncoding::Premultiplied] {
        let invisible = if matches!(input, AlphaEncoding::Straight) {
            [255, 255, 255, 0]
        } else {
            [0, 0, 0, 0]
        };
        let texture = renderer.upload_rgba(
            2,
            1,
            &[
                255,
                255,
                255,
                255,
                invisible[0],
                invisible[1],
                invisible[2],
                invisible[3],
            ],
        )?;
        for output in [AlphaEncoding::Straight, AlphaEncoding::Premultiplied] {
            let material = Material {
                pipeline: Pipeline {
                    blend: Some(if matches!(output, AlphaEncoding::Straight) {
                        Blend::STRAIGHT_ALPHA
                    } else {
                        Blend::PREMULTIPLIED_ALPHA
                    }),
                    ..Pipeline::OVERLAY
                },
                texture_alpha: input,
                output_alpha: output,
                ..Material::opaque([1.; 4])
            };
            let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
            frame.draw(&quad, &material, Some(&texture), Mat4::IDENTITY);
            expect(
                &frame,
                "filtered silhouette encoding",
                [
                    0.25 + BACKGROUND[0] * 0.75,
                    0.25 + BACKGROUND[1] * 0.75,
                    0.25 + BACKGROUND[2] * 0.75,
                    0.25 + BACKGROUND[3] * 0.75,
                ],
            )?;
            cases += 1;
        }
    }

    let images =
        [[255, 0, 0, 255], [0, 255, 0, 255]].map(|rgba| tankracer_assets::surface::Image {
            width: 1,
            height: 1,
            flags: 4,
            rgba: rgba.to_vec(),
            consumed: 0,
        });
    let atlas = tankracer_scene::atlas::Atlas::pack(&images).map_err(JsValue::from_str)?;
    let texture = renderer.upload_rgba(atlas.width, atlas.height, &atlas.premultiplied_rgba)?;
    let atlas_material = Material {
        pipeline: Pipeline {
            blend: None,
            ..Pipeline::OVERLAY
        },
        texture_alpha: AlphaEncoding::Premultiplied,
        ..Material::opaque([1.; 4])
    };
    for (index, region) in atlas.regions.iter().enumerate() {
        let mut data = MeshData::quad(0.);
        for (v, uv) in data
            .vertices
            .iter_mut()
            .zip([[0, 0], [255, 0], [255, 255], [0, 255]])
        {
            v.uv = atlas.uv(*region, uv);
        }
        let mesh = renderer.upload_mesh(&data)?;
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(&mesh, &atlas_material, Some(&texture), Mat4::IDENTITY);
        for point in [[0, 0], [3, 0], [3, 3], [0, 3]] {
            expect_at(
                &frame,
                "atlas filtered border",
                point,
                if index == 0 {
                    [1., 0., 0., 1.]
                } else {
                    [0., 1., 0., 1.]
                },
            )?;
            cases += 1;
        }
    }

    for double_sided in [false, true] {
        let mut data = MeshData::quad(0.);
        for vertex in &mut data.vertices {
            vertex.double_sided = f32::from(u8::from(double_sided));
        }
        let mesh = renderer.upload_mesh(&data)?;
        let material = Material {
            pipeline: Pipeline {
                blend: None,
                ..Pipeline::OVERLAY
            },
            geometry_culling: true,
            ..red
        };
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(
            &mesh,
            &material,
            None,
            Mat4::from_scale(glam::Vec3::new(-1., 1., 1.)),
        );
        expect(
            &frame,
            "mixed transparent geometry culling",
            if double_sided { red.color } else { BACKGROUND },
        )?;
        cases += 1;
    }

    let mut billboard = MeshData::quad(0.);
    for vertex in &mut billboard.vertices {
        vertex.billboard_offset = 1.5;
    }
    let mesh = renderer.upload_mesh(&billboard)?;
    let material = Material {
        pipeline: Pipeline {
            blend: None,
            ..Pipeline::OVERLAY
        },
        ..red
    };
    for (right, point, expected) in [
        ([1., 0.], [3, 2], red.color),
        ([-1., 0.], [3, 2], BACKGROUND),
        ([-1., 0.], [0, 2], red.color),
    ] {
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.set_billboard_right(right);
        frame.draw(&mesh, &material, None, Mat4::IDENTITY);
        expect_at(&frame, "GPU billboard basis", point, expected)?;
        cases += 1;
    }

    let mut mesh = renderer.upload_mesh(&MeshData::quad(0.))?;
    for (indices, expected) in [
        ([0, 2, 1, 0, 3, 2], BACKGROUND),
        ([0, 1, 2, 0, 2, 3], red.color),
    ] {
        renderer.update_indices(&mut mesh, &indices)?;
        let material = Material {
            pipeline: Pipeline {
                cull: Cull::Back,
                ..red.pipeline
            },
            ..red
        };
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(&mesh, &material, None, Mat4::IDENTITY);
        expect(&frame, "index-only mesh update", expected)?;
        cases += 1;
    }
    if renderer.update_indices(&mut mesh, &[0, 1, 999]).is_ok() {
        return Err("Out-of-range index update accepted".into());
    }

    // Matrix attributes are VAO state. Exercise same-VAO normal/instanced
    // transitions and another VAO with the same stream, then a different stream.
    let mut instances = renderer.instances()?;
    let transforms = [-0.5, 0.5].map(|x| {
        (Mat4::from_translation(glam::Vec3::new(x, 0., 0.))
            * Mat4::from_scale(glam::Vec3::new(0.4, 0.8, 1.)))
        .to_cols_array()
    });
    renderer.update_instances(&mut instances, &transforms)?;
    let mut single = renderer.instances()?;
    renderer.update_instances(
        &mut single,
        &[Mat4::from_translation(glam::Vec3::new(3., 0., 0.)).to_cols_array()],
    )?;
    let other = renderer.upload_mesh(&MeshData::quad(0.))?;
    {
        let overlay = Material {
            pipeline: Pipeline {
                blend: None,
                ..Pipeline::OVERLAY
            },
            ..red
        };
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw_instanced(&mesh, &overlay, None, Mat4::IDENTITY, &instances);
        for point in [[0, 2], [3, 2]] {
            expect_at(&frame, "two model instances", point, red.color)?;
            cases += 1;
        }
        frame.draw(
            &mesh,
            &Material {
                color: [0., 1., 0., 1.],
                ..overlay
            },
            None,
            Mat4::IDENTITY,
        );
        expect(&frame, "instance to ordinary same VAO", [0., 1., 0., 1.])?;
        cases += 1;
        frame.draw_instanced(&mesh, &overlay, None, Mat4::IDENTITY, &instances);
        expect(&frame, "ordinary to instance same VAO", red.color)?;
        cases += 1;
        frame.draw_instanced(
            &other,
            &Material {
                color: [0., 0., 1., 1.],
                ..overlay
            },
            None,
            Mat4::IDENTITY,
            &instances,
        );
        expect(&frame, "same stream different VAO", [0., 0., 1., 1.])?;
        cases += 1;
        frame.draw_instanced(&other, &overlay, None, Mat4::IDENTITY, &single);
        expect(&frame, "different instance stream", [0., 0., 1., 1.])?;
        cases += 1;
    }
    renderer.update_indices(&mut mesh, &[0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3])?;
    {
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw_range(&mesh, &red, None, [Mat4::IDENTITY; 2], 9..12)?;
        expect_at(&frame, "range byte offset", [0, 3], red.color)?;
        expect_at(&frame, "range excludes other triangle", [3, 0], BACKGROUND)?;
        cases += 2;
        if frame
            .draw_range(&mesh, &red, None, [Mat4::IDENTITY; 2], 0..15)
            .is_ok()
        {
            return Err("Out-of-range draw accepted".into());
        }
    }
    let mut changed = MeshData::quad(0.);
    for v in &mut changed.vertices {
        v.position[0] += 3.;
    }
    renderer.update_vertices(&mut mesh, &changed.vertices)?;
    {
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(&mesh, &red, None, Mat4::IDENTITY);
        expect(&frame, "current vertex upload", BACKGROUND)?;
        cases += 1;
        frame.draw_model(
            &mesh,
            &red,
            None,
            Mat4::IDENTITY,
            Mat4::from_translation(glam::Vec3::new(-3., 0., 0.)),
        );
        expect(&frame, "model uniform after vertex update", red.color)?;
        cases += 1;
    }
    // World / transformed object / world must interleave, including switching
    // index banks. Independent source-over equations catch order/state leaks.
    {
        use glam::Vec3;
        use tankracer_scene::{
            camera::Camera,
            transparency::{Object, Order},
            world::TransparentTriangle,
        };
        let mut world = MeshData::quad(-0.5);
        for vertex in &mut world.vertices {
            vertex.color = [1., 0., 0., 0.25];
        }
        let mut front = MeshData::quad(0.5);
        for vertex in &mut front.vertices {
            vertex.color = [0., 1., 0., 0.5];
        }
        world.vertices.extend(front.vertices);
        world
            .indices
            .extend(front.indices.into_iter().map(|i| i + 4));
        let object = MeshData::quad(0.);
        let mut gpu = [
            renderer.upload_mesh(&world)?,
            renderer.upload_mesh(&object)?,
        ];
        let faces = [
            vec![
                TransparentTriangle {
                    indices: [0, 1, 2],
                    center: Vec3::new(0., 0., -0.5),
                },
                TransparentTriangle {
                    indices: [0, 2, 3],
                    center: Vec3::new(0., 0., -0.5),
                },
                TransparentTriangle {
                    indices: [4, 5, 6],
                    center: Vec3::new(0., 0., 0.5),
                },
                TransparentTriangle {
                    indices: [4, 6, 7],
                    center: Vec3::new(0., 0., 0.5),
                },
            ],
            vec![
                TransparentTriangle {
                    indices: [0, 1, 2],
                    center: Vec3::ZERO,
                },
                TransparentTriangle {
                    indices: [0, 2, 3],
                    center: Vec3::ZERO,
                },
            ],
        ];
        let objects = [
            Object {
                geometry: 0,
                model: Mat4::IDENTITY,
            },
            Object {
                geometry: 1,
                model: Mat4::IDENTITY,
            },
        ];
        let camera = Camera::looking_at(Vec3::new(0., 0., 3.), Vec3::ZERO);
        let mut order = Order::default();
        order.update(&objects, |i| &faces[i], &camera, 0);
        for (mesh, indices) in gpu.iter_mut().zip(&order.banks) {
            renderer.update_indices(mesh, indices)?;
        }
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        for run in &order.runs {
            let object = objects[run.object];
            let material = Material {
                pipeline: Pipeline {
                    blend: Some(Blend::PREMULTIPLIED_ALPHA),
                    ..Pipeline::OVERLAY
                },
                output_alpha: AlphaEncoding::Premultiplied,
                ..Material::opaque(if object.geometry == 0 {
                    [1.; 4]
                } else {
                    [0., 0., 1., 0.5]
                })
            };
            frame.draw_range(
                &gpu[object.geometry],
                &material,
                None,
                [Mat4::IDENTITY, object.model],
                run.first..run.first + run.count,
            )?;
        }
        let over = |source: [f32; 4], destination: [f32; 4]| {
            std::array::from_fn(|i| {
                if i == 3 {
                    source[3] + destination[3] * (1. - source[3])
                } else {
                    source[i] * source[3] + destination[i] * (1. - source[3])
                }
            })
        };
        expect(
            &frame,
            "world object world alpha order",
            over(
                [0., 1., 0., 0.5],
                over([0., 0., 1., 0.5], over([1., 0., 0., 0.25], BACKGROUND)),
            ),
        )?;
        cases += 1;
    }
    // Optional UV streams must wrap each corner inside its atlas region, stay
    // independent between channels, and never leak into an ordinary mesh VAO.
    {
        let pixels: Vec<_> = (0..1024)
            .flat_map(|pixel| {
                if (128..640).contains(&pixel) {
                    let row = ((pixel - 128) / 2) as u8;
                    [row, 255 - row, 32, 255]
                } else {
                    [255, 0, 255, 255]
                }
            })
            .collect();
        let texture = renderer.upload_rgba(1, 1024, &pixels)?;
        for (channel, base, phases) in [(0usize, 0.5, 8usize), (1, 250.5, 5)] {
            let mut data = MeshData::quad(0.);
            for vertex in &mut data.vertices {
                vertex.uv = [0.5, (128. + base * 2.) / 1024.];
            }
            let mut coefficient = [0., 0., 128. / 1024., 512. / 1024.];
            coefficient[channel] = if channel == 0 { 32. / 512. } else { 2. / 512. };
            let animated =
                renderer.upload_animated_mesh(&data, &vec![coefficient; data.vertices.len()])?;
            let ordinary = renderer.upload_mesh(&data)?;
            for phase in 0..phases {
                let mut material = Material::opaque([1.; 4]);
                material.uv_phase[channel] = phase as f32;
                let row = (base as usize + phase * if channel == 0 { 32 } else { 2 }) & 255;
                let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
                frame.draw(&animated, &material, Some(&texture), Mat4::IDENTITY);
                expect(
                    &frame,
                    "wheel/tread atlas row",
                    [row as f32 / 255., (255 - row) as f32 / 255., 32. / 255., 1.],
                )?;
                cases += 1;
                // Preserve the nonzero material phase deliberately. A mesh with
                // no animation stream still samples its original UVs.
                let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
                frame.draw(&ordinary, &material, Some(&texture), Mat4::IDENTITY);
                let row = base as u8;
                expect(
                    &frame,
                    "UV stream VAO restoration",
                    [
                        f32::from(row) / 255.,
                        f32::from(255 - row) / 255.,
                        32. / 255.,
                        1.,
                    ],
                )?;
                cases += 1;
            }
        }
    }
    {
        let mesh = renderer.upload_mesh(&MeshData::quad(0.))?;
        let shadow = Material {
            pipeline: Pipeline {
                cull: Cull::None,
                depth_write: false,
                depth_bias: Some([-1, -1]),
                blend: Some(Blend::STRAIGHT_ALPHA),
                ..Pipeline::OPAQUE
            },
            ..Material::opaque([32. / 255., 32. / 255., 32. / 255., 128. / 255.])
        };
        let green = Material::opaque([0., 1., 0., 1.]);
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(&mesh, &green, None, Mat4::IDENTITY);
        frame.draw(&mesh, &shadow, None, Mat4::IDENTITY);
        let alpha = 128. / 255.;
        let expected = [
            32. / 255. * alpha,
            1. - alpha + 32. / 255. * alpha,
            32. / 255. * alpha,
            1.,
        ];
        expect(&frame, "coplanar ground shadow blend", expected)?;
        frame.draw(
            &mesh,
            &Material {
                pipeline: Pipeline {
                    depth_compare: Some(Compare::Less),
                    ..Pipeline::OPAQUE
                },
                ..Material::opaque([0., 0., 1., 1.])
            },
            None,
            Mat4::IDENTITY,
        );
        expect(
            &frame,
            "shadow restores bias and does not write depth",
            expected,
        )?;
        cases += 2;
        frame.draw(
            &mesh,
            &Material {
                pipeline: Pipeline {
                    depth_compare: Some(Compare::Equal),
                    ..Pipeline::OPAQUE
                },
                ..Material::opaque([0., 0., 1., 1.])
            },
            None,
            Mat4::IDENTITY,
        );
        expect(
            &frame,
            "shadow preserves the original ground depth",
            [0., 0., 1., 1.],
        )?;
        cases += 1;
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(
            &mesh,
            &green,
            None,
            Mat4::from_translation(glam::Vec3::new(0., 0., -0.5)),
        );
        frame.draw(&mesh, &shadow, None, Mat4::IDENTITY);
        expect(&frame, "foreground occludes ground shadow", green.color)?;
        cases += 1;
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        let shaded = Material {
            shaded: true,
            ..Material::opaque([1.; 4])
        };
        frame.draw(&mesh, &shaded, None, Mat4::IDENTITY);
        expect(
            &frame,
            "neutral body shade truncates 255 to 127",
            [127. / 255., 127. / 255., 127. / 255., 1.],
        )?;
        cases += 1;
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(&mesh, &Material::opaque([1.; 4]), None, Mat4::IDENTITY);
        expect(&frame, "body shade is reset on ordinary draws", [1.; 4])?;
        cases += 1;
    }
    // Authored normal layer: integer GPU projection, fused SRCALPHA/ONE RGB,
    // shade gain, face mask, cutout, foreground depth and state restoration.
    {
        use tankracer_render::material::NormalLayer;
        let pixels: Vec<_> = (0..64)
            .flat_map(|y| (0..64).flat_map(move |x| [x * 4, y * 4, 50, 255]))
            .collect();
        let texture = renderer.upload_rgba(64, 64, &pixels)?;
        for (normal, matrix) in [
            ([0, 4096, 0], tankracer_game::math::Matrix::IDENTITY),
            (
                [-1537, 3073, 1025],
                tankracer_game::math::Matrix::from_angles([713, 3901, 917]),
            ),
            (
                [-32768, 32767, -16383],
                tankracer_game::math::Matrix([
                    i32::MIN,
                    i32::MAX,
                    -8191,
                    17,
                    -31,
                    4097,
                    -65535,
                    8193,
                    -32768,
                ]),
            ),
            (
                [317, -901, 1341],
                tankracer_game::math::Matrix::from_angles([31731, 13391, 719]),
            ),
        ] {
            let data = MeshData::quad(0.);
            let stream = vec![
                [
                    f32::from(normal[0]),
                    f32::from(normal[1]),
                    f32::from(normal[2]),
                    1.
                ];
                4
            ];
            let mesh = renderer.upload_mesh_streams(&data, &[], &stream)?;
            let uv = tankracer_scene::vehicle::layer::uv(normal, matrix)
                .map(|v| (v as f32 / 65536.).clamp(0., 1.));
            let m = matrix.0;
            let material = Material {
                normal_layer: Some(NormalLayer {
                    rows: [[m[0], m[1], m[2]], [m[6], m[7], m[8]]],
                    atlas: [0., 0., 1., 1.],
                    gain: 1.,
                }),
                ..Material::opaque([0., 0., 0., 1.])
            };
            let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
            frame.draw(&mesh, &material, Some(&texture), Mat4::IDENTITY);
            expect(
                &frame,
                "Q12 authored normal GPU projection",
                [
                    (uv[0] * 64. - 0.5).clamp(0., 63.) * 4. / 255.,
                    (uv[1] * 64. - 0.5).clamp(0., 63.) * 4. / 255.,
                    50. / 255.,
                    1.,
                ],
            )?;
            cases += 1;
        }
        let texture =
            renderer.upload_rgba(3, 1, &[51, 102, 153, 255, 64, 32, 16, 128, 0, 0, 0, 0])?;
        let mut data = MeshData::quad(0.);
        for v in &mut data.vertices {
            v.uv = [1. / 6., 0.5];
        }
        let mesh = renderer.upload_mesh_streams(&data, &[], &[[0., 4096., 0., 1.]; 4])?;
        let mut layer = Material {
            texture_alpha: AlphaEncoding::Premultiplied,
            normal_layer: Some(NormalLayer {
                rows: [[4096, 0, 0], [0, 0, 4096]],
                atlas: [1. / 3., 0., 1. / 3., 1.],
                gain: 128. / 255.,
            }),
            ..Material::opaque([1.; 4])
        };
        for gain in [128. / 255., 32. / 255.] {
            layer.normal_layer.as_mut().unwrap().gain = gain;
            let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
            frame.draw(&mesh, &layer, Some(&texture), Mat4::IDENTITY);
            expect(
                &frame,
                "base plus original additive layer gain",
                [
                    (51. + 64. * gain) / 255.,
                    (102. + 32. * gain) / 255.,
                    (153. + 16. * gain) / 255.,
                    1.,
                ],
            )?;
            cases += 1;
        }
        let plain = Material::opaque([0., 1., 0., 1.]);
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(
            &mesh,
            &plain,
            None,
            Mat4::from_translation(glam::Vec3::new(0., 0., -0.5)),
        );
        frame.draw(&mesh, &layer, Some(&texture), Mat4::IDENTITY);
        expect(
            &frame,
            "foreground occludes both base and normal layer",
            plain.color,
        )?;
        cases += 1;
        let hidden = renderer.upload_mesh_streams(&data, &[], &[[0., 4096., 0., 0.]; 4])?;
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(&hidden, &layer, Some(&texture), Mat4::IDENTITY);
        expect(
            &frame,
            "face Y threshold disables only the layer",
            [51. / 255., 102. / 255., 153. / 255., 1.],
        )?;
        cases += 1;
        for v in &mut data.vertices {
            v.uv = [5. / 6., 0.5];
        }
        let hole = renderer.upload_mesh_streams(&data, &[], &[[0., 4096., 0., 1.]; 4])?;
        layer.alpha_cutoff = Some(0.5);
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(&hole, &layer, Some(&texture), Mat4::IDENTITY);
        expect(
            &frame,
            "normal layer cannot fill base alpha cutouts",
            BACKGROUND,
        )?;
        cases += 1;
        frame.draw(&mesh, &plain, None, Mat4::IDENTITY);
        expect(
            &frame,
            "ordinary draw resets normal layer and normal VAO",
            plain.color,
        )?;
        cases += 1;
        for v in &mut data.vertices {
            v.uv = [1. / 6., 0.5];
        }
        let no_stream = renderer.upload_mesh(&data)?;
        layer.alpha_cutoff = None;
        let mut frame = renderer.begin_frame(Some(&target), [4, 4], BACKGROUND);
        frame.draw(&no_stream, &layer, Some(&texture), Mat4::IDENTITY);
        expect(
            &frame,
            "normal stream does not leak into a different VAO",
            [51. / 255., 102. / 255., 153. / 255., 1.],
        )?;
        cases += 1;
    }
    renderer.check_error()?;
    Ok(format!(
        "{{\"passed\":true,\"pixel_cases\":{cases},\"depth_comparisons\":8,\"blend_transitions\":81}}"
    ))
}
