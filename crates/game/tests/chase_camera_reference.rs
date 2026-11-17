use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_assets::{archive::Archive, object_catalog::Catalog};
use tankracer_game::{
    chase_camera::{Config, Frame, Keys, Shake, State, Subject},
    math::Matrix,
};

fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn config(c: &Config) -> Vec<u32> {
    let mut out = Vec::new();
    for a in [c.near, c.far] {
        append(&mut out, a);
    }
    append(&mut out, c.pitch);
    append(&mut out, c.overhead);
    append(&mut out, [c.overhead_pitch]);
    append(&mut out, c.cockpit);
    append(&mut out, c.rotation_speed);
    append(&mut out, [c.yaw_scale]);
    out.extend(c.cockpit_drag);
    out
}
fn encode(s: &State) -> Vec<u32> {
    let mut out = Vec::new();
    append(&mut out, s.position);
    append(&mut out, s.matrix.0);
    out.push(s.mode);
    append(&mut out, s.angles);
    append(&mut out, [s.pitch_offset]);
    append(&mut out, s.displacement);
    append(&mut out, s.cockpit_angles);
    append(&mut out, s.cockpit_local);
    out.extend([u32::from(s.cycle_held), u32::from(s.rear)]);
    append(
        &mut out,
        [
            s.shake.amplitude,
            s.shake.position,
            s.shake.speed,
            i32::from(s.shake.remaining),
            i32::from(s.shake.duration),
            i32::from(s.shake.rising),
        ],
    );
    out
}
#[test]
fn authored_camera_config_uses_native_units_and_defaults() {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../../assets/game/DATA/FASTFILE.DAT");
    let bytes = std::fs::read(root).unwrap();
    let archive = Archive::parse(&bytes).unwrap();
    let c = Catalog::decode(archive.get("OBPARAMS.DAT").unwrap())
        .unwrap()
        .camera;
    assert_eq!(c.near, [0, 2000, -10000]);
    assert_eq!(c.pitch[0], -1638);
    assert_eq!(c.rotation_speed, [9, 18]);
    assert_eq!(c.yaw_scale, 4550);
    assert_eq!(c.far, [0, 2200, -12250]);
    assert_eq!(c.cockpit, [0; 3]);
    assert_eq!(c.cockpit_drag, [0x0701000f; 2]);
}
#[test]
#[ignore = "requires TANKRACER_CHASE_CAMERA_REFERENCE native C oracle"]
fn all_chase_modes_and_feedback_short_sequences_match_c() {
    let exe = std::env::var_os("TANKRACER_CHASE_CAMERA_REFERENCE")
        .expect("TANKRACER_CHASE_CAMERA_REFERENCE");
    let mut input = Vec::new();
    let mut expected = Vec::new();
    let mut calls = 0;
    let c = Config {
        near: [0, 2000, -10000],
        rotation_speed: [9, 18],
        yaw_scale: 4550,
        cockpit_drag: [0x09010001; 2],
        ..Default::default()
    };
    for scenario in 0..16 {
        let c = Config {
            cockpit_drag: if scenario >= 12 {
                [0x0701000f; 2]
            } else {
                c.cockpit_drag
            },
            ..c
        };
        let mut subject = Subject {
            base: [10000, 1200, 5000],
            physical: [9000, 1300, 4900],
            basis: Matrix::from_angles([scenario * 391, scenario * 997, scenario * 717]),
        };
        let mut state = State::new(&c, subject).unwrap();
        state.mode = scenario % 3;
        state.angles = [-32761, scenario as i32 * 731 - 10000, scenario as i32 * 197];
        if scenario >= 6 {
            state.shake = Shake {
                amplitude: 2000,
                position: if scenario % 2 == 0 { 2000 } else { -2000 },
                speed: 2999,
                remaining: 1000,
                duration: 1000,
                rising: scenario % 2 == 0,
            };
        }
        input.push(0);
        input.extend(config(&c));
        input.extend(encode(&state));
        if scenario >= 12 {
            let duration = if scenario % 2 == 0 { 16 } else { 2500 };
            input.extend([3, duration, 2000]);
            state.shake.start(duration, 2000);
        }
        for tick in 0..12 {
            subject.physical[0] += 17;
            subject.base[2] += 71;
            let frame = Frame {
                dt: [1, 16, 32][tick % 3],
                keys: Keys {
                    cycle: (2..5).contains(&tick) || tick == 8,
                    rear: (5..8).contains(&tick),
                },
                starting: scenario == 3,
                overhead: scenario == 4,
                hold_displacement: scenario == 5,
                force_restore: tick == 10,
            };
            input.push(1);
            append(&mut input, subject.base);
            append(&mut input, subject.physical);
            append(&mut input, subject.basis.0);
            append(&mut input, [frame.dt]);
            input.extend(
                [
                    frame.keys.cycle,
                    frame.keys.rear,
                    frame.starting,
                    frame.overhead,
                    frame.hold_displacement,
                    frame.force_restore,
                ]
                .map(u32::from),
            );
            state.tick(&c, subject, frame).unwrap();
            expected.push(0);
            expected.extend(encode(&state));
            calls += 1;
            let feedback = [
                tick as i32 * 7,
                792 + tick as i32 * 3,
                -264 - tick as i32 * 5,
            ];
            input.push(2);
            append(&mut input, feedback);
            state.feedback(feedback);
        }
    }
    let mut child = Command::new(exe)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .spawn()
        .unwrap();
    child
        .stdin
        .take()
        .unwrap()
        .write_all(
            &input
                .into_iter()
                .flat_map(u32::to_le_bytes)
                .collect::<Vec<_>>(),
        )
        .unwrap();
    let output = child.wait_with_output().unwrap();
    assert!(output.status.success());
    assert_eq!(&output.stdout[..4], b"CC01");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    for (i, (a, b)) in actual.iter().zip(&expected).enumerate() {
        assert_eq!(
            a,
            b,
            "camera field {i} (per-call {} words)",
            encode(
                &State::new(
                    &c,
                    Subject {
                        base: [0; 3],
                        physical: [0; 3],
                        basis: Matrix::IDENTITY
                    }
                )
                .unwrap()
            )
            .len()
                + 1
        );
    }
    assert_eq!(actual.len(), expected.len());
    eprintln!("chase camera: {calls} updates / {} fields", actual.len());
}
