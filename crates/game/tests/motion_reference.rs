//! Inputs captured at the post-ground boundary of a real C vehicle update.
use std::{path::PathBuf, process::Command};
use tankracer_assets::config::{Damping, Gain};
use tankracer_game::{attitude, body, math::Matrix, motion, pose};

#[path = "common/motion.rs"]
mod common;
use common::{Reader, words};

#[test]
#[ignore = "requires full native C motion oracle; set TANKRACER_MOTION_REFERENCE"]
fn actual_vehicle_update_motion_pose_hull_and_error_boundaries_match_c() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let executable =
        std::env::var_os("TANKRACER_MOTION_REFERENCE").expect("set TANKRACER_MOTION_REFERENCE");
    let mut records = 0;
    let mut errors = [0; 16];
    let mut callbacks = 0;
    let mut total_words = 0;
    for (map, vehicle) in (1..=15).map(|v| (0, v)).chain((1..8).map(|m| (m, 1))) {
        let output = Command::new(&executable)
            .arg(&root)
            .arg(map.to_string())
            .arg(vehicle.to_string())
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "map={map} vehicle={vehicle} {:?}: {}",
            output.status.code(),
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], b"MO01");
        let mut r = Reader {
            words: output.stdout[4..]
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
                .collect(),
            at: 0,
        };
        while r.at < r.words.len() {
            let variant = r.word() as usize;
            let step = r.word();
            let dt = r.signed();
            let number = r.word();
            let player = r.word() != 0;
            let mode = r.signed();
            let timer = r.word() as u16;
            let ambient_wobble = r.word() != 0;
            let session_active = r.word() != 0;
            let ambient_phase = r.word();
            let recoil_mode = r.signed();
            let turret_yaw = r.signed() as i16;
            let tag = r.word();
            let matrix = Matrix(r.vector());
            let external = match tag {
                0 => pose::ExternalBasis::None,
                1 => pose::ExternalBasis::Body,
                2 => pose::ExternalBasis::Custom(matrix),
                _ => panic!("invalid external basis"),
            };
            let frame = motion::Frame {
                dt,
                number,
                player,
                mode,
                pose: motion::PoseInput {
                    timer,
                    ambient_wobble,
                    session_active,
                    ambient_phase,
                    recoil_mode,
                    turret_yaw,
                    external,
                },
            };
            let attitude = attitude::Config {
                longitudinal: r.coefficient(),
                lateral: r.coefficient(),
            };
            let recoil_decay = r.word() as u8;
            let recoil_step = r.signed() as i16;
            let pitch = r.word();
            let lean_decay = r.word() as u8;
            let body = body::Tuning {
                recoil_decay,
                recoil_step,
                pitch_damping: Damping {
                    step: pitch as i16,
                    fraction: (pitch >> 16) as u8,
                    shift: (pitch >> 24) as u8,
                },
                lean_decay,
            };
            let track_offsets = [r.vector(), r.vector()];
            let turret_offset = r.vector();
            let gun_offset = r.vector();
            let attachment_offsets = [r.vector(), r.vector()];
            let bottom_offset = r.vector();
            let track_length = r.signed();
            let gain = r.word();
            let gun_pivot = r.vector();
            let pose = pose::Config {
                track_offsets,
                turret_offset,
                gun_offset,
                attachment_offsets,
                bottom_offset,
                track_length,
                travel_gain: Gain {
                    multiplier: gain as u16,
                    shift: (gain >> 16) as u16,
                },
                gun_pivot,
            };
            let config = motion::Config {
                attitude: &attitude,
                body: &body,
                pose: &pose,
            };
            let mut state = r.state();
            let has_gun = r.word() != 0;
            let indices = r.vector().map(|v| v as u8);
            let original_z = r.vector().map(|v| v as i16);
            let count = r.word() as usize;
            let mut vertices = r.vertices(count);
            let expected_status = r.signed();
            let expected = r.state();
            let expected_vertices = r.vertices(count);
            let expected_calls = r.word();
            let mut called = 0;
            let result = state.finish(
                &config,
                &frame,
                has_gun.then_some(body::GunMesh {
                    vertices: &mut vertices,
                    indices,
                    original_z,
                }),
                |s| {
                    called += 1;
                    if variant == 6 || variant == 15 {
                        s.poses.physical_position[0] =
                            s.poses.physical_position[0].wrapping_add(123);
                        s.contacts[0].gap = s.contacts[0].gap.wrapping_add(77);
                        s.rates.travel_sum = 123456;
                    }
                    match variant {
                        4 => Err(11),
                        5 => Err(-19),
                        _ => Ok(()),
                    }
                },
            );
            let status = result.map_or_else(motion::Error::status, |()| 0);
            assert_eq!(
                status, expected_status,
                "map={map} vehicle={vehicle} variant={variant} step={step}"
            );
            let actual = words(&state);
            let expected = words(&expected);
            for (field, (a, b)) in actual.iter().zip(&expected).enumerate() {
                assert_eq!(
                    a, b,
                    "map={map} vehicle={vehicle} variant={variant} step={step} field={field}"
                );
            }
            assert_eq!(
                vertices, expected_vertices,
                "gun: map={map} vehicle={vehicle} variant={variant} step={step}"
            );
            assert_eq!(called, expected_calls);
            errors[variant] += usize::from(status != 0);
            callbacks += called;
            records += 1;
            total_words += actual.len() + count * 3 + 2;
        }
    }
    assert!(
        errors[2] > 0 && errors[3] > 0 && errors[4] > 0 && errors[5] > 0,
        "error coverage {errors:?}"
    );
    assert!(callbacks > 0);
    eprintln!(
        "{records} real-update motion records / {total_words} output words exact; callbacks={callbacks}, errors={errors:?}"
    );
}
