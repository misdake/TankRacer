//! Real threshold, wrap, hold/release, pickup suppression and restore branches.
use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_game::{
    drive,
    player_brake::{Frame, Reference, State, Vehicle},
};
#[derive(Clone)]
struct Case {
    state: State,
    input: drive::Input,
    max_turn: i32,
    yaw_lock: i32,
    frame: Frame,
    reference: Reference,
    restore: bool,
}
impl Default for Case {
    fn default() -> Self {
        Self {
            state: State::default(),
            input: drive::Input {
                throttle: 60000,
                steering: 12000,
                alternate_lateral: 256,
                alternate_rotation: 12,
                ..Default::default()
            },
            max_turn: 100,
            yaw_lock: 77,
            frame: Frame {
                dt: 16,
                speed: 250001,
                mode: 0,
                temporary_timer: 0,
                down: true,
            },
            reference: Reference {
                slide: 256,
                rotation: 12,
            },
            restore: false,
        }
    }
}
fn values(c: &Case) -> Vec<u32> {
    [
        c.input.throttle,
        c.input.brake,
        c.input.steering,
        i32::from(c.input.alternate),
        i32::from(c.input.alternate_lateral),
        i32::from(c.input.alternate_rotation),
        i32::from(c.state.saved_turn),
        i32::from(c.state.reverse_braking),
        c.max_turn,
        c.yaw_lock,
    ]
    .map(|v| v as u32)
    .to_vec()
}
#[test]
#[ignore = "requires native brake oracle; set TANKRACER_PLAYER_BRAKE_REFERENCE"]
fn auxiliary_brake_and_restore_match_c_by_branch_and_short_sequence() {
    let mut cases = vec![];
    for mode in [0, 1, 4, 5] {
        for speed in [
            i32::MIN,
            -250001,
            -250000,
            -249999,
            0,
            249999,
            250000,
            250001,
            i32::MAX,
        ] {
            for down in [false, true] {
                let mut c = Case::default();
                c.frame.mode = mode;
                c.frame.speed = speed;
                c.frame.down = down;
                cases.push(c);
            }
        }
    }
    for kind in 0..25 {
        let mut c = Case::default();
        c.input.alternate = true;
        c.state.saved_turn = 100;
        match kind {
            0 => c.frame.dt = 0,
            1 => c.frame.dt = -16,
            2 => c.frame.dt = i32::MAX,
            3 => c.frame.dt = 16384,
            4 => c.frame.dt = 3,
            5 => c.input.alternate_lateral = i16::MIN,
            6 => c.input.alternate_lateral = i16::MAX,
            7 => c.input.alternate_rotation = i16::MIN,
            8 => c.input.alternate_rotation = i16::MAX,
            9 => c.reference.slide = 65535,
            10 => c.reference.slide = 32768,
            11 => c.reference.slide = 0,
            12 => c.reference.rotation = -20,
            13 => c.max_turn = i32::MIN,
            14 => c.max_turn = i32::MAX,
            15 => {
                c.input.alternate = false;
                c.max_turn = 65535;
            }
            16 => c.frame.temporary_timer = 1,
            17 => c.frame.temporary_timer = -1,
            18 => {
                c.frame.temporary_timer = 1;
                c.input.alternate = false;
                c.state.reverse_braking = true;
            }
            19 => {
                c.state.saved_turn = -32768;
                c.frame.down = false;
            }
            20 => {
                c.restore = true;
                c.input.alternate = false;
            }
            21 => {
                c.restore = true;
                c.frame.mode = 4;
                c.max_turn = 100;
            }
            22 => {
                c.restore = true;
                c.frame.mode = 4;
                c.input.alternate_lateral = 0;
                c.input.alternate_rotation = 0;
            }
            23 => {
                c.restore = true;
                c.frame.mode = 4;
                c.reference.slide = 65535;
            }
            _ => {
                c.frame.down = false;
                c.max_turn = 50;
            }
        }
        cases.push(c);
    }
    let mut bytes = vec![];
    let mut expected = vec![];
    for (case, mut c) in cases.iter().cloned().enumerate() {
        for tick in 0..12 {
            let mut record = values(&c);
            record.extend([
                c.frame.dt as u32,
                c.frame.speed as u32,
                c.frame.mode as u32,
                c.frame.temporary_timer as i32 as u32,
                u32::from(c.frame.down),
                u32::from(c.reference.slide),
                c.reference.rotation as i32 as u32,
                u32::from(c.restore),
            ]);
            bytes.extend(record.into_iter().flat_map(u32::to_le_bytes));
            let vehicle = Vehicle {
                input: &mut c.input,
                max_turn: &mut c.max_turn,
                yaw_lock: &mut c.yaw_lock,
            };
            if c.restore {
                c.state
                    .restore(vehicle, c.frame.dt, c.frame.mode, c.reference);
            } else {
                c.state.step(vehicle, c.frame, c.reference);
            }
            let mut out = vec![0];
            out.extend(values(&c));
            expected.push((case, tick, out));
            if tick == 2 {
                c.frame.down = false;
            }
            if tick == 5 {
                c.frame.temporary_timer = 1;
            }
            if tick == 6 {
                c.frame.temporary_timer = 0;
            }
        }
    }
    let mut child = Command::new(
        std::env::var_os("TANKRACER_PLAYER_BRAKE_REFERENCE").expect("set brake oracle"),
    )
    .stdin(Stdio::piped())
    .stdout(Stdio::piped())
    .stderr(Stdio::piped())
    .spawn()
    .unwrap();
    let mut stdin = child.stdin.take().unwrap();
    let writer = std::thread::spawn(move || stdin.write_all(&bytes).unwrap());
    let output = child.wait_with_output().unwrap();
    writer.join().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"PB01");
    let words: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    let mut at = 0;
    for (case, tick, out) in &expected {
        for (field, value) in out.iter().enumerate() {
            assert_eq!(*value, words[at], "case={case} tick={tick} field={field}");
            at += 1;
        }
    }
    assert_eq!(at, words.len());
    println!(
        "Player brake: {} cases / {} operations / {at} words",
        cases.len(),
        expected.len()
    );
}
