//! Alternating real vehicle owners; no saved scene memory or emulated calls.
use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    object_query::EntityId,
    random,
    vehicle_scene::{debris, spray},
};

struct Fixture {
    vehicles: [debris::Vehicle; 9],
    accumulators: [i32; 9],
    shared: debris::State,
    random: u32,
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
impl Fixture {
    fn words(&self) -> Vec<u32> {
        let mut out = Vec::new();
        for (v, a) in self.vehicles.iter().zip(self.accumulators) {
            out.extend([
                v.active,
                v.timer as u32,
                u32::from(v.previous_plane),
                a as u32,
            ]);
        }
        for b in self.shared.banks {
            for p in b.particles {
                append(&mut out, p.velocity);
                append(&mut out, p.position);
                out.extend([
                    u32::from_le_bytes([p.color[0], p.color[1], p.color[2], p.active]),
                    p.size,
                ]);
            }
            out.extend([b.floor as u32, b.owner.map_or(0, |v| v.0)]);
        }
        out.push(self.random);
        out
    }
}
fn fixture(case: usize) -> Fixture {
    Fixture {
        vehicles: std::array::from_fn(|i| debris::Vehicle {
            active: u32::from((case + i).is_multiple_of(3)),
            timer: [0, 1, 15, 16, 499, 500, i32::MAX][(case + i) % 7],
            previous_plane: ((case + i) % 2) as u8,
        }),
        accumulators: std::array::from_fn(|i| i32::MAX - i as i32),
        shared: debris::State {
            banks: std::array::from_fn(|b| debris::Bank {
                particles: std::array::from_fn(|i| debris::Particle {
                    velocity: [
                        -12345,
                        [0, 100000, -100000, i32::MIN][(case + i) % 4],
                        98765,
                    ],
                    position: [i32::MAX, [-100, 0, 100, i32::MIN][(case + i) % 4], -123456],
                    color: [i as u8, b as u8, 79],
                    active: ((case + i + b) % 3) as u8,
                    size: i as u32 + 999,
                }),
                floor: [-1, 0, 100, i32::MIN][case % 4],
                owner: if case.is_multiple_of(4) {
                    None
                } else {
                    Some(EntityId((case % 9 + 1) as u32))
                },
            }),
        },
        random: (case as u32).wrapping_mul(982451653),
    }
}
fn input(case: usize, step: usize) -> debris::Input {
    debris::Input {
        dt: [0, 1, 16, 33, 500, 65535][(case + step) % 6],
        player: (case + step).is_multiple_of(3),
        position: [123456, -7890, [100, -100, i32::MIN][case % 3]],
        velocity: [
            [102400, 0, 0],
            [102401, 0, 0],
            [123456, -102400, 987654],
            [-100, 102401, 300000],
            [0, i32::MIN, 0],
            [i32::MAX; 3],
        ][(case / 2 + step) % 6],
        has_plane: u8::from(!(case + step).is_multiple_of(7)),
        plane_distance: [-1024, 0, 1024, i32::MIN][case / 3 % 4],
        materials: [[4, 2], [2, 4], [4, 4], [2, 2], [0, 15]][(case + step) % 5],
        contacts: [[1234, 5678, 9012], [-1234, -5678, -9012]],
        camera_position: [0, 0, 0],
        camera_forward: [[0, 0, 4096], [0, 0, -4096], [i32::MAX; 3]][case / 5 % 3],
    }
}
fn words(input: &debris::Input) -> Vec<u32> {
    let mut out = vec![input.dt as u32, u32::from(input.player)];
    append(&mut out, input.position);
    append(&mut out, input.velocity);
    out.extend([u32::from(input.has_plane), input.plane_distance as u32]);
    out.extend(input.materials.map(|v| v as u32));
    append(&mut out, input.contacts.into_iter().flatten());
    append(&mut out, input.camera_position);
    append(&mut out, input.camera_forward);
    out
}
#[test]
#[ignore = "requires native debris oracle; set TANKRACER_VEHICLE_DEBRIS_REFERENCE"]
fn shared_debris_stealing_hidden_emission_and_clear_match_c() {
    const CASES: usize = 144;
    let styles = spray::Tuning::environment(
        spray::Materials {
            filth: 1,
            crap: 1,
            splash: 1,
        },
        false,
        false,
    );
    let mut wire = vec![CASES as u32];
    let mut expected = Vec::new();
    let mut bounds = Vec::new();
    let mut emitted = 0;
    let mut hidden = 0;
    let mut stolen = 0;
    let mut cleared = 0;
    let mut draws = 0;
    for case in 0..CASES {
        let mut f = fixture(case);
        wire.extend(f.words());
        wire.push(12);
        expected.push(12);
        for step in 0..12 {
            let own = (case + step) % 9;
            let clear = (case + step).is_multiple_of(11);
            let input = input(case, step);
            wire.extend([own as u32, u32::from(clear)]);
            wire.extend(words(&input));
            let seed = f.random;
            let before = f.shared.clone();
            let previous = before.banks[usize::from(input.player)].owner;
            let was_active = f.vehicles[own].active;
            let mut sound = None;
            let result = if clear {
                f.shared.clear(input.player, &mut f.vehicles[own]);
                cleared += usize::from(was_active != 0);
                Ok(())
            } else {
                let mut owners: Vec<_> = f
                    .vehicles
                    .iter_mut()
                    .enumerate()
                    .map(|(i, state)| debris::Owner {
                        entity: EntityId(i as u32 + 1),
                        state,
                    })
                    .collect();
                f.shared.step(
                    &mut owners,
                    own,
                    &input,
                    &mut debris::Context {
                        kinds: &styles.kinds,
                        random: &mut f.random,
                        impact_accumulator: &mut f.accumulators[own],
                        sound_draw: &mut sound,
                    },
                )
            };
            assert!(result.is_ok());
            let mut after = seed;
            let count = (0..=145)
                .find(|_| {
                    if after == f.random {
                        true
                    } else {
                        random::next(&mut after);
                        false
                    }
                })
                .expect("bounded RNG consumption");
            // RNG writes are counted independently in C. Check the expected
            // one audio draw / 144 particle draws even on invisible emission.
            assert!(matches!(count, 0 | 1 | 145));
            assert_eq!(sound.is_some(), count != 0);
            draws += count;
            emitted += usize::from(count == 145);
            hidden += usize::from(count == 1);
            stolen += usize::from(count == 145 && previous.is_some_and(|p| p.0 != own as u32 + 1));
            let start = expected.len();
            expected.extend([0, count, sound.unwrap_or(0)]);
            expected.extend(f.words());
            bounds.push((case, step, start, expected.len()));
        }
    }
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests");
    fs::create_dir_all(&root).unwrap();
    let path = root.join("vehicle-debris-input.bin");
    fs::write(
        &path,
        wire.iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output =
        Command::new(std::env::var_os("TANKRACER_VEHICLE_DEBRIS_REFERENCE").expect("set oracle"))
            .stdin(Stdio::from(fs::File::open(path).unwrap()))
            .output()
            .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"VD01");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(actual[0], CASES as u32);
    assert_eq!(actual.len() - 1, expected.len());
    for (word, (a, e)) in actual[1..].iter().zip(&expected).enumerate() {
        if a != e {
            let &(case, step, start, _) = bounds
                .iter()
                .find(|(_, _, s, end)| word >= *s && word < *end)
                .unwrap();
            panic!(
                "case={case} step={step} word={} C={a:08x} Rust={e:08x}",
                word - start
            );
        }
    }
    assert!(emitted > 0 && hidden > 0 && stolen > 0 && cleared > 0);
    println!(
        "{CASES} scenarios / {} operations / {} words; emitted={emitted}, hidden={hidden}, stolen={stolen}, cleared={cleared}, draws={draws}",
        CASES * 12,
        expected.len()
    );
}
