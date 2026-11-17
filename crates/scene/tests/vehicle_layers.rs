use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_assets::config::Part;
use tankracer_game::{math::Matrix, pose::Pose};
use tankracer_scene::vehicle::{aerial, layer};

#[test]
fn layer_gates_keep_end_exception_and_cockpit_isolation() {
    let mut c = layer::Context::default();
    for p in [Part::Body, Part::Turret, Part::End] {
        assert_eq!(c.mode(p), 1);
    }
    assert_eq!(c.mode(Part::Gun), 0);
    c.shield_timer = 1;
    c.invincible = true;
    assert_eq!(c.mode(Part::Body), 0);
    assert_eq!(c.mode(Part::Turret), 0);
    assert_eq!(c.mode(Part::End), 1);
    c.starting = true;
    assert_eq!(c.mode(Part::End), 2);
    c.cockpit = true;
    assert_eq!(c.mode(Part::End), 0);
    let m = layer::material(
        Matrix::IDENTITY,
        Matrix::IDENTITY,
        1,
        [0., 0., 1., 1.],
        true,
    )
    .unwrap();
    assert_eq!(m.gain, 32. / 255.);
}

#[test]
#[ignore = "requires TANKRACER_VEHICLE_LAYERS_REFERENCE native C oracle"]
fn original_gates_normals_composition_and_four_segment_aerial_match_c() {
    let mut input = Vec::<u32>::new();
    let mut expected = Vec::new();
    let mut gates = 0;
    let mut projections = 0;
    let mut aerials = 0;
    for part in [0, 1, 2, 6, 7, 8] {
        for hardware in [false, true] {
            for player in [false, true] {
                for game_mode in [0, 1, 2, 3, 4] {
                    for starting in [false, true] {
                        for shield in [-1, 0, 1] {
                            for invincible in [false, true] {
                                let c = layer::Context {
                                    additive_capability: hardware,
                                    player,
                                    game_mode,
                                    starting,
                                    shield_timer: shield,
                                    invincible,
                                    ..Default::default()
                                };
                                let p = match part {
                                    0 => Part::Body,
                                    1 => Part::Turret,
                                    6 => Part::End,
                                    2 => Part::Gun,
                                    _ => Part::Aerial,
                                };
                                input.extend([
                                    0,
                                    part,
                                    u32::from(hardware),
                                    u32::from(player),
                                    game_mode as u32,
                                    u32::from(starting),
                                    shield as u32,
                                    u32::from(invincible),
                                ]);
                                expected.push(c.mode(p));
                                gates += 1;
                            }
                        }
                    }
                }
            }
        }
    }
    for mode in [1, 2] {
        for index in 0..18_u32 {
            let model = if index == 17 {
                Matrix([
                    i32::MIN,
                    2147483647,
                    -8191,
                    17,
                    -31,
                    4097,
                    -65535,
                    8193,
                    -32768,
                ])
            } else {
                Matrix::from_angles([index * 1731, index * 391, index * 917])
            };
            let view = Matrix::from_angles([index * 371, index * 1901, index * 37]);
            let normal = if index % 3 == 0 {
                [-32768, 32767, -16383]
            } else {
                [-1537, 3073, 1025]
            };
            input.extend([1, mode]);
            input.extend(model.0.map(|v| v as u32));
            input.extend(view.0.map(|v| v as u32));
            input.extend(normal.map(|v: i16| v as i32 as u32));
            let joined = layer::matrix(model, view, mode);
            expected.extend(joined.0.map(|v| v as u32));
            expected.extend(layer::uv(normal, joined).map(|v| v as u32));
            projections += 1;
        }
    }
    for index in 0..18_u32 {
        let body = Matrix::from_angles([index * 713, index * 193, index * 917]);
        let turret = Matrix::from_angles([index * 133, index * 1193, index * 17]);
        let socket = [-357, 811, -173];
        let origin = [123456, -23456, 345678];
        let offset = turret.local_to_world(socket);
        let position = std::array::from_fn(|i| origin[i] + offset[i]);
        let (pitch, roll, height) = (index as i32 * 391 - 3000, 2000 - index as i32 * 713, 517);
        for gate in 0..4 {
            input.extend([
                2,
                u32::from(gate != 1),
                u32::from(gate != 2),
                u32::from(gate == 3),
            ]);
            input.extend(socket.map(|v| v as u32));
            input.extend(origin.map(|v| v as u32));
            input.extend(turret.0.map(|v| v as u32));
            input.extend(body.0.map(|v| v as u32));
            input.extend([pitch as u32, roll as u32, height as u32]);
            if gate == 0 {
                for p in aerial::chain(
                    Pose {
                        position,
                        matrix: body,
                    },
                    height,
                    pitch,
                    roll,
                ) {
                    expected.extend(p.position.map(|v| v as u32));
                    expected.extend(p.matrix.0.map(|v| v as u32));
                }
                expected.push(4);
            } else {
                expected.push(0);
            }
            aerials += 1;
        }
    }
    let mut child =
        Command::new(std::env::var_os("TANKRACER_VEHICLE_LAYERS_REFERENCE").expect("C oracle"))
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()
            .unwrap();
    // The exhaustive gates exceed a pipe buffer; drain stdout concurrently.
    let mut stdin = child.stdin.take().unwrap();
    let writer = std::thread::spawn(move || {
        stdin
            .write_all(
                &input
                    .into_iter()
                    .flat_map(u32::to_le_bytes)
                    .collect::<Vec<_>>(),
            )
            .unwrap()
    });
    let output = child.wait_with_output().unwrap();
    writer.join().unwrap();
    assert!(output.status.success());
    assert_eq!(&output.stdout[..4], b"VL01");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(actual, expected);
    eprintln!(
        "vehicle layers C: {gates} gates / {projections} normal projections / {aerials} aerial branches / {} fields",
        actual.len()
    );
}
