//! Real light/track-mark kernels and interleaved quad/line pool aliases.
use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    effect_pool::{self, LineHandle, Pool, Quad, QuadHandle},
    math::Matrix,
    vehicle_scene::{lights, marks},
};
struct Fixture {
    lights: lights::State,
    marks: marks::State,
    pool: Pool,
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn quad(out: &mut Vec<u32>, q: &Quad) {
    append(out, q.points.into_iter().flatten());
    out.push(q.material);
    out.extend(
        q.style
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap())),
    );
}
impl Fixture {
    fn words(&self) -> Vec<u32> {
        let mut out = Vec::new();
        out.push(u32::from(self.lights.brake_mode));
        out.extend(self.lights.active.map(u32::from));
        out.extend(self.lights.handles.map(|h| h.0));
        out.extend(self.marks.active.map(u32::from));
        append(
            &mut out,
            self.marks.endpoints.into_iter().flatten().flatten(),
        );
        append(&mut out, self.marks.movement);
        out.extend(self.marks.handles.map(|h| h.0));
        out.extend([
            self.marks.count,
            self.marks.head,
            self.marks.tail,
            self.pool.line_count,
            self.pool.quad_count,
        ]);
        quad(&mut out, &self.pool.exhausted_quad);
        for q in &self.pool.quads {
            quad(&mut out, q);
        }
        out.extend(self.pool.exhausted_colors.map(u32::from_le_bytes));
        for line in self.pool.lines.iter().chain([&self.pool.overflow]) {
            append(&mut out, line.points.into_iter().flatten());
            out.extend(line.colors.map(u32::from_le_bytes));
        }
        out
    }
}
fn fixture(case: usize) -> Fixture {
    let occupied = [0, 68, 159, 160][case % 4];
    let mut pool = Pool {
        quad_count: [0, 68, 160, 159][case % 4],
        line_count: 3,
        ..Pool::default()
    };
    for (i, q) in pool.quads.iter_mut().enumerate() {
        *q = Quad {
            points: std::array::from_fn(|p| {
                std::array::from_fn(|j| (i * 12 + p * 3 + j) as i32 * -9)
            }),
            material: i as u32 + 23,
            style: std::array::from_fn(|j| (i + j) as u8),
        };
        if i >= occupied {
            q.points[0][1] = effect_pool::FREE;
        }
    }
    pool.exhausted_quad = Quad {
        points: [[12345, 6789, -123]; 4],
        material: 876,
        style: [42; 12],
    };
    pool.exhausted_colors = [[12; 4], [34; 4]];
    for (i, line) in pool.lines.iter_mut().enumerate() {
        line.points = [[i as i32 + 77, -(i as i32) - 88, 99]; 2];
        line.colors = [[i as u8; 4], [i as u8 + 3; 4]];
        if i >= 3 {
            line.points[0][1] = effect_pool::FREE;
        }
    }
    pool.overflow.points = [[111, 222, 333]; 2];
    let handles = if case.is_multiple_of(17) {
        [QuadHandle::RELEASED, QuadHandle(1)]
    } else if case.is_multiple_of(13) {
        [QuadHandle::EXHAUSTED, QuadHandle::OVERFLOW]
    } else {
        [QuadHandle(0), QuadHandle(1)]
    };
    let mut marks = marks::State {
        active: [(case % 2) as u8, (case / 2 % 2) as u8],
        endpoints: std::array::from_fn(|s| {
            std::array::from_fn(|p| std::array::from_fn(|i| (s * 100 + p * 30 + i) as i32 * 1024))
        }),
        movement: [67, 89, -10],
        ..marks::State::default()
    };
    if case.is_multiple_of(3) {
        marks.count = 68;
        marks.head = 67;
        marks.tail = 67;
    }
    marks.handles = std::array::from_fn(|i| QuadHandle(i as u32));
    Fixture {
        lights: lights::State {
            brake_mode: (case % 2) as u8,
            active: [(case / 2 % 2) as u8, (case / 3 % 2) as u8],
            handles,
        },
        marks,
        pool,
    }
}
fn light_input(case: usize, step: usize) -> lights::Input {
    lights::Input {
        player: !case.is_multiple_of(5),
        disabled: case.is_multiple_of(7),
        settings: lights::Settings {
            active: !case.is_multiple_of(11),
            visible: if case.is_multiple_of(19) { 0 } else { 1 },
            queued: case.is_multiple_of(23),
            reverse_braking: case.is_multiple_of(3),
            material: 456,
        },
        throttle: [-1, -40000, 0, 1000][(case + step) % 4],
        brake: [0, 1, 0, 40000][(case / 4 + step) % 4],
        forward_velocity: [-1001, -1000, 1000, 1001, 327680][case % 5],
        position: [10000, [2999, 3000, -3000, 50000][case / 5 % 4], -123456],
        basis: if case.is_multiple_of(3) {
            None
        } else {
            Some(Matrix::from_angles([1234, 4567, 8901]))
        },
        materials: [[1, 1], [1, 2], [2, 1], [0, 0]][case / 6 % 4],
        raw_heights: [0, 10, -10, 0],
        offset: [1100, 200, -3000],
    }
}
fn light_words(out: &mut Vec<u32>, i: lights::Input) {
    out.extend([
        u32::from(i.player),
        u32::from(i.settings.active),
        i.settings.visible as u32,
        u32::from(i.disabled),
        u32::from(i.settings.queued),
        u32::from(i.settings.reverse_braking),
    ]);
    append(out, [i.throttle, i.brake, i.forward_velocity]);
    append(out, i.position);
    out.push(u32::from(i.basis.is_some()));
    append(out, i.basis.unwrap_or_default().0);
    append(out, i.materials);
    append(out, i.raw_heights);
    append(out, i.offset);
    out.push(i.settings.material);
}
fn mark_input(case: usize, step: usize) -> marks::Input {
    marks::Input {
        player: !case.is_multiple_of(5),
        deformable: case.is_multiple_of(17),
        mode: [0, 1, 2, 3, 4, 5][case / 3 % 6],
        dt: [16, 40, 33, 65535][case % 4],
        lateral_velocity: [79999, 80000, -80000, i32::MIN][(case + step) % 4],
        track_slip: [24999, 25000, -25000, 0][case / 4 % 4],
        velocity: [123456, -654321, if step == 0 { i32::MAX } else { 987654 }],
        materials: [[1, 1], [1, 0], [0, 1], [0, 0]][(case / 7 + step) % 4],
        travel: if step == 3 {
            [40000; 4]
        } else {
            [40000, 40001, 50000, 60000]
        },
        kinds: [[0, 1, 2, 3], [2, 3, 0, 1], [0, 0, 0, 0]][case / 5 % 3],
        contacts: [
            [100000, -1000, -200000],
            [110000, -900, -210000],
            [-100000, -2000, 200000],
            [-110000, -1900, 210000],
        ],
        tracks: [
            Matrix::from_angles([1000, 2000, 3000]),
            Matrix::from_angles([4000, 5000, 6000]),
        ],
    }
}
fn mark_words(out: &mut Vec<u32>, i: marks::Input) {
    out.extend([u32::from(i.player), u32::from(i.deformable)]);
    append(out, [i.mode, i.dt, i.lateral_velocity, i.track_slip]);
    append(out, i.velocity);
    append(out, i.materials);
    append(out, i.travel);
    append(out, i.kinds);
    append(out, i.contacts.into_iter().flatten());
    append(out, i.tracks.into_iter().flat_map(|m| m.0));
}
fn payload(case: usize, step: usize) -> Quad {
    Quad {
        points: std::array::from_fn(|p| {
            std::array::from_fn(|i| {
                [
                    i32::MIN,
                    i32::MAX,
                    -9,
                    -8,
                    -7,
                    0,
                    7,
                    8,
                    9,
                    123456,
                    -123456,
                    654321,
                ][(p * 3 + i + step) % 12]
            })
        }),
        material: (case * 100 + step) as u32,
        style: std::array::from_fn(|i| (i * 17 + case + step) as u8),
    }
}
#[test]
#[ignore = "requires production light/mark/quad oracle; set TANKRACER_VEHICLE_QUADS_REFERENCE"]
fn vehicle_quads_and_pool_aliases_match_c_by_branch_and_short_sequence() {
    const CASES: usize = 168;
    let mut input = vec![CASES as u32];
    let mut expected = Vec::new();
    let mut errors = 0;
    let mut operations = 0;
    let mut allocated = 0;
    let mut lights_changed = 0;
    let mut marks_changed = 0;
    let mut fifo_retirements = 0;
    let mut fifo_wraps = 0;
    let mut overflow_allocations = 0;
    let mut exhausted_allocations = 0;
    let mut bounds = Vec::new();
    for case in 0..CASES {
        let mut f = fixture(case);
        input.extend(f.words());
        let kinds: &[u32] = if case < 144 {
            &[0, 1, 0, 1, 1, 0]
        } else {
            &[2, 3, 4, 5, 5, 6, 7, 9, 10, 8, 2, 1, 0]
        };
        input.push(kinds.len() as u32);
        expected.push(kinds.len() as u32);
        for (step, kind) in kinds.iter().enumerate() {
            let handle = if matches!(kind, 6 | 7) {
                [0, 99, 100, u32::MAX, LineHandle::RELEASED.0, 1][(case + step) % 6]
            } else {
                [0, 159, 160, u32::MAX, QuadHandle::RELEASED.0, 100][(case + step) % 6]
            };
            input.extend([*kind, handle]);
            let mut result = 0;
            let before_lights = f.lights.clone();
            let before_marks = f.marks.clone();
            let status = match kind {
                0 => {
                    let i = light_input(case, step);
                    light_words(&mut input, i);
                    f.lights.step(&i, &mut f.pool)
                }
                1 => {
                    let i = mark_input(case, step);
                    mark_words(&mut input, i);
                    f.marks.step(&i, &mut f.pool)
                }
                8 => {
                    f.pool.reset();
                    Ok(())
                }
                _ => {
                    let q = payload(case, step);
                    append(&mut input, q.points.into_iter().flatten());
                    input.push(q.material);
                    input.extend(q.style.map(u32::from));
                    let line_points = [q.points[0], q.points[1]];
                    let colors = [
                        q.style[..4].try_into().unwrap(),
                        q.style[4..8].try_into().unwrap(),
                    ];
                    match kind {
                        2 | 9 => {
                            result = f.pool.allocate_quad(q).0;
                            allocated += usize::from(result < 160);
                            overflow_allocations += usize::from(result == 160);
                            exhausted_allocations += usize::from(result == u32::MAX);
                            Ok(())
                        }
                        3 => f
                            .pool
                            .update_quad(QuadHandle(handle), q.points)
                            .map_err(|_| -1),
                        4 => f.pool.replace_quad(QuadHandle(handle), q).map_err(|_| -1),
                        5 => f.pool.release_quad(QuadHandle(handle)).map_err(|_| -1),
                        6 => f
                            .pool
                            .update(LineHandle(handle), line_points, colors)
                            .map_err(|_| -1),
                        7 => f.pool.release(LineHandle(handle)).map_err(|_| -1),
                        10 => {
                            result = f.pool.allocate(line_points, colors).0;
                            Ok(())
                        }
                        _ => unreachable!(),
                    }
                }
            };
            lights_changed += usize::from(before_lights != f.lights);
            marks_changed += usize::from(before_marks != f.marks);
            fifo_retirements +=
                usize::from(before_marks.count == 68 && before_marks.head != f.marks.head);
            fifo_wraps +=
                usize::from(f.marks.head < before_marks.head || f.marks.tail < before_marks.tail);
            errors += usize::from(status.is_err());
            let start = expected.len();
            expected.extend([status.err().unwrap_or(0) as u32, result]);
            expected.extend(f.words());
            bounds.push((case, step, start, expected.len()));
            operations += 1;
        }
    }
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests");
    fs::create_dir_all(&root).unwrap();
    let path = root.join("vehicle-quads-input.bin");
    fs::write(
        &path,
        input
            .iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output =
        Command::new(std::env::var_os("TANKRACER_VEHICLE_QUADS_REFERENCE").expect("set oracle"))
            .stdin(Stdio::from(fs::File::open(path).unwrap()))
            .output()
            .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"VQ01");
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
                .find(|(_, _, start, end)| word >= *start && word < *end)
                .unwrap();
            panic!(
                "case={case} step={step} word={} C={a:08x} Rust={e:08x}",
                word - start
            );
        }
    }
    assert!(errors > 0 && allocated > 0 && lights_changed > 0 && marks_changed > 0);
    assert!(
        fifo_retirements > 0
            && fifo_wraps > 0
            && overflow_allocations > 0
            && exhausted_allocations > 0
    );
    println!(
        "{CASES} scenarios / {operations} operations / {} words / {errors} error prefixes; light changes={lights_changed}, mark changes={marks_changed}, direct quad allocations={allocated}, FIFO retire/wrap={fifo_retirements}/{fifo_wraps}, overflow/exhausted allocations={overflow_allocations}/{exhausted_allocations}",
        expected.len()
    );
}
