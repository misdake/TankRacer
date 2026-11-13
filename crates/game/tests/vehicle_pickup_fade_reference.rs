//! Shared pickup fading/retirement against unchanged production C.
use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    effect_pool::{Pool, Quad, QuadHandle},
    object_query::EntityId,
    vehicle_scene::pickup,
};
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn quad(out: &mut Vec<u32>, q: Quad) {
    append(out, q.points.into_iter().flatten());
    out.push(q.material);
    out.extend(
        q.style
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap())),
    );
}
fn words(s: &pickup::State, pool: &Pool) -> Vec<u32> {
    let mut out = Vec::new();
    for b in &s.banks {
        out.extend([
            u32::from(b.active),
            u32::from(b.projected),
            u32::from(b.follow[0]),
            u32::from(b.follow[1]),
            b.target.map_or(0, |e| e.0),
            b.life as u32,
            b.frame,
        ]);
        append(&mut out, b.origin);
        append(&mut out, b.position);
        append(
            &mut out,
            [b.speed, b.inner_radius, b.outer_radius, b.flare_radius],
        );
        for r in b.rays {
            out.extend(r.angles.map(u32::from));
            append(&mut out, r.angular_velocity);
            out.extend([r.palette_index, r.handle.0]);
            quad(&mut out, r.quad);
        }
        for r in b.rings {
            quad(&mut out, r.quad);
            out.push(r.handle.0);
        }
    }
    out.extend([pool.quad_count, pool.line_count]);
    for q in pool.quads {
        quad(&mut out, q);
    }
    quad(&mut out, pool.exhausted_quad);
    out.extend(pool.exhausted_colors.map(u32::from_le_bytes));
    for l in &pool.lines[..2] {
        append(&mut out, l.points.into_iter().flatten());
        out.extend(l.colors.map(u32::from_le_bytes));
    }
    out
}
fn handle(case: usize, i: usize) -> QuadHandle {
    if case.is_multiple_of(13) && i == 3 {
        return QuadHandle(0x12345678);
    }
    [
        QuadHandle::RELEASED,
        QuadHandle::EXHAUSTED,
        QuadHandle::OVERFLOW,
        QuadHandle(0),
        QuadHandle(159),
        QuadHandle(i as u32),
    ][(case + i) % 6]
}
fn fixture(case: usize) -> (pickup::State, Pool) {
    let s = pickup::State {
        banks: std::array::from_fn(|b| pickup::Bank {
            active: if (case + b).is_multiple_of(7) { 0 } else { 1 },
            projected: 1,
            follow: [(case % 2) as u8, (case / 2 % 2) as u8],
            target: if (case + b).is_multiple_of(11) {
                None
            } else {
                Some(EntityId((case % 2 + 1) as u32))
            },
            life: [0, 1, 192, 65536, i32::MIN, i32::MAX][(case + b) % 6],
            frame: [0, 1, u32::MAX][(case + b) % 3],
            origin: [1, 2, 3],
            position: [12345, 67890, -123456],
            speed: [0, 1, 100000, i32::MIN][case % 4],
            inner_radius: [25599, 25600, 25601, i32::MAX][(case + b) % 4],
            outer_radius: [25599, 25600, 25601, i32::MAX][(case + 2 * b) % 4],
            flare_radius: 123,
            rays: std::array::from_fn(|i| pickup::Ray {
                angles: [i as u16, 65535 - i as u16],
                angular_velocity: [-19999 + i as i32, 98765],
                palette_index: ((case + i) % 10) as u32,
                handle: handle(case, i),
                quad: Quad {
                    points: [[i as i32, 12345, -98765]; 4],
                    material: 77,
                    style: [i as u8; 12],
                },
            }),
            rings: std::array::from_fn(|i| pickup::Ring {
                quad: Quad {
                    points: [[i as i32 + 777, 12345, -98765]; 4],
                    material: 88,
                    style: [99; 12],
                },
                handle: handle(case + 1, i),
            }),
        }),
    };
    let mut pool = Pool {
        quad_count: [0, 160, 159, u32::MAX][case % 4],
        ..Pool::default()
    };
    for (i, q) in pool.quads.iter_mut().enumerate() {
        *q = Quad {
            points: [[
                i as i32,
                if (case + i).is_multiple_of(2) {
                    tankracer_game::effect_pool::FREE
                } else {
                    123
                },
                456,
            ]; 4],
            material: i as u32,
            style: [i as u8; 12],
        };
    }
    pool.exhausted_quad = Quad {
        points: [[5, 6, 7]; 4],
        material: 55,
        style: [66; 12],
    };
    pool.exhausted_colors = [[7; 4], [8; 4]];
    (s, pool)
}
#[test]
#[ignore = "requires native fade oracle; set TANKRACER_VEHICLE_PICKUP_FADE_REFERENCE"]
fn shared_pickup_fade_follow_gate_and_retirement_match_c() {
    const CASES: usize = 96;
    let mut wire = vec![CASES as u32];
    let mut expected = Vec::new();
    let mut bounds = Vec::new();
    let mut errors = 0;
    let mut changed = 0;
    let mut expired = 0;
    let mut retired = 0;
    for case in 0..CASES {
        let tuning = pickup::Tuning::new([0, 5000, -7000, i32::MAX][case % 4]);
        wire.push(tuning.flare_growth as u32);
        let start = expected.len();
        expected.extend([tuning.outer_growth as u32, tuning.inner_growth as u32]);
        expected.extend(tuning.palette.into_iter().flatten().map(u32::from));
        bounds.push((case, usize::MAX, start, expected.len()));
        let (mut s, mut pool) = fixture(case);
        wire.extend(words(&s, &pool));
        wire.push(8);
        expected.push(8);
        for step in 0..8 {
            let entity = EntityId(((case + step / 3) % 2 + 1) as u32);
            let position = if step % 2 == 0 {
                [12345, 67890, -123456]
            } else {
                [i32::MIN, 123456, 98765]
            };
            let frame = [0, 1, u32::MAX, 2][step % 4];
            let dt = [0, 1, 16, 33, 65535][(case + step) % 5];
            wire.push(entity.0);
            append(&mut wire, position);
            wire.extend([frame, dt as u32]);
            let before = s.clone();
            let count = pool.quad_count;
            let result = s.fade(entity, position, frame, dt, &tuning, &mut pool);
            errors += usize::from(result.is_err());
            changed += usize::from(s != before);
            expired += before
                .banks
                .iter()
                .zip(&s.banks)
                .filter(|(a, b)| a.active != 0 && b.active == 0)
                .count();
            retired += usize::from(count != pool.quad_count);
            let start = expected.len();
            expected.push(result.err().unwrap_or(0) as u32);
            expected.extend(words(&s, &pool));
            bounds.push((case, step, start, expected.len()));
        }
    }
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests");
    fs::create_dir_all(&root).unwrap();
    let path = root.join("vehicle-pickup-fade-input.bin");
    fs::write(
        &path,
        wire.iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_VEHICLE_PICKUP_FADE_REFERENCE").expect("set oracle"),
    )
    .stdin(Stdio::from(fs::File::open(path).unwrap()))
    .output()
    .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"VF01");
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
    assert!(errors > 0 && changed > 0 && expired > 0 && retired > 0);
    println!(
        "{CASES} scenarios / {} operations / {} words; errors={errors}, changed={changed}, expired={expired}, retirement calls={retired}",
        CASES * 8,
        expected.len()
    );
}
