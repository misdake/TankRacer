//! Immediate blast thresholds, state prefixes and exact random consumption.
use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_game::blast::{self, Input, State, Target};
#[derive(Clone)]
struct Case {
    input: Input,
    state: State,
    velocity: [i32; 3],
    angular: [i32; 3],
    seed: u32,
    marker: u32,
}
impl Default for Case {
    fn default() -> Self {
        Self {
            input: Input {
                position: [100, 200, 300],
                source: [100, 200, 300],
                mode: 4,
                damage: false,
                max_health: 40000,
                record_axes: false,
                mark: true,
            },
            state: State {
                angular_enabled: true,
                ..Default::default()
            },
            velocity: [-50, 100, 500],
            angular: [50, -20, 400],
            seed: 0x12345678,
            marker: 10,
        }
    }
}
fn append(v: &mut Vec<u32>, n: impl IntoIterator<Item = i32>) {
    v.extend(n.into_iter().map(|n| n as u32));
}
fn state(v: &mut Vec<u32>, c: &Case) {
    append(v, [c.state.health]);
    v.extend([
        u32::from(c.state.health_timer),
        u32::from(c.state.health_color),
    ]);
    append(v, [c.state.protection_timer]);
    v.extend([
        u32::from(c.state.angular_enabled),
        u32::from(c.state.hit_axes[0]),
        u32::from(c.state.hit_axes[1]),
    ]);
}
#[test]
#[ignore = "requires native blast oracle; set TANKRACER_BLAST_REFERENCE"]
fn impulse_thresholds_health_prefixes_and_discarded_rng_match_c() {
    let mut cases = vec![];
    for mode in [0, 1, 4] {
        for distance in [0, 1, 5119, 5120, 7167, 7168, 20479, 20480, 50000] {
            for direction in [-1, 1] {
                let mut c = Case::default();
                c.input.mode = mode;
                c.input.position[0] += distance * direction;
                cases.push(c);
            }
        }
    }
    for n in 0..16 {
        let mut c = Case::default();
        match n {
            0 => c.input.position = [1000, -3000, 4000],
            1 => c.input.position = [i32::MIN, 0, 0],
            2 => c.input.position = [i32::MAX, i32::MAX, i32::MAX],
            3 => {
                c.velocity = [i32::MAX; 3];
                c.angular = [i32::MIN; 3];
            }
            4 => c.state.angular_enabled = false,
            5 => c.input.mark = false,
            6 => c.input.record_axes = true,
            7 => {
                c.input.damage = true;
                c.state.health = 15000;
            }
            8 => {
                c.input.damage = true;
                c.state.health = 10;
            }
            9 => {
                c.input.damage = true;
                c.input.max_health = 0;
            }
            10 => {
                c.input.damage = true;
                c.input.max_health = -2;
                c.state.health = i32::MIN;
            }
            11 => {
                c.input.damage = true;
                c.state.health = i32::MIN + 3200;
                c.input.max_health = -2;
            }
            12 => {
                c.state.protection_timer = 10;
            }
            13 => {
                c.input.mode = 1;
                c.input.position = [20480, 20480, 20480];
            }
            14 => {
                c.input.mode = 1;
                c.input.position = [-80000, 0, 0];
            }
            _ => {
                c.input.record_axes = true;
                c.state.angular_enabled = false;
            }
        }
        cases.push(c);
    }
    let mut input = vec![];
    let mut expected = vec![];
    for (case, mut c) in cases.iter().cloned().enumerate() {
        for step in 0..4 {
            input.push(c.seed);
            append(&mut input, c.input.position);
            append(&mut input, c.input.source);
            input.extend([
                c.input.mode,
                u32::from(c.input.damage),
                c.input.max_health as u32,
                u32::from(c.input.record_axes),
                u32::from(c.input.mark),
            ]);
            append(&mut input, c.velocity);
            append(&mut input, c.angular);
            input.push(c.marker);
            state(&mut input, &c);
            let result = blast::apply(
                Target {
                    state: &mut c.state,
                    velocity: &mut c.velocity,
                    angular: &mut c.angular,
                    marker: &mut c.marker,
                },
                c.input,
                &mut c.seed,
            );
            let mut out = vec![result.err().unwrap_or(0) as u32, c.seed];
            append(&mut out, c.velocity);
            append(&mut out, c.angular);
            out.push(c.marker);
            state(&mut out, &c);
            expected.push((case, step, out));
            if result.is_err() {
                break;
            }
        }
    }
    let bytes: Vec<_> = input.into_iter().flat_map(u32::to_le_bytes).collect();
    let mut child =
        Command::new(std::env::var_os("TANKRACER_BLAST_REFERENCE").expect("set blast oracle"))
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
    assert_eq!(&output.stdout[..4], b"BL01");
    let words: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    let mut at = 0;
    for (case, step, out) in &expected {
        for (word, value) in out.iter().enumerate() {
            assert_eq!(*value, words[at], "case={case} step={step} word={word}");
            at += 1;
        }
    }
    assert_eq!(at, words.len());
    println!(
        "Blast: {} scenarios / {} operations / {} words",
        cases.len(),
        expected.len(),
        at
    );
}
