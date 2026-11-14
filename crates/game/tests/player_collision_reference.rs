use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_game::{player_collision as collision, vehicle::CollisionState};
#[derive(Clone)]
struct Case {
    state: CollisionState,
    dt: i32,
    active: bool,
}
fn values(c: &Case) -> Vec<u32> {
    [c.state.collision_size, c.state.impact_accumulator as u32]
        .into_iter()
        .chain(c.state.ground_delta.map(|v| v as u32))
        .chain(c.state.tank_delta.map(|v| v as u32))
        .chain([c.state.obstacle_impulse as u32])
        .collect()
}
#[test]
#[ignore = "requires native collision-prefix oracle; set TANKRACER_PLAYER_COLLISION_REFERENCE"]
fn race_gate_impact_thresholds_ties_and_wrap_match_c() {
    let mut cases = vec![];
    for vector in [
        [0, 0, 0],
        [30000, 0, 0],
        [30001, 0, 0],
        [32047, 0, 0],
        [32048, 0, 0],
        [20000, 20000, 20000],
        [100000, -100000, 100000],
        [90000, 90000, 89999],
        [i32::MAX, 1, 0],
        [i32::MIN, 0, 0],
        [i32::MIN, i32::MIN, i32::MIN],
    ] {
        for shift in 0..3 {
            let mut ground = vector;
            ground.rotate_left(shift);
            let mut tank = ground.map(i32::wrapping_neg);
            tank.rotate_left(1);
            for active in [false, true] {
                cases.push(Case {
                    state: CollisionState {
                        collision_size: 300,
                        impact_accumulator: 50,
                        ground_delta: ground,
                        tank_delta: tank,
                        obstacle_impulse: vector[0],
                        ..Default::default()
                    },
                    dt: 16,
                    active,
                });
            }
        }
    }
    for marker in [0, 1, 16, 1250, 1251, -1, i32::MIN, i32::MAX] {
        for dt in [0, 1, 16, -16, i32::MIN, i32::MAX] {
            cases.push(Case {
                state: CollisionState {
                    collision_size: marker as u32,
                    impact_accumulator: marker,
                    obstacle_impulse: 30001,
                    ..Default::default()
                },
                dt,
                active: true,
            });
        }
    }
    let mut bytes = vec![];
    let mut expected = vec![];
    for (case, mut c) in cases.iter().cloned().enumerate() {
        for tick in 0..8 {
            let record = [u32::from(c.active), c.dt as u32]
                .into_iter()
                .chain(values(&c));
            bytes.extend(record.flat_map(u32::to_le_bytes));
            collision::step(&mut c.state, c.dt, c.active);
            let mut out = vec![0];
            out.extend(values(&c));
            out.extend([
                collision::change_magnitude(c.state.ground_delta) as u32,
                collision::change_magnitude(c.state.tank_delta) as u32,
            ]);
            expected.push((case, tick, out));
        }
    }
    let mut child = Command::new(
        std::env::var_os("TANKRACER_PLAYER_COLLISION_REFERENCE")
            .expect("set collision-prefix oracle"),
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
    assert_eq!(&output.stdout[..4], b"PC01");
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
        "Player collision: {} cases / {} operations / {at} words",
        cases.len(),
        expected.len()
    );
}
