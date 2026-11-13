//! Production C prefix/helper entries, including phase boundaries and shared RNG.
use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::config::{Drive, Gain, Tank};
use tankracer_game::{
    contact_pipeline::Pipeline,
    dispatch::{self, Class, ClassId, MethodId},
    drive,
    effect_pool::{self, LineHandle, Pool},
    math::Matrix,
    motion,
    object_query::EntityId,
    pose, vehicle,
    vehicle_prefix::{self as prefix, boost, spin},
};

struct Fixture {
    state: prefix::State,
    contact: Pipeline,
    view: prefix::View,
    camera: boost::Camera,
    spin: spin::Shared,
    boost: boost::Shared,
    temporary: prefix::Temporary,
    pool: Pool,
    random: u32,
    slot: u32,
    other: boost::State,
    tuning: spin::Tuning,
    increments: [i32; 2],
    power: [i32; 2],
    loss: [i32; 2],
    camera_position: [i32; 3],
    sort: i16,
    applications: [usize; 2],
    rejected_methods: usize,
}
fn signed(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn gain(packed: u32) -> Gain {
    Gain {
        multiplier: packed as u16,
        shift: (packed >> 16) as u16,
    }
}
impl Fixture {
    fn words(&self) -> Vec<u32> {
        let mut out = Vec::new();
        let p = &self.state;
        signed(
            &mut out,
            [
                p.alternating_sign,
                p.countdown_throttle,
                p.countdown_rpm,
                p.target,
            ],
        );
        let s = &p.spin;
        out.extend([u32::from(s.flags), u32::from(s.visible)]);
        signed(&mut out, [s.hold, s.color_timer]);
        out.extend([s.color, s.target_color]);
        signed(&mut out, [s.angle, s.speed]);
        out.push(u32::from(s.disabled));
        let b = &p.boost;
        signed(&mut out, [b.extra_speed, b.power, b.power_loss]);
        out.push(u32::from(b.timer as u16));
        signed(&mut out, [b.camera_velocity, b.camera_timer]);
        out.extend([
            b.target.0,
            u32::from(b.active),
            u32::from(b.applied),
            u32::from(b.phase),
        ]);
        out.extend([
            u32::from(p.temporary_timer as u16),
            u32::from(p.temporary_slot as u16),
            p.restore_sidegrip.packed(),
            p.sidegrip.packed(),
        ]);
        out.extend(
            self.contact
                .sampling
                .auxiliary
                .each_ref()
                .map(|a| a.cached as u32),
        );
        out.extend([u32::from(self.view.enabled), u32::from(self.view.visible)]);
        signed(&mut out, self.view.position);
        out.push(self.view.angular_rate as u32);
        signed(&mut out, self.view.matrix.0);
        signed(
            &mut out,
            [self.camera.pitch, self.camera.height, self.camera.distance],
        );
        out.extend([
            u32::from(self.spin.count),
            u32::from(self.spin.pulse),
            self.boost.texture_frame,
            u32::from(self.boost.texture_delta),
        ]);
        out.extend(self.boost.source_uv.map(u32::from));
        out.extend(self.boost.current_uv.map(u32::from));
        signed(&mut out, self.boost.feedback);
        out.push(self.temporary.available);
        out.extend(self.temporary.timestamps);
        out.extend(self.temporary.lines.into_iter().flatten().map(|h| h.0));
        out.extend([self.random, self.slot, self.pool.line_count]);
        signed(&mut out, self.pool.lines.iter().map(|l| l.points[0][1]));
        signed(
            &mut out,
            [
                self.pool.overflow.points[0][1],
                self.pool.quads[159].points[3][2],
            ],
        );
        signed(
            &mut out,
            [
                self.other.extra_speed,
                self.other.power,
                self.other.power_loss,
            ],
        );
        out.push(u32::from(self.other.timer as u16));
        out
    }
    fn config(&self, out: &mut Vec<u32>) {
        let t = self.tuning;
        signed(out, [t.fast, t.slow, t.stop]);
        out.extend([t.accelerate, t.settle, 0x0e010003, t.fade]);
        signed(out, self.increments);
        signed(out, self.power);
        signed(out, self.loss);
        signed(out, self.camera_position);
        out.push(u32::from(self.sort as u16));
    }
    fn run(&mut self, op: Operation) -> (i32, u32) {
        let env = boost::Environment {
            dt: op.dt,
            frame: op.frame,
            player: op.player,
            camera_preset: op.preset,
            camera_variant: op.variant,
            vehicle_mode: op.vehicle_mode,
        };
        let increments = self.increments;
        let power = self.power;
        let loss = self.loss;
        let slot = &mut self.slot;
        let other = &mut self.other;
        let applications = &mut self.applications;
        let rejected_methods = &mut self.rejected_methods;
        let class = Class {
            maximum_slot: if op.dispatch == 2 { 0 } else { 1 },
            methods: vec![
                None,
                if op.dispatch == 1 {
                    None
                } else {
                    Some(MethodId(op.dispatch as u16))
                },
            ],
        };
        let apply = |own: &mut boost::State, target: EntityId| {
            let mut result = 0;
            dispatch::call_entity(&class, 1, slot, &mut result, |method, _, _| {
                if method == MethodId(3) {
                    *rejected_methods += 1;
                    return Err(-5);
                }
                assert!(target == EntityId(1) || target == EntityId(2));
                let (state, index, player) = if target == EntityId(1) {
                    (own, 0, op.player)
                } else {
                    (&mut *other, 1, !op.player)
                };
                let config = usize::from(!player);
                applications[index] += 1;
                state.apply(increments[index], power[config], loss[config]);
                Ok(())
            })
            .map_err(|error| match error {
                dispatch::Error::Method(c) => c,
                _ => -1,
            })
        };
        let mut result = 0;
        let status = match op.kind {
            0 => {
                // Only adapter inputs are synthetic here; the prefix's entire
                // mutable state is compared with C below, including failures.
                let mut vehicle = vehicle::State {
                    entity: EntityId(1),
                    class: ClassId(0),
                    player: op.player,
                    motion: motion::State::default(),
                    powertrain: Default::default(),
                    contact: std::mem::take(&mut self.contact),
                    pose: pose::Config::from_tank(&Tank::default(), 1, [0; 3]),
                    pose_input: motion::PoseInput {
                        timer: 0,
                        ambient_wobble: false,
                        session_active: false,
                        ambient_phase: 0,
                        recoil_mode: 0,
                        turret_yaw: 0,
                        external: pose::ExternalBasis::Body,
                    },
                    yaw_lock: 0,
                    collision: Default::default(),
                };
                let mut drive = Drive::default();
                let mut frame = vehicle::Frame {
                    dt: op.dt,
                    number: op.frame,
                    mode: op.mode,
                    input: drive::Input::default(),
                    drive: drive::Environment {
                        active: op.active,
                        ..Default::default()
                    },
                };
                let result = self.state.prepare_vehicle(
                    &mut vehicle,
                    &mut drive,
                    &mut frame,
                    prefix::Frame {
                        boost: env,
                        drive_active: op.active,
                        rpm_limit: op.engine_limit,
                        spin: self.tuning,
                        spin_work_limit: 65536,
                    },
                    prefix::World {
                        view: &mut self.view,
                        camera_position: self.camera_position,
                        camera: &mut self.camera,
                        spin: &mut self.spin,
                        boost: &mut self.boost,
                        temporary: &mut self.temporary,
                        pool: &mut self.pool,
                        random: &mut self.random,
                    },
                    apply,
                );
                self.contact = vehicle.contact;
                assert_eq!(vehicle.pose_input.timer, self.state.temporary_timer as u16);
                assert_eq!(drive.sidegrip_fraction, self.state.sidegrip);
                assert_eq!(frame.drive.boost_charge, self.state.boost.timer);
                assert_eq!(frame.drive.boost_power, self.state.boost.extra_speed);
                assert_eq!(frame.drive.extra_drive, self.state.boost.power);
                result
            }
            1 => {
                result =
                    self.state
                        .spin
                        .start(op.player, op.mode, &mut self.spin, &mut self.random);
                Ok(())
            }
            2 => self.state.spin.step(
                op.dt,
                self.tuning,
                op.limit,
                &mut self.spin,
                &mut self.random,
            ),
            3 => {
                self.state
                    .boost
                    .start(EntityId(1), op.preset, self.sort, &mut self.boost);
                Ok(())
            }
            4 => {
                let i = usize::from(!op.player);
                self.state.boost.apply(increments[0], power[i], loss[i]);
                Ok(())
            }
            5 => self
                .view
                .step(self.camera_position, op.dt, op.frame)
                .map_err(|_| -2),
            6 => self.state.boost.step(
                env,
                &mut self.camera,
                &mut self.boost,
                &mut self.random,
                apply,
            ),
            _ => panic!("unknown operation"),
        };
        (status.err().unwrap_or(0), result)
    }
}
#[derive(Clone, Copy)]
struct Operation {
    kind: u32,
    dt: i32,
    frame: u32,
    player: bool,
    active: bool,
    preset: bool,
    variant: bool,
    vehicle_mode: i16,
    limit: u32,
    dispatch: u32,
    engine_limit: i32,
    mode: i32,
}
impl Operation {
    fn words(self) -> [u32; 12] {
        [
            self.kind,
            self.dt as u32,
            self.frame,
            u32::from(self.player),
            u32::from(self.active),
            u32::from(self.preset),
            u32::from(self.variant),
            u32::from(self.vehicle_mode as u16),
            self.limit,
            self.dispatch,
            self.engine_limit as u32,
            self.mode as u32,
        ]
    }
}
fn fixture(case: usize) -> Fixture {
    let mut state = prefix::State {
        alternating_sign: [1, -1, i32::MIN][case % 3],
        countdown_throttle: [0, 215000, -215000, i32::MIN][case % 4],
        countdown_rpm: [0, 213500, -500, i32::MAX][case % 4],
        target: 0x1234,
        temporary_timer: [0, 1, 16, 17, 40, -1, 32767][case % 7],
        temporary_slot: (case % 4) as i16,
        sidegrip: gain(0x08000003),
        restore_sidegrip: gain(0x09000005),
        ..prefix::State::default()
    };
    state.spin = spin::State {
        flags: [0, 1, 2, 4, 12, 3, 6][case % 7],
        visible: (case % 2) as u8,
        hold: [0, 1, 40, 4000][case % 4],
        color_timer: [0, 59, 60, 61][case % 4],
        color: [0, 1, 0xabcdefe7, 0xfffe, 0xf800, 0x7c0, 0x3e][case % 7],
        target_color: [0, 1, 0xabcdefe7, 0xfffe, 0xf800, 0x7c0, 0x3e][case % 7],
        angle: [0, 32760, -32759, i32::MAX, i32::MIN][case % 5],
        speed: [0, 1, 27955, 16307, 931, 932, -1][case % 7],
        disabled: 0,
    };
    state.boost = boost::State {
        extra_speed: [0, 490000, 500000, i32::MAX][case % 4],
        power: [0, 65000, -100, i32::MAX][case % 4],
        power_loss: [50, -50, 0, i32::MAX][case % 4],
        timer: [0, 1, 16, 17, 2500, 5000, -1][case % 7],
        camera_velocity: [0, 100, 5000, -15000, 1][case % 5],
        camera_timer: [0, 184, 199, 200, 334, 349, 999, -1][case % 8],
        target: EntityId(1 + (case / 3 % 2) as u32),
        active: u8::from(!(case / 5).is_multiple_of(3)),
        applied: (case / 2 % 2) as u8,
        phase: [0, 1, 2, 4, 8, 9][case / 4 % 6],
    };
    let mut contact = Pipeline::default();
    contact.sampling.auxiliary[0].cached = 1;
    contact.sampling.auxiliary[1].cached = 7;
    let mut pool = Pool {
        line_count: 20,
        ..Pool::default()
    };
    for (i, line) in pool.lines.iter_mut().enumerate() {
        line.points[0][1] = if i < 20 {
            i as i32 + 100
        } else {
            effect_pool::FREE
        };
    }
    pool.overflow.points[0][1] = 42;
    pool.quads[159].points[3][2] = 43;
    let temporary = prefix::Temporary {
        available: u32::MAX,
        timestamps: [10, 20, 30, 40],
        lines: std::array::from_fn(|slot| {
            std::array::from_fn(|i| {
                if slot == 3 && i == 0 {
                    LineHandle::EXHAUSTED
                } else if slot == 3 && i == 1 {
                    LineHandle::OVERFLOW
                } else {
                    LineHandle(((slot * 3 + i) % 20) as u32)
                }
            })
        }),
    };
    let mut tuning = spin::Tuning::default();
    if case.is_multiple_of(11) {
        tuning.accelerate = 0;
    }
    Fixture {
        state,
        contact,
        view: prefix::View {
            enabled: !case.is_multiple_of(4),
            visible: true,
            position: [1024, -111, -2048],
            angular_rate: [0, 4193, -4193][case % 3],
            matrix: Matrix::from_angles([1000, 2000, 3000]),
        },
        camera: boost::Camera {
            pitch: [0, 1, -1638][case % 3],
            height: [450, 2000, 5501][case % 3],
            distance: [-7800, -7799, -7200, -7199, -9000, -8999, -8200, -8199][case % 8],
        },
        spin: spin::Shared {
            count: if case.is_multiple_of(2) { 0 } else { 255 },
            pulse: 7,
        },
        boost: boost::Shared {
            texture_frame: (case % 3) as u32,
            texture_delta: 19,
            source_uv: [0, 29],
            current_uv: [65535, 111],
            feedback: [-16, 20, 30],
        },
        temporary,
        pool,
        random: (case as u32).wrapping_mul(982451653),
        slot: 77,
        other: boost::State {
            extra_speed: 490000,
            power: 7000,
            power_loss: 33,
            timer: 49,
            ..boost::State::default()
        },
        tuning,
        increments: [20000, 30000],
        power: [65000, 64000],
        loss: [50, 51],
        camera_position: [128 + [0, 19199, 19200, -19200][case % 4], 0, -256],
        sort: -16,
        applications: [0; 2],
        rejected_methods: 0,
    }
}

#[test]
#[ignore = "requires production C prefix oracle; set TANKRACER_VEHICLE_PREFIX_REFERENCE"]
fn active_prefix_and_ability_branches_match_production_c() {
    const CASES: usize = 232;
    let mut input = vec![CASES as u32];
    let mut expected = Vec::new();
    let mut errors = 0;
    let mut operations = 0;
    let mut applications = [0; 2];
    let mut rejected_methods = 0;
    for case in 0..CASES {
        let mut f = fixture(case);
        input.extend(f.words());
        f.config(&mut input);
        let kinds: &[u32] = if !(168..224).contains(&case) {
            &[0, 0, 0, 0, 0, 0]
        } else {
            &[1, 2, 2, 3, 6, 4, 0]
        };
        input.push(kinds.len() as u32);
        expected.push(kinds.len() as u32);
        for (step, kind) in kinds.iter().enumerate() {
            let op = Operation {
                kind: *kind,
                dt: if case >= 224 {
                    -(case as i32 % 2)
                } else {
                    [16, 40, 1, 100, 256, 32768, 65535][(case + step) % 7]
                },
                frame: (step / 2) as u32 + if case % 3 == 0 { 30 } else { 0 },
                player: case % 2 == 0,
                active: case % 3 == 0,
                preset: case / 24 % 2 != 0,
                variant: case / 48 % 2 != 0,
                vehicle_mode: if case % 13 == 0 { 3 } else { 0 },
                limit: if case % 17 == 0 {
                    0
                } else if case % 5 == 0 {
                    1
                } else {
                    65536
                },
                dispatch: (case / 7 % 4) as u32,
                engine_limit: if case % 19 == 0 { 0 } else { 215000 },
                mode: [0, 4, 5][case % 3],
            };
            input.extend(op.words());
            let (status, result) = f.run(op);
            errors += usize::from(status != 0);
            expected.extend([status as u32, result]);
            expected.extend(f.words());
            operations += 1;
        }
        for (total, count) in applications.iter_mut().zip(f.applications) {
            *total += count;
        }
        rejected_methods += f.rejected_methods;
    }
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests");
    fs::create_dir_all(&root).unwrap();
    let path = root.join("vehicle-prefix-input.bin");
    fs::write(
        &path,
        input
            .iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output =
        Command::new(std::env::var_os("TANKRACER_VEHICLE_PREFIX_REFERENCE").expect("set oracle"))
            .stdin(Stdio::from(fs::File::open(path).unwrap()))
            .output()
            .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"VP01");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(actual[0], CASES as u32);
    assert_eq!(actual.len() - 1, expected.len());
    for (word, (actual, expected)) in actual[1..].iter().zip(&expected).enumerate() {
        assert_eq!(
            actual,
            expected,
            "word {word} ({} words per snapshot)",
            fixture(0).words().len()
        );
    }
    assert!(errors > 0);
    assert!(applications.into_iter().all(|count| count > 0));
    assert!(rejected_methods > 0);
    println!(
        "{CASES} scenarios / {operations} operations / {} words / {errors} error prefixes; {} own + {} peer applications / {rejected_methods} rejected methods",
        expected.len(),
        applications[0],
        applications[1]
    );
}
