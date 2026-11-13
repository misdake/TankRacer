//! Inputs captured at the post-ground boundary of a real C vehicle update.
use std::{path::PathBuf, process::Command};
use tankracer_assets::config::{Damping, Gain};
use tankracer_game::{attitude, body, contact::Coefficient, math::Matrix, motion, pose};

struct Reader {
    words: Vec<u32>,
    at: usize,
}
impl Reader {
    fn word(&mut self) -> u32 {
        let v = self.words[self.at];
        self.at += 1;
        v
    }
    fn signed(&mut self) -> i32 {
        self.word() as i32
    }
    fn vector<const N: usize>(&mut self) -> [i32; N] {
        std::array::from_fn(|_| self.signed())
    }
    fn pose(&mut self) -> pose::Pose {
        pose::Pose {
            position: self.vector(),
            matrix: Matrix(self.vector()),
        }
    }
    fn spring(&mut self) -> body::Spring {
        body::Spring {
            position: self.signed(),
            rate: self.signed(),
        }
    }
    fn coefficient(&mut self) -> Coefficient {
        Coefficient {
            multiplier: self.word() as u16,
            shift: self.word() as u8,
        }
    }
    fn state(&mut self) -> motion::State {
        let poses = pose::State {
            physical_position: self.vector(),
            reference: Matrix(self.vector()),
            base_position: self.vector(),
            body: self.pose(),
            tracks: [self.pose(), self.pose()],
            turret: self.pose(),
            gun: self.pose(),
            attachments: [self.pose(), self.pose()],
            bottom_position: self.vector(),
            recoil_factor: self.word() as u8,
            recoil_shift: self.word() as u8,
        };
        let previous_position = self.vector();
        let velocity = self.vector();
        let angular = self.vector();
        let acceleration = self.vector();
        let pivot = self.vector();
        let articulation = body::Articulation {
            recoil: self.signed(),
            recoil_rate: self.signed(),
            pitch: self.signed(),
            pitch_rate: self.signed(),
        };
        let lean = body::Lean {
            lateral: self.spring(),
            longitudinal: self.spring(),
        };
        let contacts = std::array::from_fn(|_| body::Contact {
            world: self.vector(),
            gap: self.signed(),
        });
        let travel = self.vector();
        let hull = body::Hull {
            position: self.vector(),
            matrix: Matrix(self.vector()),
            vertices: std::array::from_fn(|_| self.vector().map(|v| v as i16)),
            radius: self.word() as u16,
        };
        let ready = self.word() != 0;
        let rates = motion::Rates {
            travel_sum: self.signed(),
            penetrating_force: self.signed(),
            lateral_velocity: self.signed(),
            track_slip: self.signed(),
        };
        motion::State {
            poses,
            previous_position,
            velocity,
            angular,
            acceleration,
            pivot,
            articulation,
            lean,
            contacts,
            travel,
            hull,
            ready,
            rates,
        }
    }
    fn vertices(&mut self, count: usize) -> Vec<[i16; 3]> {
        (0..count)
            .map(|_| self.vector().map(|v| v as i16))
            .collect()
    }
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn words(s: &motion::State) -> Vec<u32> {
    let p = &s.poses;
    let mut out = Vec::new();
    append(&mut out, p.physical_position);
    append(&mut out, p.reference.0);
    append(&mut out, p.base_position);
    for p in [
        p.body,
        p.tracks[0],
        p.tracks[1],
        p.turret,
        p.gun,
        p.attachments[0],
        p.attachments[1],
    ] {
        append(&mut out, p.position);
        append(&mut out, p.matrix.0);
    }
    append(&mut out, p.bottom_position);
    out.extend([u32::from(p.recoil_factor), u32::from(p.recoil_shift)]);
    append(&mut out, s.previous_position);
    append(&mut out, s.velocity);
    append(&mut out, s.angular);
    append(&mut out, s.acceleration);
    append(&mut out, s.pivot);
    append(
        &mut out,
        [
            s.articulation.recoil,
            s.articulation.recoil_rate,
            s.articulation.pitch,
            s.articulation.pitch_rate,
        ],
    );
    for spring in [s.lean.lateral, s.lean.longitudinal] {
        append(&mut out, [spring.position, spring.rate]);
    }
    for c in s.contacts {
        append(&mut out, c.world);
        out.push(c.gap as u32);
    }
    append(&mut out, s.travel);
    append(&mut out, s.hull.position);
    append(&mut out, s.hull.matrix.0);
    for v in s.hull.vertices {
        append(&mut out, v.map(i32::from));
    }
    out.extend([u32::from(s.hull.radius), u32::from(s.ready)]);
    append(
        &mut out,
        [
            s.rates.travel_sum,
            s.rates.penetrating_force,
            s.rates.lateral_velocity,
            s.rates.track_slip,
        ],
    );
    assert_eq!(out.len(), 176);
    out
}

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
