//! Branches, device query order/error prefixes and short CUP input sequences.
use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_game::{
    drive,
    player_input::{Action, Axis, Device, Frame, Shared, State, Tuning},
};

#[derive(Clone)]
struct Case {
    state: State,
    countdown: i32,
    shared: Shared,
    frame: Frame,
    tuning: Tuning,
    keys: u32,
    axes: [i32; 2],
    fault: u32,
    trace: Vec<u32>,
}
impl Default for Case {
    fn default() -> Self {
        Self {
            state: State::default(),
            countdown: 0,
            shared: Shared::default(),
            frame: Frame {
                dt: 16,
                driving: true,
                player: 0,
                speed: 0,
                start_stage: 0,
                analog: false,
                analog_longitudinal: false,
                twin_tracks: false,
                track_values: [0; 2],
            },
            tuning: Tuning::default(),
            keys: 0,
            axes: [0; 2],
            fault: u32::MAX,
            trace: vec![],
        }
    }
}
struct Sample<'a> {
    keys: u32,
    axes: [i32; 2],
    fault: u32,
    trace: &'a mut Vec<u32>,
}
impl Device for Sample<'_> {
    fn down(&mut self, action: Action) -> Result<bool, i32> {
        self.trace.push(action as u32);
        Ok(self.keys & (1 << action as u32) != 0)
    }
    fn axis(&mut self, axis: Axis) -> Result<i32, i32> {
        let index = match axis {
            Axis::Steering => 0,
            Axis::Longitudinal => 1,
        };
        self.trace.push(100 + index as u32);
        if self.fault == index as u32 {
            Err(-1)
        } else {
            Ok(self.axes[index])
        }
    }
}
fn record(c: &Case) -> Vec<u32> {
    let s = c.state;
    let f = c.frame;
    let t = c.tuning;
    [
        c.countdown,
        s.drive.throttle,
        s.drive.brake,
        s.drive.steering,
        i32::from(s.start_held),
        i32::from(s.fast_start),
        i32::from(c.shared.steering_analog_latch),
        f.dt,
        i32::from(f.driving),
        i32::from(f.player),
        f.speed,
        f.start_stage,
        i32::from(f.analog),
        i32::from(f.analog_longitudinal),
        i32::from(f.twin_tracks),
        f.track_values[0],
        f.track_values[1],
        t.push_throttle,
        t.push_brake,
        t.push_steer,
        i32::from(t.return_throttle),
        i32::from(t.return_brake),
        t.return_steer,
        c.keys as i32,
        c.axes[0],
        c.axes[1],
        c.fault as i32,
    ]
    .map(|v| v as u32)
    .to_vec()
}
#[test]
#[ignore = "requires native input oracle; set TANKRACER_PLAYER_INPUT_REFERENCE"]
fn cup_countdown_driving_and_query_prefixes_match_c() {
    let mut cases = vec![];
    // Every simultaneous digital combination, at forward/reverse/zero speed.
    for mask in 0..32 {
        for speed in [-1, 0, 1] {
            let mut c = Case::default();
            c.frame.speed = speed;
            for (bit, action) in [1, 2, 5, 6, 15].into_iter().enumerate() {
                if mask & (1 << bit) != 0 {
                    c.keys |= 1 << action;
                }
            }
            cases.push(c);
        }
    }
    for stage in [-1, 0, 1, 2, 3, 4] {
        for held in [0, 1, 255] {
            for throttle in [false, true] {
                let mut c = Case::default();
                c.frame.driving = false;
                c.frame.start_stage = stage;
                c.state.start_held = held;
                c.state.drive.brake = 90000;
                c.state.drive.steering = -80000;
                c.keys = if throttle { 1 << 5 } else { 0 };
                cases.push(c);
            }
        }
    }
    for driving in [false, true] {
        for speed in [-1, 0, 1] {
            for axis in [-32768, -16385, -16384, -1, 0, 1, 32767] {
                let mut c = Case::default();
                c.frame.driving = driving;
                c.frame.speed = speed;
                c.frame.analog = true;
                c.frame.analog_longitudinal = true;
                c.frame.start_stage = 2;
                c.axes = [axis, axis];
                cases.push(c);
            }
        }
    }
    for player in [-1, 0, 1] {
        for tracks in [
            [-32768, 32767],
            [20000, 20000],
            [-20000, -20000],
            [i32::MAX, 1],
        ] {
            for speed in [-1, 0, 1] {
                let mut c = Case::default();
                c.frame.player = player;
                c.frame.analog = true;
                c.frame.twin_tracks = true;
                c.frame.track_values = tracks;
                c.frame.speed = speed;
                c.keys = u32::MAX;
                cases.push(c);
            }
        }
    }
    for kind in 0..16 {
        let mut c = Case::default();
        c.state.drive = drive::Input {
            throttle: i32::MAX,
            brake: -123,
            steering: i32::MIN,
            ..Default::default()
        };
        c.countdown = i32::MAX;
        c.keys = (1 << 5) | (1 << 1);
        match kind {
            0 => c.frame.dt = i32::MAX,
            1 => c.frame.dt = 0,
            2 => c.frame.dt = -16,
            3 => c.tuning.return_throttle = -32768,
            4 => c.tuning.return_brake = -2,
            5 => c.tuning.return_steer = i32::MAX,
            6 => c.tuning.push_throttle = i32::MIN,
            7 => c.frame.player = 1,
            8 => c.frame.player = -1,
            9 => {
                c.keys = 0;
                c.shared.steering_analog_latch = true;
            }
            10 => c.fault = 0,
            11 => {
                c.frame.analog = true;
                c.frame.analog_longitudinal = true;
                c.keys = 0;
                c.fault = 1;
            }
            12 => {
                c.frame.driving = false;
                c.frame.analog = true;
                c.frame.analog_longitudinal = true;
                c.keys = 0;
                c.fault = 1;
            }
            13 => {
                c.frame.analog = true;
                c.axes = [100, 100];
                c.keys |= 1 << 2;
            }
            14 => {
                c.frame.driving = false;
                c.tuning.return_throttle = -1;
                c.keys = 0;
            }
            _ => {
                c.keys = 0;
                c.tuning.return_steer = -5;
            }
        }
        cases.push(c);
    }
    let mut bytes = vec![];
    let mut expected = vec![];
    for (case, mut c) in cases.iter().cloned().enumerate() {
        for step in 0..6 {
            c.trace.clear();
            bytes.extend(record(&c).into_iter().flat_map(u32::to_le_bytes));
            let result = c.state.step(
                &mut c.countdown,
                &mut c.shared,
                c.frame,
                c.tuning,
                &mut Sample {
                    keys: c.keys,
                    axes: c.axes,
                    fault: c.fault,
                    trace: &mut c.trace,
                },
            );
            let mut out = vec![result.as_ref().err().copied().unwrap_or(0) as u32];
            out.extend(record(&c)[..7].iter());
            out.extend(result.map_or([333, 444], |r| {
                [u32::from(r.driving), u32::from(r.auxiliary_down)]
            }));
            out.push(c.trace.len() as u32);
            out.extend(&c.trace);
            expected.push((case, step, out));
            if result.is_err() {
                break;
            }
            // Short held/released sequences retain the preceding input state.
            if step == 2 {
                c.keys = 0;
                c.axes = [0; 2];
            }
        }
    }
    let mut child = Command::new(
        std::env::var_os("TANKRACER_PLAYER_INPUT_REFERENCE").expect("set input oracle"),
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
    assert_eq!(&output.stdout[..4], b"PI01");
    let words: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    let mut at = 0;
    for (case, step, out) in &expected {
        for (field, value) in out.iter().enumerate() {
            assert_eq!(*value, words[at], "case={case} step={step} field={field}");
            at += 1;
        }
    }
    assert_eq!(at, words.len());
    println!(
        "CUP input: {} cases / {} operations / {at} words",
        cases.len(),
        expected.len()
    );
}
