//! Mover branch inputs against original C construction/tick. Parent/list/terrain
//! responses are explicit here; SR05 uses real owners and authored BIO resources.
use std::{
    io::Write,
    process::{Command, Stdio},
};
use tankracer_assets::{object_catalog::Catalog, parameters::Parameters};
use tankracer_game::{
    geometry::Bounds,
    math::Matrix,
    mover::{self, Inputs, State, Tick},
    object_query::{Collider, EntityId},
    pose::Pose,
    spin::{self, Component, Motion},
    terrain::MISS_HEIGHT,
};
#[derive(Clone)]
struct World {
    height: i32,
    normal: [i32; 3],
    fail_at: usize,
    calls: usize,
    events: Vec<u32>,
}
impl World {
    fn event(&mut self, id: u32, args: &[i32]) -> Result<(), i32> {
        self.events.push(id);
        self.events.extend(args.iter().map(|v| *v as u32));
        self.calls += 1;
        if self.calls == self.fail_at {
            Err(-13)
        } else {
            Ok(())
        }
    }
}
impl mover::Services for World {
    fn normal_height(&mut self, x: i32, z: i32, n: &mut [i32; 3]) -> Result<i32, i32> {
        self.event(3, &[x, z])?;
        *n = self.normal;
        Ok(self.height)
    }
    fn stop(&mut self) -> Result<(), i32> {
        self.event(4, &[])
    }
}
#[derive(Clone)]
struct Case {
    op: u32,
    limit: u32,
    active: i32,
    mover: State,
    spin: Component,
    collider: Collider,
    inputs: Inputs,
    debris: bool,
    world: World,
    parameters: &'static str,
}
impl Default for Case {
    fn default() -> Self {
        Self {
            op: 1,
            limit: 10000,
            active: 1,
            mover: State {
                flags: 1,
                ..Default::default()
            },
            spin: Component {
                state: spin::State {
                    velocity: [100, 20, 1000],
                    normal: [0, 4096, 0],
                    ..Default::default()
                },
                ..Default::default()
            },
            collider: Collider {
                entity: EntityId(1),
                pose: Pose {
                    position: [300, 500, 700],
                    matrix: Matrix::IDENTITY,
                },
                external_basis: Some(Matrix::IDENTITY),
                radius: 128,
                bounds: Bounds {
                    min: [-20; 3],
                    max: [20; 3],
                },
            },
            inputs: Inputs {
                dt: 16,
                camera: [0; 3],
                players: [Some([10000, 0, 10000]), None],
            },
            debris: false,
            world: World {
                height: 15,
                normal: [0, 4096, 0],
                fail_at: 0,
                calls: 0,
                events: vec![],
            },
            parameters: "",
        }
    }
}
fn append(v: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    v.extend(values.into_iter().map(|n| n as u32));
}
fn fields(v: &mut Vec<u32>, m: State) {
    v.push(m.flags);
    append(v, [m.distance, m.travel, m.speed, m.activation_range]);
}
fn encode(v: &mut Vec<u32>, c: &Case) {
    v.extend([c.op, c.inputs.dt as u32, c.limit, c.active as u32]);
    fields(v, c.mover);
    v.push(u32::from(c.spin.state.timer));
    append(v, c.spin.state.velocity);
    append(v, c.spin.state.normal);
    append(v, c.collider.pose.position);
    append(v, c.collider.pose.matrix.0);
    v.push(if c.collider.external_basis.is_none() {
        0
    } else if c.spin.reference_alias {
        1
    } else {
        2
    });
    append(v, c.collider.external_basis.unwrap_or(Matrix::IDENTITY).0);
    v.extend([
        u32::from(c.spin.config.flatten.multiplier),
        u32::from(c.spin.config.flatten.shift),
    ]);
    append(v, c.inputs.camera);
    for p in c.inputs.players {
        v.push(u32::from(p.is_some()));
        append(v, p.unwrap_or([0; 3]));
    }
    v.extend([
        u32::from(c.debris),
        u32::from(c.debris),
        c.world.height as u32,
    ]);
    append(v, c.world.normal);
    v.extend([
        c.world.fail_at as u32,
        (-13_i32) as u32,
        c.parameters.len() as u32,
    ]);
    v.extend(c.parameters.bytes().map(u32::from));
}
#[test]
#[ignore = "requires C Mover oracle; set TANKRACER_MOVER_REFERENCE"]
fn constructor_proximity_camera_gating_endpoints_and_short_sequences_match_c() {
    let mut cases = vec![];
    for parameters in [
        "",
        ":speed 10 :move_range 60 :oscillating true :collidable true :explode true :lights )",
        ":Speed -10 :Dist 15 :Move_range 50 :Snap_to_track tRuE :Flip_180 TRUE )",
        ":oscillating 1 :collidable truth :snap_to_track false :explode TRUE :flip_180 true )",
        ":type missing :dist -1 :speed 8192 :move_range -2 )",
        ":dist 12",
        ":speed 12",
        ":move_range 12",
        ":flip_180 true",
        ":Lights :Beep unused )",
        ":dist true :speed false :move_range 12junk )",
    ] {
        cases.push(Case {
            op: 0,
            parameters,
            mover: State::default(),
            ..Default::default()
        });
    }
    for variant in 0..35 {
        let mut c = Case::default();
        match variant {
            0 => (),
            1 => c.active = 0,
            2 => {
                c.active = 0;
                c.mover.flags = 64;
                c.debris = true;
            }
            3 => c.mover.flags = 65,
            4 => c.mover.flags = 3,
            5 => {
                c.mover.flags = 0;
                c.spin.state.timer = 4;
            }
            6 => c.mover.flags = 0,
            7 => {
                c.mover.flags = 0;
                c.inputs.players = [None, None];
            }
            8 => {
                c.mover.flags = 0;
                c.inputs.players = [Some([51200, 0, 700]), None];
            }
            9 => {
                c.mover.flags = 0;
                c.inputs.players = [None, Some([300, 0, 700])];
            }
            10 => {
                c.mover.flags = 0;
                c.spin.state.timer = 255;
            }
            11 => {
                c.inputs.camera = [12837, 0, 0];
            }
            12 => {
                c.inputs.camera = [0, 0, -12713];
            }
            13 => {
                c.mover.flags = 9;
                c.spin.state.timer = 2;
            }
            14 => {
                c.mover.flags = 9;
                c.world.normal = [300, 4000, -100];
            }
            15 => {
                c.mover.flags = 9;
                c.world.height = MISS_HEIGHT;
            }
            16 => {
                c.mover.flags = 9;
                c.collider.external_basis = None;
            }
            17 => {
                c.mover.distance = 80;
            }
            18 => {
                c.mover.flags = 17;
                c.mover.distance = 80;
            }
            19 => {
                c.mover.flags = 49;
                c.mover.distance = 80;
            }
            20 => {
                c.mover.flags = 17;
                c.mover.distance = 80;
                c.mover.speed = -1310;
            }
            21 => {
                c.mover.flags = 49;
                c.mover.distance = 80;
                c.spin.reference_alias = false;
                c.collider.external_basis = Some(Matrix::IDENTITY);
            }
            22 => {
                c.mover.flags = 49;
                c.mover.distance = 80;
                c.collider.external_basis = None;
            }
            23 => {
                c.inputs.dt = 0;
            }
            24 => {
                c.inputs.dt = -16;
            }
            25 => {
                c.mover.distance = -1;
            }
            26 => {
                c.mover.flags = 0;
                c.mover.activation_range = 10240001;
                c.inputs.players = [None, None];
            }
            27 => {
                c.mover.flags = 9;
                c.spin.state.timer = 255;
            }
            28 => {
                c.mover.flags = 9;
                c.spin.config.flatten.multiplier = 65535;
                c.spin.config.flatten.shift = 40;
            }
            29 => {
                c.mover.travel = i32::MAX;
                c.mover.speed = i32::MAX;
                c.spin.state.velocity = [i32::MAX, 0, i32::MIN];
            }
            30 => {
                c.mover.flags = 0;
                c.inputs.players = [Some([i32::MIN, 0, i32::MAX]), None];
            }
            31 => {
                c.limit = 0;
            }
            32 => {
                c.world.fail_at = 1;
                c.mover.flags = 9;
            }
            33 => {
                c.world.fail_at = 1;
                c.mover.distance = 0;
            }
            _ => {
                c.mover.flags = 0;
                c.mover.activation_range = 1000;
                c.inputs.players = [Some([1300, 0, 700]), None];
            }
        }
        cases.push(c);
    }
    for fail_at in 1..=5 {
        let mut c = Case {
            op: 0,
            parameters: ":lights :speed 10 )",
            ..Default::default()
        };
        c.world.fail_at = fail_at;
        cases.push(c);
    }
    let mut input = vec![];
    let mut expected = vec![];
    for (case, mut c) in cases.iter().cloned().enumerate() {
        for step in 0..4 {
            c.world.calls = 0;
            c.world.events.clear();
            encode(&mut input, &c);
            let result = (|| {
                if c.limit == 0 {
                    return Err(-1);
                }
                if c.op == 0 {
                    c.world.event(9, &[])?;
                    let p = Parameters::new(c.parameters).unwrap();
                    c.mover.construct(p, &Catalog::default());
                    if p.find(":lights").is_some() {
                        c.world.event(6, &[])?;
                        c.world.event(7, &[])?;
                        c.world.event(8, &[])?;
                    }
                    mover::set_velocity(
                        &c.mover,
                        &mut Motion {
                            state: &mut c.spin.state,
                            collider: &mut c.collider,
                            activity: &mut c.active,
                            reference_alias: c.spin.reference_alias,
                        },
                    );
                    c.world.event(5, &[])?;
                    c.active = 1;
                } else {
                    let action = mover::tick(
                        &mut c.mover,
                        Motion {
                            state: &mut c.spin.state,
                            collider: &mut c.collider,
                            activity: &mut c.active,
                            reference_alias: c.spin.reference_alias,
                        },
                        c.spin.config,
                        c.inputs,
                        c.debris,
                        &mut c.world,
                    )?;
                    match action {
                        Tick::None => (),
                        Tick::Spin => c.world.event(1, &[])?,
                        Tick::Debris => c.world.event(2, &[])?,
                    }
                }
                Ok(())
            })();
            let mut out = vec![result.err().unwrap_or(0) as u32, c.active as u32];
            fields(&mut out, c.mover);
            out.push(u32::from(c.spin.state.timer));
            append(&mut out, c.spin.state.velocity);
            append(&mut out, c.spin.state.normal);
            append(&mut out, c.collider.pose.position);
            append(&mut out, c.collider.pose.matrix.0);
            out.push(u32::from(c.collider.external_basis.is_some()));
            append(&mut out, c.collider.external_basis.map_or([0; 9], |m| m.0));
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
        Command::new(std::env::var_os("TANKRACER_MOVER_REFERENCE").expect("set Mover oracle"))
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
    assert_eq!(&output.stdout[..4], b"MV01");
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
        "Mover: {} scenarios / {} operations / {} words",
        cases.len(),
        expected.len(),
        at
    );
}
