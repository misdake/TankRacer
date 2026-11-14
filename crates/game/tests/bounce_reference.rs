//! Fixed-point kernel branches and short sequences. Terrain/lifecycle responses
//! are declared inputs; SR02 separately exercises actual scene/factory owners.
use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_assets::config::Gain;
use tankracer_game::{
    bounce::{self, Component, Config, Hit, Motion, Services, State},
    geometry::Bounds,
    math::Matrix,
    object_query::{Collider, EntityId},
    pose::Pose,
    terrain::MISS_HEIGHT,
};
#[derive(Clone)]
struct World {
    ground: i32,
    normal_ground: i32,
    normal: [i32; 3],
    fallback: bool,
    fallback_normal: [i32; 3],
    fail_at: usize,
    failure: i32,
    events: Vec<u32>,
    packed: u32,
}
impl World {
    fn event(&mut self, id: u32) -> Result<(), i32> {
        self.events.push(id);
        if self.events.len() == self.fail_at {
            Err(self.failure)
        } else {
            Ok(())
        }
    }
}
impl Services for World {
    fn activate(&mut self) -> Result<(), i32> {
        self.event(1)
    }
    fn deactivate(&mut self) -> Result<(), i32> {
        self.event(2)
    }
    fn settle(&mut self, _: &Collider) -> Result<(), i32> {
        self.event(6)?;
        self.event(7)?;
        self.packed = 17;
        self.event(8)
    }
    fn height(&mut self, _: i32, _: i32) -> Result<i32, i32> {
        self.event(3)?;
        Ok(self.ground)
    }
    fn normal_height(&mut self, _: i32, _: i32, n: &mut [i32; 3]) -> Result<i32, i32> {
        self.event(4)?;
        *n = self.normal;
        Ok(self.normal_ground)
    }
    fn fallback(&mut self, _: [i32; 3], n: &mut [i32; 3]) -> Result<bool, i32> {
        self.event(5)?;
        if self.fallback {
            *n = self.fallback_normal;
        }
        Ok(self.fallback)
    }
}
#[derive(Clone)]
struct Case {
    op: u32,
    dt: i32,
    active: i32,
    threshold: i32,
    seed: u32,
    collider: Collider,
    body: Component,
    external: u32,
    vehicle: Hit,
    world: World,
}
impl Default for Case {
    fn default() -> Self {
        Self {
            op: 1,
            dt: 16,
            active: 1,
            threshold: 10,
            seed: 0x12345678,
            collider: Collider {
                entity: EntityId(1),
                pose: Pose {
                    position: [0, 100, 0],
                    matrix: Matrix::IDENTITY,
                },
                external_basis: Some(Matrix::IDENTITY),
                radius: 64,
                bounds: Bounds {
                    min: [-10; 3],
                    max: [10; 3],
                },
            },
            body: Component {
                reference_alias: true,
                config: Config {
                    elasticity: 70,
                    hit_spin: Gain {
                        multiplier: 1,
                        shift: 8,
                    },
                    slow_spin: Gain {
                        multiplier: 1,
                        shift: 7,
                    },
                    deflection: 1820,
                    ..Default::default()
                },
                state: State {
                    velocity: [1024, -4096, 4096],
                    spin: -512,
                    airborne: 1,
                    ..Default::default()
                },
            },
            external: 1,
            vehicle: Hit {
                position: [-1024, 0, -2048],
                velocity: [3000, -1000, 600000],
                forward_velocity: 600000,
                reference: Matrix::from_angles([0, 7000, 0]),
                external: Some(Matrix::from_angles([0, 7000, 0])),
            },
            world: World {
                ground: 0,
                normal_ground: 0,
                normal: [0, 4096, 0],
                fallback: false,
                fallback_normal: [0, 0, -4096],
                fail_at: 0,
                failure: -13,
                events: vec![],
                packed: 0,
            },
        }
    }
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn body(c: &Case) -> Vec<u32> {
    let s = c.body.state;
    let p = c.body.config;
    let mut out = vec![];
    append(&mut out, s.velocity);
    append(&mut out, [s.spin, s.ground]);
    out.extend([
        u32::from(s.timer),
        s.age as i32 as u32,
        u32::from(s.airborne),
        u32::from(s.reverse_spin),
        u32::from(s.special),
        u32::from(s.raised),
    ]);
    append(
        &mut out,
        [
            i32::from(p.elasticity),
            i32::from(p.friction),
            i32::from(p.height_factor),
            i32::from(p.hit_spin.multiplier as i16),
        ],
    );
    out.push(u32::from(p.hit_spin.shift));
    out.push(p.slow_spin.multiplier as i16 as i32 as u32);
    out.push(u32::from(p.slow_spin.shift));
    append(&mut out, [i32::from(p.deflection), p.gravity]);
    out.push(u32::from(p.height_period));
    out
}
fn input(c: &Case, out: &mut Vec<u32>) {
    out.extend([
        c.op,
        c.dt as u32,
        c.active as u32,
        c.threshold as u32,
        c.world.packed,
        c.seed,
        c.world.fail_at as u32,
        c.world.failure as u32,
        c.world.ground as u32,
        c.world.normal_ground as u32,
    ]);
    append(out, c.world.normal);
    out.push(u32::from(c.world.fallback));
    append(out, c.world.fallback_normal);
    append(out, c.collider.pose.position);
    append(out, c.collider.pose.matrix.0);
    out.push(c.external);
    append(out, c.collider.external_basis.map_or([0; 9], |m| m.0));
    out.extend(body(c));
    append(out, c.vehicle.position);
    append(out, c.vehicle.velocity);
    out.push(c.vehicle.forward_velocity as u32);
    append(out, c.vehicle.reference.0);
    out.push(u32::from(c.vehicle.external.is_some()));
    append(out, c.vehicle.external.map_or([0; 9], |m| m.0));
}
fn output(c: &Case, status: i32) -> Vec<u32> {
    let mut out = vec![status as u32, c.seed, c.active as u32, c.world.packed];
    append(&mut out, c.collider.pose.position);
    append(&mut out, c.collider.pose.matrix.0);
    out.push(u32::from(c.collider.external_basis.is_some()));
    append(&mut out, c.collider.external_basis.map_or([0; 9], |m| m.0));
    out.extend(body(c));
    out.push(c.world.events.len() as u32);
    out.extend(&c.world.events);
    out
}
#[test]
#[ignore = "requires C Bounce kernel oracle; set TANKRACER_BOUNCE_REFERENCE"]
fn branches_and_short_motion_sequences_match_native_c() {
    let mut cases = vec![];
    for speed in [0, 250000, 250001, -250000, -250001, i32::MIN] {
        for external in 0..3 {
            let mut c = Case {
                op: 0,
                active: 0,
                external,
                ..Default::default()
            };
            c.vehicle.forward_velocity = speed;
            c.collider.external_basis = match external {
                0 => None,
                1 => Some(c.collider.pose.matrix),
                _ => Some(Matrix::from_angles([3000, 4000, 5000])),
            };
            cases.push(c);
        }
    }
    let mut fail = Case {
        op: 0,
        active: 0,
        ..Default::default()
    };
    fail.world.fail_at = 1;
    cases.push(fail);
    for op in [0, 1] {
        cases.push(Case {
            op,
            external: 2,
            ..Default::default()
        });
    }
    for variant in 0..33 {
        let mut c = Case::default();
        match variant {
            0 => c.active = 0,
            1 => c.body.state.age = 20001,
            2 => c.body.state.age = 20000,
            3 => c.body.state.age = 32760,
            4 => c.body.state.timer = u16::MAX,
            5 => c.body.config.height_period = 0,
            6 => {
                c.dt = i32::MAX;
                c.body.state.velocity = [i32::MIN, i32::MAX, -1];
            }
            7 => c.body.state.spin = 12,
            8 => c.body.state.reverse_spin = 1,
            9 => c.body.config.slow_spin.shift = 35,
            10 => {
                c.collider.pose.position[1] = -100;
                c.body.state.velocity[1] = -50000;
            }
            11 => {
                c.collider.pose.position[1] = -100;
                c.body.state.velocity[1] = -100;
                c.threshold = 0;
            }
            12..=17 | 21..=24 => {
                c.body.state.airborne = 0;
                c.body.state.velocity = [0; 3];
                if variant == 13 {
                    c.body.state.velocity = [-100000, 0, -200000];
                }
                if variant == 14 {
                    c.world.normal_ground = MISS_HEIGHT;
                }
                if variant == 15 {
                    c.world.normal_ground = MISS_HEIGHT;
                    c.world.fallback = true;
                }
                if variant == 16 {
                    c.collider.external_basis = None;
                    c.external = 0;
                }
                if variant == 17 {
                    c.body.state.raised = 1;
                }
                if (21..=24).contains(&variant) {
                    c.world.fail_at = variant - 20;
                }
            }
            18 => {
                c.body.state.timer = 3;
                c.body.state.raised = 1;
                c.world.ground = 200;
            }
            19 => {
                c.body.state.timer = 3;
                c.world.ground = MISS_HEIGHT;
            }
            20 => {
                c.body.state.timer = 3;
                c.world.fail_at = 1;
            }
            25..=29 => {
                c.body.state.ground = MISS_HEIGHT;
                c.body.state.timer = 0;
                c.world.fallback = true;
                if variant == 26 {
                    c.world.fallback_normal = [0, 0, 4096];
                }
                if variant == 27 {
                    c.external = 0;
                    c.collider.external_basis = None;
                }
                if variant == 28 {
                    c.world.fail_at = 1;
                }
                if variant == 29 {
                    c.body.state.velocity = [i32::MIN, 12345, i32::MAX];
                }
            }
            30 => c.body.config.friction = -128,
            31 => {
                c.dt = -1;
                c.body.state.reverse_spin = 1;
            }
            32 => c.body.config.elasticity = -32768,
            _ => unreachable!(),
        }
        cases.push(c);
    }
    let mut words = vec![];
    let mut expected = vec![];
    for (case, mut c) in cases.iter().cloned().enumerate() {
        for step in 0..4 {
            input(&c, &mut words);
            c.world.events.clear();
            let motion = Motion {
                state: &mut c.body.state,
                collider: &mut c.collider,
                activity: &mut c.active,
                threshold: c.threshold,
                reference_alias: c.external == 1,
            };
            let result = if c.op == 0 {
                bounce::hit(motion, c.body.config, c.vehicle, &mut c.seed, &mut c.world)
            } else {
                bounce::tick(motion, c.body.config, c.dt, &mut c.world)
            };
            expected.push((case, step, output(&c, result.err().unwrap_or(0))));
            c.op = 1;
        }
    }
    let bytes: Vec<_> = words.into_iter().flat_map(u32::to_le_bytes).collect();
    let mut child =
        Command::new(std::env::var_os("TANKRACER_BOUNCE_REFERENCE").expect("set Bounce oracle"))
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
    assert_eq!(&output.stdout[..4], b"BO01");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    let mut at = 0;
    for (case, step, values) in &expected {
        for (word, &value) in values.iter().enumerate() {
            assert_eq!(actual[at], value, "case {case} step {step} word {word}");
            at += 1;
        }
    }
    assert_eq!(at, actual.len());
    println!(
        "Bounce kernels: {} scenarios / {} operations / {at} words",
        cases.len(),
        expected.len()
    );
}
