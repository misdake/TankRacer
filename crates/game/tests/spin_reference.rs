//! Declared branch inputs against the unchanged C Spin kernels. SR04 checks
//! real resources/factory/terrain/collision owners separately.
use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_game::{
    bounce::Hit,
    geometry::Bounds,
    math::Matrix,
    object_query::{Collider, EntityId},
    pose::Pose,
    spin::{self, Component, Motion, Services},
    terrain::MISS_HEIGHT,
};

#[derive(Clone)]
struct Terrain {
    ground: i32,
    normal_ground: i32,
    normal: [i32; 3],
    fallback: bool,
    fallback_normal: [i32; 3],
    fail_at: usize,
    calls: usize,
    events: Vec<u32>,
}
impl Terrain {
    fn event(&mut self, id: u32, args: &[i32]) -> Result<(), i32> {
        self.calls += 1;
        self.events.push(id);
        self.events.extend(args.iter().map(|v| *v as u32));
        if self.calls == self.fail_at {
            Err(-13)
        } else {
            Ok(())
        }
    }
}
impl Services for Terrain {
    fn stop(&mut self) -> Result<(), i32> {
        self.event(8, &[])
    }
    fn normal_height(&mut self, x: i32, z: i32, n: &mut [i32; 3]) -> Result<i32, i32> {
        self.event(4, &[x, z])?;
        *n = self.normal;
        Ok(self.normal_ground)
    }
    fn height(&mut self, x: i32, z: i32) -> Result<i32, i32> {
        self.event(3, &[x, z])?;
        Ok(self.ground)
    }
    fn fallback(&mut self, p: [i32; 3], n: &mut [i32; 3]) -> Result<bool, i32> {
        self.event(5, &[p[0], p[2], p[1]])?;
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
    weight: i32,
    blocked: bool,
    auxiliary: bool,
    auxiliary_active: bool,
    gravity: i32,
    bounds: [[i32; 3]; 2],
    collider: Collider,
    body: Component,
    vehicle: Hit,
    world: Terrain,
}
impl Default for Case {
    fn default() -> Self {
        Self {
            op: 1,
            dt: 16,
            active: 1,
            threshold: 10,
            weight: 20,
            blocked: false,
            auxiliary: false,
            auxiliary_active: false,
            gravity: 5,
            bounds: [[-31, -7, -52], [37, 23, 61]],
            collider: Collider {
                entity: EntityId(1),
                pose: Pose {
                    position: [300, 100, 600],
                    matrix: Matrix::IDENTITY,
                },
                external_basis: Some(Matrix::IDENTITY),
                radius: 64,
                bounds: Bounds {
                    min: [-100; 3],
                    max: [100; 3],
                },
            },
            body: Component {
                state: spin::State {
                    velocity: [300, -20, 600],
                    yawing: 1,
                    normal: [0, 4096, 0],
                    angular_velocity: [-200, 5000, 100],
                    ..Default::default()
                },
                ..Default::default()
            },
            vehicle: Hit {
                position: [700, 200, 900],
                velocity: [1200, -500, 600000],
                forward_velocity: 600000,
                reference: Matrix::IDENTITY,
                external: Some(Matrix::IDENTITY),
            },
            world: Terrain {
                ground: 0,
                normal_ground: 0,
                normal: [0, 4096, 0],
                fallback: false,
                fallback_normal: [4096, 0, 0],
                fail_at: 0,
                calls: 0,
                events: vec![],
            },
        }
    }
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn fields(b: &Component) -> Vec<u32> {
    let (s, c) = (b.state, b.config);
    let mut v = vec![];
    append(&mut v, s.velocity);
    v.extend([
        s.yawing,
        c.elasticity as i32 as u32,
        c.friction as i32 as u32,
        u32::from(s.timer),
        s.pivot_axis,
    ]);
    append(&mut v, [s.ground, s.edge_ground]);
    append(&mut v, s.normal);
    append(&mut v, s.angular_velocity);
    v.push(c.yaw_factor as i32 as u32);
    for g in [c.hit_x, c.hit_z, c.hit_slow, c.yaw_slow, c.flatten] {
        v.extend([g.multiplier as i16 as i32 as u32, u32::from(g.shift)]);
    }
    append(&mut v, [i32::from(c.stop_yaw), c.deflection]);
    v
}
fn encode(out: &mut Vec<u32>, c: &Case) {
    out.extend([
        c.op,
        c.dt as u32,
        c.active as u32,
        c.threshold as u32,
        c.weight as u32,
        if c.blocked { 32 } else { 0 },
        u32::from(c.auxiliary),
        u32::from(c.auxiliary_active),
        c.gravity as u32,
        u32::from(c.collider.radius),
    ]);
    append(out, c.bounds.into_iter().flatten());
    out.extend([
        c.world.fail_at as u32,
        (-13_i32) as u32,
        c.world.ground as u32,
        c.world.normal_ground as u32,
    ]);
    append(out, c.world.normal);
    out.push(u32::from(c.world.fallback));
    append(out, c.world.fallback_normal);
    append(out, c.collider.pose.position);
    append(out, c.collider.pose.matrix.0);
    out.push(if c.collider.external_basis.is_none() {
        0
    } else if c.body.reference_alias {
        1
    } else {
        2
    });
    append(out, c.collider.external_basis.unwrap_or(Matrix::IDENTITY).0);
    out.extend(fields(&c.body));
    append(out, c.vehicle.position);
    append(out, c.vehicle.velocity);
    out.extend([
        c.vehicle.forward_velocity as u32,
        u32::from(c.vehicle.external.is_some()),
    ]);
    append(out, c.vehicle.external.unwrap_or(Matrix::IDENTITY).0);
}
#[test]
#[ignore = "requires C Spin oracle; set TANKRACER_SPIN_REFERENCE"]
fn hits_edge_pivots_sampling_damping_and_short_sequences_match_c() {
    let mut cases = vec![];
    for direction in 0..8 {
        for extra in 0..6 {
            let mut c = Case {
                op: 0,
                active: 0,
                ..Default::default()
            };
            c.vehicle.velocity = match direction {
                0 => [0, 10, 600000],
                1 => [0, -10, -600000],
                2 => [600000, 20, 0],
                3 => [-600000, 20, 0],
                4 => [300000, 0, 300000],
                5 => [0; 3],
                6 => [i32::MAX, 0, i32::MIN],
                _ => [-300000, 30, -300000],
            };
            match extra {
                0 => (),
                1 => c.blocked = true,
                2 => c.active = 1,
                3 => c.collider.external_basis = None,
                4 => {
                    c.body.reference_alias = false;
                    c.collider.external_basis = Some(Matrix::from_angles([1000, 12000, 2000]));
                }
                _ => {
                    c.vehicle.external = None;
                    c.body.config.deflection = 1820;
                    c.body.config.yaw_factor = -123;
                    c.vehicle.forward_velocity = -600000;
                }
            }
            cases.push(c);
        }
    }
    for variant in 0..28 {
        let mut c = Case::default();
        match variant {
            0 => (),
            1 => c.active = 0,
            2 => c.body.state.timer = 9,
            3 => c.body.state.timer = 255,
            4 => c.body.state.timer = 2,
            5 => c.collider.radius = 6144,
            6 => c.collider.radius = 6145,
            7 => {
                c.collider.radius = 8000;
                c.world.ground = MISS_HEIGHT;
            }
            8 => {
                c.world.normal_ground = MISS_HEIGHT;
                c.world.fallback = true;
                c.body.state.velocity = [-5000, 10, 50];
            }
            9 => {
                c.body.state.edge_ground = MISS_HEIGHT;
                c.world.fallback = true;
                c.body.state.velocity = [300, -3, -600];
            }
            10 => {
                c.body.state.normal = [100, 4000, -300];
                c.world.normal = c.body.state.normal;
            }
            11 => {
                c.body.state.pivot_axis = 1;
                c.body.state.angular_velocity = [0, -5000, -200];
            }
            12 => c.body.state.pivot_axis = 2,
            13 => {
                c.body.state.angular_velocity = [1, 0, 0];
                c.body.config.yaw_slow.multiplier = 0;
            }
            14 => {
                c.body.state.angular_velocity = [-1, -1, 0];
            }
            15 => {
                c.collider.pose.position[1] = -1;
                c.body.state.angular_velocity = [0; 3];
                c.body.state.yawing = 0;
                c.body.state.velocity = [-100000, 0, -100000];
            }
            16 => {
                c.collider.pose.position[1] = 0;
                c.body.state.angular_velocity = [0; 3];
                c.body.state.yawing = 0;
                c.body.state.velocity = [0; 3];
            }
            17 => {
                c.collider.external_basis = None;
            }
            18 => {
                c.body.state.timer = 9;
                c.body.state.yawing = 0;
                c.collider.pose.matrix = Matrix([4000, 1, 0, 65407, 339, 0, 0, 0, 4096]);
                c.collider.external_basis = Some(c.collider.pose.matrix);
            }
            19 => {
                c.body.reference_alias = false;
                c.collider.external_basis = Some(Matrix::IDENTITY);
            }
            20 => {
                c.body.config.friction = -120;
                c.body.config.hit_slow.shift = 40;
                c.body.config.flatten.shift = 36;
            }
            21 => {
                c.body.config.hit_slow.multiplier = 65535;
                c.body.config.yaw_slow.multiplier = 65535;
            }
            22 => {
                c.dt = 0;
            }
            23 => {
                c.dt = -16;
            }
            24 => {
                c.body.state.yawing = 0;
                c.body.state.angular_velocity = [0; 3];
                c.body.state.normal = [0; 3];
                c.world.normal = [0; 3];
            }
            25 => {
                c.collider.pose.position = [i32::MAX, -100, i32::MIN];
                c.body.state.velocity = [i32::MIN, i32::MAX, i32::MAX];
            }
            26 => {
                c.auxiliary = true;
                c.auxiliary_active = true;
            }
            _ => {
                c.active = 0;
                c.auxiliary = true;
                c.auxiliary_active = true;
            }
        }
        cases.push(c);
    }
    // Fail at individual callbacks; the returned state must preserve exactly
    // the prefix before the failure, including stop's activity change.
    for fail_at in 1..=7 {
        cases.push(Case {
            collider: Collider {
                radius: 8000,
                ..Case::default().collider
            },
            auxiliary: true,
            auxiliary_active: true,
            world: Terrain {
                fail_at,
                ..Case::default().world
            },
            ..Default::default()
        });
    }
    for op in [0, 1] {
        let mut c = Case {
            op,
            active: 0,
            auxiliary: true,
            auxiliary_active: true,
            ..Default::default()
        };
        c.world.fail_at = 1;
        cases.push(c);
    }
    for blocked in [false, true] {
        cases.push(Case {
            op: 0,
            active: 0,
            auxiliary: true,
            auxiliary_active: true,
            blocked,
            ..Default::default()
        });
    }
    let mut stop = Case {
        ..Default::default()
    };
    stop.collider.pose.position[1] = 0;
    stop.body.state.angular_velocity = [0; 3];
    stop.body.state.yawing = 0;
    stop.body.state.velocity = [0; 3];
    stop.body.state.timer = 1;
    stop.world.fail_at = 1;
    cases.push(stop);
    let mut input = vec![];
    let mut expected = vec![];
    for (case, mut c) in cases.iter().cloned().enumerate() {
        for step in 0..4 {
            c.world.events.clear();
            c.world.calls = 0;
            encode(&mut input, &c);
            let result = (|| {
                let motion = Motion {
                    state: &mut c.body.state,
                    collider: &mut c.collider,
                    activity: &mut c.active,
                    reference_alias: c.body.reference_alias,
                };
                if c.op == 0 {
                    spin::hit(
                        motion,
                        c.body.config,
                        c.weight,
                        c.vehicle,
                        c.blocked,
                        || c.world.event(1, &[]),
                    )?;
                    if c.auxiliary {
                        c.world.event(2, &[])?;
                        c.active = 0;
                    }
                } else {
                    spin::tick(
                        motion,
                        c.body.config,
                        c.threshold,
                        c.bounds,
                        c.gravity,
                        c.dt,
                        &mut c.world,
                    )?;
                    if c.auxiliary && c.auxiliary_active {
                        c.world.event(2, &[])?;
                    }
                }
                Ok::<_, i32>(())
            })();
            let mut out = vec![result.err().unwrap_or(0) as u32, c.active as u32];
            append(&mut out, c.collider.pose.position);
            append(&mut out, c.collider.pose.matrix.0);
            out.push(u32::from(c.collider.external_basis.is_some()));
            append(&mut out, c.collider.external_basis.map_or([0; 9], |m| m.0));
            out.extend(fields(&c.body));
            out.push(c.world.events.len() as u32);
            out.extend(&c.world.events);
            expected.push((case, step, out));
            if result.is_err() {
                break;
            }
            c.op = 1;
        }
    }
    let bytes: Vec<_> = input.into_iter().flat_map(u32::to_le_bytes).collect();
    let mut child =
        Command::new(std::env::var_os("TANKRACER_SPIN_REFERENCE").expect("set Spin oracle"))
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
    assert_eq!(&output.stdout[..4], b"SP01");
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
        "Spin: {} scenarios / {} operations / {} words",
        cases.len(),
        expected.len(),
        at
    );
}
