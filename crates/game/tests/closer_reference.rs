//! Inverse basis lookup, phase polling and closing branches against unchanged C.
use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_game::{
    closer::{self, Phase, State},
    math::Matrix,
    object_query::EntityId,
};
#[derive(Clone)]
struct Case {
    op: u32,
    id: u32,
    frame: u32,
    dt: i32,
    limit: u32,
    player: Option<[i32; 3]>,
    position: [i32; 3],
    state: State,
    matrix: Matrix,
    external: Option<Matrix>,
    alias: bool,
    failure: i32,
}
impl Default for Case {
    fn default() -> Self {
        Self {
            op: 1,
            id: 1,
            frame: 1,
            dt: 16,
            limit: 1000,
            player: Some([0; 3]),
            position: [0; 3],
            state: State {
                speed: 1820,
                flags: 4,
                phase: Phase::Closing,
            },
            matrix: Matrix::IDENTITY,
            external: Some(Matrix::IDENTITY),
            alias: true,
            failure: 0,
        }
    }
}
fn phase(p: Phase) -> u32 {
    match p {
        Phase::Uninitialized => 0,
        Phase::Waiting => 1,
        Phase::Closing => 2,
        Phase::Fragments => 3,
    }
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn encode(c: &Case, out: &mut Vec<u32>) {
    out.extend([
        c.op,
        c.id,
        c.frame,
        c.dt as u32,
        c.limit,
        u32::from(c.player.is_some()),
    ]);
    append(out, c.player.unwrap_or([0; 3]));
    append(out, c.position);
    out.extend([
        c.state.speed as u32,
        c.state.flags,
        phase(c.state.phase),
        if c.external.is_none() {
            0
        } else if c.alias {
            1
        } else {
            2
        },
    ]);
    append(out, c.matrix.0);
    append(out, c.external.map_or([0; 9], |m| m.0));
    out.push(c.failure as u32);
}
#[test]
#[ignore = "requires C Closer/matrix-angle oracle; set TANKRACER_CLOSER_REFERENCE"]
fn lookup_phases_closing_thresholds_and_error_prefixes_match_c() {
    let mut cases = vec![];
    for pitch in [0, 8192, 16384, 49152, 65532] {
        for yaw in [0, 7000, 32768] {
            for roll in [0, 4, 8192, 32768, 65532] {
                let m = Matrix::from_angles([pitch, yaw, roll]);
                cases.push(Case {
                    op: 0,
                    matrix: m,
                    external: Some(m),
                    ..Default::default()
                });
            }
        }
    }
    for (frame, player) in [
        (0, Some([0; 3])),
        (1, Some([40959, 0, 40959])),
        (1, Some([40960, 0, 0])),
        (1, Some([0, 0, -40960])),
        (1, None),
    ] {
        cases.push(Case {
            frame,
            player,
            state: State {
                phase: Phase::Waiting,
                ..Default::default()
            },
            ..Default::default()
        });
    }
    for variant in 0..17 {
        let mut c = Case::default();
        match variant {
            0 => c.state.phase = Phase::Uninitialized,
            1 => {
                c.state.phase = Phase::Fragments;
            }
            2 => {
                c.state.phase = Phase::Fragments;
                c.failure = -13;
            }
            3 => {
                c.external = None;
            }
            4 => c.limit = 0,
            5 => {
                c.dt = 256;
                c.state.speed = -1819;
            }
            6 => {
                c.dt = 256;
                c.state.speed = -1819;
                c.failure = -13;
            }
            7 => {
                c.dt = 0;
                c.state.flags = 0;
                c.state.speed = 0;
                c.matrix = Matrix([4000, 1, 0, 65407, 339, 0, 0, 0, 4096]);
                c.external = Some(c.matrix);
            }
            8..=10 => {
                c.dt = 256;
                c.state.speed = if variant == 8 {
                    5460
                } else if variant == 9 {
                    5461
                } else {
                    5464
                };
                c.matrix = Matrix::from_angles([0, 0, 4]);
                c.external = Some(c.matrix);
            }
            11 => {
                c.state.flags = 0;
                c.state.speed = -1;
            }
            12 => {
                c.state.flags = 2;
                c.state.speed = 9000;
            }
            13 => {
                c.state.flags = 4;
                c.state.speed = i32::MAX;
                c.dt = 65536;
            }
            14 => {
                c.alias = false;
                c.external = Some(c.matrix);
            }
            15 => {
                c.matrix = Matrix::from_angles([0, 0, 65532]);
                c.external = Some(c.matrix);
                c.dt = 1 << 20;
                c.state.speed = 3 - (c.dt.wrapping_mul(1820) >> 8);
            }
            16 => {
                c.state.speed = -4000;
                c.dt = -16;
            }
            _ => unreachable!(),
        }
        cases.push(c);
    }
    let mut words = vec![];
    let mut expected = vec![];
    let mut events = vec![];
    for (case, mut c) in cases.iter().cloned().enumerate() {
        for step in 0..if c.op == 0 { 1 } else { 4 } {
            encode(&c, &mut words);
            events.clear();
            let mut angles = [11, 22, 33];
            let mut nonsingular = 77;
            let result = if c.op == 0 {
                match c.external {
                    None => {
                        nonsingular = 0;
                        Ok(())
                    }
                    Some(m) => m
                        .angles()
                        .map(|(a, n)| {
                            angles = a;
                            nonsingular = u32::from(n)
                        })
                        .map_err(|_| -2),
                }
            } else if c.limit == 0 {
                Err(-1)
            } else {
                match c.state.phase {
                    Phase::Uninitialized => Err(-5),
                    Phase::Waiting => {
                        closer::poll(&mut c.state, EntityId(c.id), c.frame, c.position, c.player);
                        Ok(())
                    }
                    Phase::Fragments => {
                        events.push(2);
                        if c.failure != 0 {
                            Err(c.failure)
                        } else {
                            Ok(())
                        }
                    }
                    Phase::Closing => match c.external.as_mut() {
                        None => Err(-4),
                        Some(m) => {
                            let result = closer::close(&mut c.state, m, c.dt, c.limit, || {
                                events.push(1);
                                if c.failure != 0 {
                                    Err(c.failure)
                                } else {
                                    Ok(())
                                }
                            });
                            if c.alias {
                                c.matrix = *m;
                            }
                            result
                        }
                    },
                }
            };
            let mut out = vec![
                result.err().unwrap_or(0) as u32,
                c.state.speed as u32,
                c.state.flags,
                phase(c.state.phase),
            ];
            append(&mut out, c.matrix.0);
            out.push(u32::from(c.external.is_some()));
            append(&mut out, c.external.map_or([0; 9], |m| m.0));
            out.extend(angles);
            out.extend([nonsingular, events.len() as u32]);
            out.extend(&events);
            expected.push((case, step, out));
            c.frame += 1;
        }
    }
    let bytes: Vec<_> = words.into_iter().flat_map(u32::to_le_bytes).collect();
    let mut child =
        Command::new(std::env::var_os("TANKRACER_CLOSER_REFERENCE").expect("set Closer oracle"))
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
    assert_eq!(&output.stdout[..4], b"CL01");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    let mut at = 0;
    for (case, step, values) in &expected {
        for (word, &value) in values.iter().enumerate() {
            assert_eq!(actual[at], value, "case={case} step={step} word={word}");
            at += 1;
        }
    }
    assert_eq!(at, actual.len());
    println!(
        "Closer: {} scenarios / {} operations / {at} words",
        cases.len(),
        expected.len()
    );
}
