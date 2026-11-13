//! Native spray/smoke, immutable bindings and real impact emission parity.
use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    effect_pool::{self, LineHandle, Pool, Quad},
    impact_particles::{self, State as Impacts},
    math::Matrix,
    object_query::EntityId,
    pose::Pose,
    random,
    vehicle_scene::{smoke, spray},
};
struct Fixture {
    spray: spray::State,
    smoke: smoke::State,
    impacts: Impacts,
    pool: Pool,
    cooldown: u32,
    random: u32,
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn metadata(spray: &spray::Tuning, smoke: &smoke::Tuning) -> Vec<u32> {
    let mut out: Vec<_> = spray.kinds.map(|v| u32::from(v as u8)).into();
    for s in spray.styles {
        out.extend(s.color.map(u32::from));
        out.extend(s.source_flags.map(u32::from));
        out.extend([
            u32::from(s.blend),
            u32::from(s.radius as u16),
            u32::from(s.initial_size),
            u32::from(s.gain.multiplier),
            u32::from(s.gain.shift),
            s.spin,
            s.material,
        ]);
    }
    append(&mut out, smoke.sockets.into_iter().flatten().flatten());
    out
}
impl Fixture {
    fn words(&self) -> Vec<u32> {
        let mut out = Vec::new();
        for p in self.spray.particles {
            append(&mut out, p.position);
            append(&mut out, p.velocity);
            out.extend([
                u32::from(p.life as u16),
                u32::from(p.size),
                u32::from(p.floor as u16),
                u32::from(p.rotation),
                u32::from(p.kind as u8),
                u32::from(p.side),
            ]);
        }
        out.extend([self.spray.timer as u32, self.spray.side, self.spray.count]);
        for p in self.smoke.particles {
            append(&mut out, p.position);
            append(&mut out, p.velocity);
            out.extend([
                u32::from(p.life as u16),
                u32::from(p.size),
                u32::from(p.rotation),
                u32::from(p.direction),
                u32::from(p.expanding),
            ]);
        }
        out.extend([
            self.smoke.timer as u32,
            self.smoke.side,
            self.smoke.force as u32,
            self.smoke.count,
        ]);
        out.extend(self.smoke.damage.map(u32::from));
        out.extend(self.smoke.sockets.map(u32::from));
        append(&mut out, self.smoke.damage_timers);
        out.extend([self.random, self.cooldown]);
        for b in self.impacts.batches {
            for p in b.particles {
                out.extend(p.distance.map(|v| u32::from(v as u16)));
                append(&mut out, p.speed);
                append(&mut out, p.pitch);
                append(&mut out, p.yaw);
                out.push(p.line.0);
                out.extend(p.colors.into_iter().flatten().map(u32::from));
            }
            out.extend([
                b.kind as u32,
                b.corner,
                u32::from(b.owner.is_some()),
                u32::from(b.active),
                b.age as u32,
                b.frame,
            ]);
        }
        out.extend([
            self.impacts.next,
            self.pool.line_count,
            self.pool.quad_count,
        ]);
        for l in self.pool.lines.iter().chain([&self.pool.overflow]) {
            append(&mut out, l.points.into_iter().flatten());
            out.extend(l.colors.map(u32::from_le_bytes));
        }
        let q = self.pool.quads[159];
        append(&mut out, q.points.into_iter().flatten());
        out.push(q.material);
        out.extend(
            q.style
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap())),
        );
        out.extend(self.pool.exhausted_colors.map(u32::from_le_bytes));
        out
    }
    fn run(&mut self, kind: u32, input: &Input, styles: &spray::Tuning) -> Result<(), i32> {
        if kind == 0 {
            self.spray.step(
                &spray::Input {
                    dt: input.dt,
                    track_speed: input.track_speed,
                    velocity: input.velocity,
                    basis: input.reference,
                    materials: input.materials.map(|m| m as usize),
                    kinds: input.kinds,
                    contacts: input.contacts,
                    visible: input.visible,
                },
                styles,
            )
        } else {
            let mut owner = impact_particles::Vehicle {
                entity: EntityId(1),
                contacts: input.contacts,
                forward_velocity: input.forward,
                external_basis: input.external,
                cooldown: self.cooldown,
            };
            let result = self.smoke.step(
                &smoke::Input {
                    dt: input.dt,
                    player: input.player,
                    special: input.special,
                    velocity: input.velocity,
                    lateral_velocity: input.lateral,
                    track_slip: input.slip,
                    penetrating_force: input.force,
                    materials: input.materials,
                    contacts: input.contacts,
                    corners: input.corners,
                    poses: input.poses,
                },
                &smoke::Tuning::default(),
                &mut self.impacts,
                &mut owner,
                &mut impact_particles::Context {
                    frame: input.frame,
                    random: &mut self.random,
                    pool: &mut self.pool,
                    tuning: impact_particles::Tuning::default(),
                },
            );
            self.cooldown = owner.cooldown;
            result
        }
    }
}
struct Input {
    dt: i32,
    frame: u32,
    player: bool,
    special: bool,
    velocity: [i32; 3],
    lateral: i32,
    slip: i32,
    force: i32,
    track_speed: i32,
    forward: i32,
    materials: [i32; 2],
    kinds: [i32; 4],
    contacts: [[i32; 3]; 4],
    corners: [u32; 4],
    reference: Matrix,
    poses: [Pose; 2],
    external: Option<Matrix>,
    visible: i32,
}
impl Input {
    fn words(&self) -> Vec<u32> {
        let mut out = vec![
            self.dt as u32,
            self.frame,
            u32::from(self.player),
            u32::from(self.special),
        ];
        append(&mut out, self.velocity);
        append(
            &mut out,
            [
                self.lateral,
                self.slip,
                self.force,
                self.track_speed,
                self.forward,
            ],
        );
        append(&mut out, self.materials);
        append(&mut out, self.kinds);
        append(&mut out, self.contacts.into_iter().flatten());
        out.extend(self.corners);
        append(&mut out, self.reference.0);
        for p in self.poses {
            append(&mut out, p.position);
            append(&mut out, p.matrix.0);
        }
        out.push(u32::from(self.external.is_some()));
        append(&mut out, self.external.unwrap_or_default().0);
        out.push(self.visible as u32);
        out
    }
}
fn fixture(case: usize) -> Fixture {
    let spray = spray::State {
        particles: std::array::from_fn(|i| spray::Particle {
            position: [i32::MAX - i as i32, -10000 + i as i32, i32::MIN + i as i32],
            velocity: [-12345, 6789, 98765],
            life: if case.is_multiple_of(8) {
                1
            } else {
                [0, 1, 96, -1, 32767, i16::MIN][(case + i) % 6]
            },
            size: 65535 - i as u16,
            floor: -9000,
            rotation: 65535 - i as u16,
            kind: ((case + i) % 6) as i8,
            side: (i % 2) as u8,
        }),
        timer: [0, 39, 40, i32::MAX][case % 4],
        side: (case % 2) as u32,
        count: 987,
    };
    let smoke = smoke::State {
        particles: std::array::from_fn(|i| smoke::Particle {
            position: [i as i32 * 1024, -99999, i32::MAX],
            velocity: [12345, -6789, -98765],
            life: if case.is_multiple_of(11) {
                1
            } else {
                [0, 1, 2048, -1][(case + i) % 4]
            },
            size: 65535 - i as u16,
            rotation: 65535 - i as u16,
            direction: ((case + i) % 2) as u8,
            expanding: (i % 2) as u8,
        }),
        timer: [0, 69, 70, 1000][case % 4],
        side: (case / 2 % 2) as u32,
        force: [0, 50000000, i32::MIN][case % 3],
        count: 789,
        damage: [[0, 7, 9][case % 3], [0, 7, 9][case / 3 % 3]],
        sockets: [(case % 6) as u8, (case / 6 % 6) as u8],
        damage_timers: [[0, 149], [150, 299], [300, 301]][case / 5 % 3],
    };
    let mut pool = Pool {
        line_count: [0, 90, 100, 99][case % 4],
        ..Pool::default()
    };
    for (i, l) in pool.lines.iter_mut().enumerate() {
        l.points = [[i as i32 * 100, i as i32 + 77, -123456]; 2];
        l.colors = [[i as u8; 4], [255 - i as u8; 4]];
        if i >= [0, 90, 100, 100][case % 4] {
            l.points[0][1] = effect_pool::FREE;
        }
    }
    pool.overflow.points = [[1, 2, 3]; 2];
    pool.quads[159] = Quad {
        points: [[11, 22, 33]; 4],
        material: 123,
        style: [45; 12],
    };
    pool.exhausted_colors = [[7; 4], [9; 4]];
    let mut impacts = Impacts {
        next: if case.is_multiple_of(17) {
            4
        } else {
            (case % 4) as u32
        },
        ..Impacts::default()
    };
    for (i, b) in impacts.batches.iter_mut().enumerate() {
        b.owner = Some(EntityId(1));
        b.active = (case + i).is_multiple_of(3);
        b.kind = (i % 7) as i32;
        b.corner = (i % 4) as u32;
        b.age = 29;
        b.frame = 123;
        for (j, p) in b.particles.iter_mut().enumerate() {
            p.distance = [-1, 30000];
            p.speed = [123456, -654321];
            p.pitch = [1111, 2222];
            p.yaw = [3333, 4444];
            p.line = if case.is_multiple_of(31) && j == 0 {
                LineHandle(0x12345678)
            } else if j % 3 == 0 {
                LineHandle::RELEASED
            } else {
                LineHandle(j as u32)
            };
            p.colors = [[1, 2, 3], [4, 5, 6]];
        }
    }
    Fixture {
        spray,
        smoke,
        impacts,
        pool,
        cooldown: [499, 500, 501, u32::MAX][case / 3 % 4],
        random: (case as u32).wrapping_mul(982451653),
    }
}
fn input(case: usize, step: usize) -> Input {
    let reference = Matrix::from_angles([1234, 5678, 9012]);
    let mut contacts = [
        [1000, -2000, 3000],
        [2000, -2100, 7000],
        [-1000, -1900, 3000],
        [-2000, -2200, 7000],
    ];
    if case.is_multiple_of(29) {
        contacts[0][0] = 0;
        contacts[1][0] = i32::MIN;
    }
    Input {
        dt: [1, 16, 33, 40, 65535, 0][(case + step) % 6],
        frame: step as u32,
        player: !case.is_multiple_of(5),
        special: case.is_multiple_of(13),
        velocity: [
            [250000, 0, 0],
            [250001, 0, 0],
            [-300000, 12345, -400000],
            [i32::MIN, 0, 0],
            [900000, 0, 0],
        ][case / 2 % 5],
        lateral: [
            35000,
            35001,
            -35001,
            130000,
            130001,
            -130000,
            -130001,
            0,
            250000,
            i32::MIN,
        ][(case + step) % 10],
        slip: [0, 1, 24999, 25000, i32::MIN][case / 3 % 5],
        force: [0, 4000000, 4000001, 50000000, i32::MAX][case / 5 % 5],
        track_speed: [50000, 50001, -50000, -50001, i32::MIN, 600000][(case / 4 + step) % 6],
        forward: if case.is_multiple_of(2) { -1 } else { 0 },
        materials: [
            [1, 1],
            [1, 0],
            [0, 1],
            [2, 2],
            [4, 7],
            [6, 6],
            [3, 3],
            [9, 9],
        ][(case / 5 + step) % 8],
        kinds: [[0, 1, 2, 3], [2, 3, 0, 1], [0; 4]][case / 7 % 3],
        contacts,
        corners: std::array::from_fn(|i| ((i + case) % 4) as u32),
        reference,
        poses: [
            Pose {
                position: [12345, 67890, -123456],
                matrix: Matrix::from_angles([1000, 2000, 3000]),
            },
            Pose {
                position: [-99999, 88888, 77777],
                matrix: Matrix::from_angles([4000, 5000, 6000]),
            },
        ],
        external: if case.is_multiple_of(7) {
            None
        } else {
            Some(reference)
        },
        visible: if case.is_multiple_of(11) { 0 } else { 1 },
    }
}
fn advances(mut seed: u32, after: u32) -> u32 {
    for count in 0..=64 {
        if seed == after {
            return count;
        }
        random::next(&mut seed);
    }
    panic!("unexpected RNG count");
}
#[test]
#[ignore = "requires native particle oracle; set TANKRACER_VEHICLE_PARTICLES_REFERENCE"]
fn spray_smoke_styles_and_real_impact_generation_match_c() {
    const CASES: usize = 240;
    let mut wire = vec![CASES as u32];
    let mut expected = Vec::new();
    let mut bounds = Vec::new();
    let mut errors = 0;
    let mut emissions = 0;
    let mut spray_emissions = 0;
    let mut emitted_styles = 0_u8;
    let mut smoke_changes = 0;
    let mut draws = 0;
    for case in 0..CASES {
        let swamp = case.is_multiple_of(2);
        let hardware = (case / 2).is_multiple_of(2);
        let styles = spray::Tuning::environment(
            spray::Materials {
                filth: 101,
                crap: 102,
                splash: if hardware { 103 } else { 104 },
            },
            swamp,
            hardware,
        );
        wire.extend([u32::from(swamp), u32::from(hardware)]);
        let start = expected.len();
        expected.extend(metadata(&styles, &smoke::Tuning::default()));
        bounds.push((case, usize::MAX, start, expected.len()));
        let mut f = fixture(case);
        wire.extend(f.words());
        wire.push(8);
        expected.push(8);
        for step in 0..8 {
            let kind = (step % 2) as u32;
            let input = input(case, step);
            wire.push(kind);
            wire.extend(input.words());
            let seed = f.random;
            let next = f.impacts.next;
            let before = f.smoke.clone();
            let spray = f.spray.clone();
            let status = f.run(kind, &input, &styles);
            let count = advances(seed, f.random);
            draws += count;
            errors += usize::from(status.is_err());
            emissions += usize::from(next != f.impacts.next);
            smoke_changes += usize::from(before != f.smoke);
            if kind == 0 {
                for (old, new) in spray.particles.iter().zip(&f.spray.particles) {
                    if old.life < 1 && new.life == 2048 {
                        spray_emissions += 1;
                        emitted_styles |= 1 << new.kind;
                    }
                }
            }
            if kind == 0 {
                assert_eq!(count, 0);
            }
            let start = expected.len();
            expected.extend([status.err().unwrap_or(0) as u32, count]);
            expected.extend(f.words());
            bounds.push((case, step, start, expected.len()));
        }
    }
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests");
    fs::create_dir_all(&root).unwrap();
    let path = root.join("vehicle-particles-input.bin");
    fs::write(
        &path,
        wire.iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_VEHICLE_PARTICLES_REFERENCE").expect("set oracle"),
    )
    .stdin(Stdio::from(fs::File::open(path).unwrap()))
    .output()
    .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"VP03");
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
    assert!(errors > 0 && emissions > 0 && spray_emissions > 0 && smoke_changes > 0 && draws > 0);
    assert_eq!(emitted_styles, 0b11_1111, "emit every surface style");
    println!(
        "{CASES} scenarios / {} operations / {} words / {errors} error prefixes; impacts={emissions}, spray emissions={spray_emissions}, smoke changes={smoke_changes}, draws={draws}",
        CASES * 8,
        expected.len()
    );
}
