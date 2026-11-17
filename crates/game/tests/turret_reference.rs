use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_game::turret::{Keys, State};
fn encode(angle: i16, s: State) -> [u32; 8] {
    [
        angle as i32 as u32,
        s.velocity as u32,
        s.cooldown as u32,
        u32::from(s.centering),
        u32::from(s.moving),
        s.center_lock as i32 as u32,
        u32::from(s.pressed[0]),
        u32::from(s.pressed[1]),
    ]
}
#[test]
fn both_buttons_center_and_release_velocity_exactly() {
    let mut s = State::default();
    let mut angle = 2048;
    s.tick(
        &mut angle,
        Keys {
            left: true,
            right: true,
        },
        16,
    );
    assert!(s.centering);
    assert_eq!(s.cooldown, 600);
    let mut ticks = 1;
    while s.centering && ticks < 60 {
        s.tick(&mut angle, Keys::default(), 16);
        ticks += 1;
    }
    assert_eq!(angle, 0);
    assert_eq!(s.velocity, 0);
    assert!(!s.centering);
    assert!(!s.moving);
}
#[test]
#[ignore = "requires TANKRACER_TURRET_REFERENCE original input oracle"]
fn original_branches_and_short_sequences_match() {
    let mut cases = vec![];
    for angle in [-32768, -3, 0, 3, 32767] {
        for velocity in [i32::MIN, -6000, 0, 6000, i32::MAX] {
            for (cooldown, centering, lock, dt, keys) in [
                (
                    0,
                    false,
                    0,
                    16,
                    Keys {
                        left: true,
                        right: false,
                    },
                ),
                (
                    1,
                    false,
                    0,
                    1,
                    Keys {
                        left: true,
                        right: true,
                    },
                ),
                (
                    -2,
                    false,
                    0,
                    17,
                    Keys {
                        left: true,
                        right: true,
                    },
                ),
                (600, true, 0, 100, Keys::default()),
                (
                    -1,
                    false,
                    1,
                    10,
                    Keys {
                        left: true,
                        right: true,
                    },
                ),
                (
                    -1,
                    false,
                    -1,
                    1000,
                    Keys {
                        left: false,
                        right: true,
                    },
                ),
            ] {
                cases.push((
                    angle,
                    State {
                        velocity,
                        cooldown,
                        centering,
                        center_lock: lock,
                        ..Default::default()
                    },
                    dt,
                    keys,
                ));
            }
        }
    }
    let mut angle = 0;
    let mut s = State::default();
    for tick in 0..48 {
        let keys = Keys {
            left: tick < 6 || tick == 12,
            right: (10..=12).contains(&tick),
        };
        cases.push((angle, s, 16, keys));
        s.tick(&mut angle, keys, 16);
    }
    let mut input = vec![cases.len() as u32];
    let mut expected = vec![];
    for (mut angle, mut s, dt, k) in cases {
        input.extend([
            angle as i32 as u32,
            s.velocity as u32,
            s.cooldown as u32,
            u32::from(s.centering),
            s.center_lock as i32 as u32,
            dt as u32,
            u32::from(k.left),
            u32::from(k.right),
        ]);
        s.tick(&mut angle, k, dt);
        expected.extend(encode(angle, s));
    }
    let mut child =
        Command::new(std::env::var_os("TANKRACER_TURRET_REFERENCE").expect("set turret oracle"))
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
    let actual: Vec<u32> = output
        .stdout
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(actual, expected);
    eprintln!(
        "turret: 150 branch cases + 48 sequential updates / {} fields",
        actual.len()
    );
}
