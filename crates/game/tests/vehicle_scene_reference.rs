//! Full after-pose ordering, real effects/pools/RNG and alternating vehicle owners.
use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    collision_effects, contact_pipeline,
    dispatch::ClassId,
    drive,
    effect_pool::{LineHandle, Pool, Quad, QuadHandle},
    impact_particles,
    math::Matrix,
    motion,
    object_query::EntityId,
    pose, powertrain, random, vehicle,
    vehicle_scene::{chain, debris, lights, marks, pickup, shield, smoke, spray, temporary},
};

/// Native-boundary field codec only. Production Rust never sees data addresses.
#[derive(Default)]
struct Fields(Vec<(u32, u32, u32)>);
impl Fields {
    fn put(&mut self, a: u32, n: u32, v: u32) {
        self.0.push((a, n, v));
    }
    fn word(&mut self, a: u32, v: u32) {
        self.put(a, 4, v);
    }
    fn ints(&mut self, a: u32, values: impl IntoIterator<Item = i32>) {
        for (i, v) in values.into_iter().enumerate() {
            self.word(a + i as u32 * 4, v as u32);
        }
    }
    fn bytes(&mut self, a: u32, values: impl IntoIterator<Item = u8>) {
        for (i, v) in values.into_iter().enumerate() {
            self.put(a + i as u32, 1, u32::from(v));
        }
    }
    fn quad(&mut self, a: u32, q: Quad) {
        self.ints(a, q.points.into_iter().flatten());
        self.word(a + 48, q.material);
        self.bytes(a + 52, q.style);
    }
    fn wire(&self, out: &mut Vec<u32>) {
        out.push(self.0.len() as u32);
        for &(a, n, v) in &self.0 {
            out.extend([a, n, v]);
        }
    }
    fn values(&self) -> impl Iterator<Item = u32> + '_ {
        self.0.iter().map(|&(_, n, v)| {
            v & match n {
                1 => 255,
                2 => 65535,
                _ => u32::MAX,
            }
        })
    }
}
fn object(i: usize) -> u32 {
    0x2000 + i as u32 * 0x2000
}
fn pointer(entity: Option<EntityId>) -> u32 {
    entity.map_or(0, |e| object(e.0 as usize - 1))
}
#[derive(Clone, Default, PartialEq, Eq)]
struct Local {
    spray: spray::State,
    smoke: smoke::State,
    marks: marks::State,
    lights: lights::State,
}
struct Fixture {
    local: [Local; 2],
    debris_vehicles: [debris::Vehicle; 2],
    accumulators: [i32; 2],
    cooldowns: [u32; 2],
    debris: debris::State,
    pickup: pickup::State,
    temporary: temporary::State,
    shield: shield::State,
    impacts: impact_particles::State,
    pool: Pool,
    random: u32,
}
impl Fixture {
    fn fields(&self) -> Fields {
        let mut f = Fields::default();
        for (i, l) in self.local.iter().enumerate() {
            let o = object(i);
            for (j, p) in l.spray.particles.iter().enumerate() {
                let a = o + 0x990 + j as u32 * 34;
                f.ints(a, p.position);
                f.ints(a + 12, p.velocity);
                for (k, v) in [p.life as u16, p.size, p.floor as u16, p.rotation]
                    .into_iter()
                    .enumerate()
                {
                    f.put(a + 24 + k as u32 * 2, 2, u32::from(v));
                }
                f.bytes(a + 32, [p.kind as u8, p.side]);
            }
            f.ints(
                o + 0xaa0,
                [l.spray.timer, l.spray.side as i32, l.spray.count as i32],
            );
            for (j, p) in l.smoke.particles.iter().enumerate() {
                let a = o + 0x798 + j as u32 * 32;
                f.ints(a, p.position);
                f.ints(a + 12, p.velocity);
                for (k, v) in [p.life as u16, p.size, p.rotation].into_iter().enumerate() {
                    f.put(a + 24 + k as u32 * 2, 2, u32::from(v));
                }
                f.bytes(a + 30, [p.direction, p.expanding]);
            }
            f.ints(
                o + 0x978,
                [
                    l.smoke.timer,
                    l.smoke.side as i32,
                    l.smoke.force,
                    l.smoke.count as i32,
                ],
            );
            f.bytes(o + 0xdf1, l.smoke.damage);
            f.bytes(o + 0xdf3, l.smoke.sockets);
            f.ints(o + 0x988, l.smoke.damage_timers);
            f.bytes(
                o + 0xe17,
                [l.lights.brake_mode, l.lights.active[0], l.lights.active[1]],
            );
            f.ints(o + 0xe1a, l.lights.handles.map(|h| h.0 as i32));
            f.bytes(o + 0xc54, l.marks.active);
            f.ints(o + 0xc56, l.marks.endpoints.into_iter().flatten().flatten());
            f.ints(o + 0xc86, l.marks.movement);
            for (j, h) in l.marks.handles.iter().enumerate() {
                f.word(o + 0xaac + j as u32 * 6, h.0);
            }
            f.word(o + 0xc44, l.marks.count);
            f.word(o + 0xc4c, l.marks.head);
            f.word(o + 0xc50, l.marks.tail);
            let d = self.debris_vehicles[i];
            f.word(o + 0xc92, d.active);
            f.word(o + 0xc96, d.timer as u32);
            f.put(o + 0xdef, 1, u32::from(d.previous_plane));
            f.word(o + 0x118, self.accumulators[i] as u32);
            f.word(o + 0x1082, self.cooldowns[i]);
        }
        for (i, b) in self.debris.banks.iter().enumerate() {
            let a = 0x546460 + i as u32 * 264;
            for (j, p) in b.particles.iter().enumerate() {
                let a = a + j as u32 * 32;
                f.ints(a, p.velocity);
                f.ints(a + 12, p.position);
                f.bytes(a + 24, p.color);
                f.put(a + 27, 1, u32::from(p.active));
                f.word(a + 28, p.size);
            }
            f.word(a + 256, b.floor as u32);
            f.word(a + 260, b.owner.map_or(0, |e| e.0));
        }
        for (i, b) in self.pickup.banks.iter().enumerate() {
            let a = 0x58faa0 + i as u32 * 44;
            f.bytes(a, [b.active, b.projected, b.follow[0], b.follow[1]]);
            f.word(a + 4, pointer(b.target));
            f.word(a + 8, b.life as u32);
            f.word(a + 12, b.frame);
            f.ints(a + 16, b.origin);
            f.ints(a + 28, b.position);
            f.word(a + 40, b.speed as u32);
            f.word(0x5450c0 + i as u32 * 4, b.inner_radius as u32);
            f.word(0x5450c8 + i as u32 * 4, b.outer_radius as u32);
            f.word(0x5457d8 + i as u32 * 4, b.flare_radius as u32);
            for (j, r) in b.rays.iter().enumerate() {
                let a = 0x545148 + i as u32 * 840 + j as u32 * 84;
                f.put(a, 2, u32::from(r.angles[0]));
                f.put(a + 2, 2, u32::from(r.angles[1]));
                f.ints(a + 4, r.angular_velocity);
                f.word(a + 12, r.palette_index);
                f.word(a + 16, r.handle.0);
                f.quad(a + 20, r.quad);
            }
            for (j, r) in b.rings.iter().enumerate() {
                let a = 0x5457e0 + i as u32 * 680 + j as u32 * 68;
                f.quad(a, r.quad);
                f.word(a + 64, r.handle.0);
            }
        }
        f.word(0x54669c, self.temporary.available);
        for i in 0..4 {
            let a = 0x5461b0 + i as u32 * 136;
            f.word(a, self.temporary.timestamps[i]);
            for j in 0..3 {
                let a = a + 4 + j as u32 * 44;
                let r = self.temporary.rows[i][j];
                f.ints(a, r.first);
                f.ints(a + 12, r.last);
                f.bytes(a + 24, r.colors);
                f.put(a + 28, 2, u32::from(r.phases[0]));
                f.put(a + 30, 2, u32::from(r.phases[1]));
                f.ints(
                    a + 32,
                    self.temporary.lines[i][j * 3..j * 3 + 3]
                        .iter()
                        .map(|h| h.0 as i32),
                );
            }
        }
        for (i, b) in self.shield.banks.iter().enumerate() {
            let a = 0x58ee20 + i as u32 * 160;
            for (j, o) in b.orbits.iter().enumerate() {
                let a = a + j as u32 * 39;
                f.ints(
                    a,
                    [
                        o.pitch,
                        o.pitch_target,
                        o.yaw,
                        o.yaw_rate,
                        o.yaw_rate_target,
                        o.size,
                    ],
                );
                f.put(a + 24, 1, u32::from(o.growing));
                f.put(a + 25, 2, u32::from(o.spin));
                f.ints(a + 27, o.position);
            }
            f.word(a + 156, b.tail);
        }
        for (i, b) in self.impacts.batches.iter().enumerate() {
            let a = 0x58f400 + i as u32 * 404;
            for (j, p) in b.particles.iter().enumerate() {
                let a = a + j as u32 * 38;
                f.put(a, 2, u32::from(p.distance[0] as u16));
                f.put(a + 2, 2, u32::from(p.distance[1] as u16));
                f.ints(a + 4, p.speed);
                f.word(a + 12, p.pitch[0] as u32);
                f.word(a + 16, p.yaw[0] as u32);
                f.word(a + 20, p.pitch[1] as u32);
                f.word(a + 24, p.yaw[1] as u32);
                f.word(a + 28, p.line.0);
                f.bytes(a + 32, p.colors.into_iter().flatten());
            }
            f.ints(a + 380, [b.kind, b.corner as i32]);
            f.word(a + 388, pointer(b.owner));
            f.word(a + 392, u32::from(b.active));
            f.ints(a + 396, [b.age, b.frame as i32]);
        }
        f.word(0x545e1c, self.impacts.next);
        f.word(0x55b6ac, self.random);
        f.word(0x55ae20, self.pool.line_count);
        f.word(0x55ae1c, self.pool.quad_count);
        f.quad(0x54cc48, self.pool.exhausted_quad);
        for (i, q) in self.pool.quads.iter().enumerate() {
            f.quad(0x54cc88 + i as u32 * 64, *q);
        }
        f.bytes(0x54f488, self.pool.exhausted_colors.into_iter().flatten());
        for (i, l) in self
            .pool
            .lines
            .iter()
            .chain([&self.pool.overflow])
            .enumerate()
        {
            let a = 0x54f490 + i as u32 * 32;
            f.ints(a, l.points.into_iter().flatten());
            f.bytes(a + 24, l.colors.into_iter().flatten());
        }
        f
    }
}
fn fixture(case: usize) -> Fixture {
    let mut pool = Pool::default();
    let used = if case.is_multiple_of(4) { 100 } else { 36 };
    pool.line_count = used;
    for l in &mut pool.lines[..used as usize] {
        l.points = [[1, 2, 3]; 2];
        l.colors = [[10, 20, 30, 1]; 2];
    }
    pool.quad_count = 40;
    for q in &mut pool.quads[..40] {
        q.points = [[1, 2, 3]; 4];
    }
    let mut temporary = temporary::State::default();
    for (i, slot) in temporary.lines.iter_mut().enumerate() {
        for (j, h) in slot.iter_mut().enumerate() {
            *h = LineHandle((i * 9 + j) as u32);
        }
    }
    if case.is_multiple_of(13) {
        temporary.lines[0][8] = LineHandle(0x12345678);
    }
    let mut random = (case as u32).wrapping_mul(982451653);
    let mut shield = shield::State::default();
    shield.initialize(&mut random);
    let mut local: [Local; 2] = std::array::from_fn(|_| Local::default());
    for (i, l) in local.iter_mut().enumerate() {
        l.spray.timer = 39;
        l.smoke.timer = 69;
        l.smoke.damage = [7, 9];
        l.smoke.damage_timers = [299, 149];
        l.smoke.sockets = [case as u8 % 6, case as u8 / 6 % 6];
        if case.is_multiple_of(5) {
            l.spray.particles[0].life = 10;
            l.spray.count = 1;
            l.smoke.particles[0].life = 10;
            l.smoke.count = 1;
        }
        l.marks.count = if case.is_multiple_of(7) { 68 } else { 0 };
        l.marks.handles.fill(QuadHandle(39));
        l.marks.tail = i as u32;
    }
    let pickup = pickup::State {
        banks: std::array::from_fn(|i| pickup::Bank {
            active: 1,
            target: Some(EntityId(i as u32 + 1)),
            life: [65536, 192][case % 2],
            frame: u32::MAX,
            follow: [1, 0],
            position: [0; 3],
            speed: 100000,
            inner_radius: 25599,
            outer_radius: 25599,
            rays: std::array::from_fn(|j| pickup::Ray {
                handle: QuadHandle(j as u32),
                palette_index: j as u32,
                ..pickup::Ray::default()
            }),
            rings: std::array::from_fn(|j| pickup::Ring {
                handle: if case.is_multiple_of(11) && j == 5 {
                    QuadHandle(0x12345678)
                } else {
                    QuadHandle(j as u32 + 10)
                },
                ..pickup::Ring::default()
            }),
            ..pickup::Bank::default()
        }),
    };
    Fixture {
        local,
        debris_vehicles: [debris::Vehicle::default(); 2],
        accumulators: [i32::MAX, 0],
        cooldowns: [500; 2],
        debris: debris::State::default(),
        pickup,
        temporary,
        shield,
        impacts: impact_particles::State::default(),
        pool,
        random,
    }
}

struct Input {
    motion: motion::State,
    contact: contact_pipeline::Pipeline,
    powertrain: powertrain::State,
    pose: motion::PoseInput,
    frame: vehicle::Frame,
    settings: chain::Settings,
    timer: i16,
    slot: i16,
}
fn input(case: usize, step: usize, own: usize) -> Input {
    let mut motion = motion::State::default();
    motion.poses.base_position = [12345, -1000, -54321];
    motion.poses.physical_position = [12350, 100, -54325];
    motion.poses.reference = Matrix::from_angles([1234, 5678, 9012]);
    motion.poses.body = pose::Pose {
        position: motion.poses.base_position,
        matrix: motion.poses.reference,
    };
    motion.poses.turret = motion.poses.body;
    for p in &mut motion.poses.tracks {
        p.matrix = motion.poses.reference;
    }
    motion.velocity = [600001, -102401, 333333];
    motion.rates.lateral_velocity = if step.is_multiple_of(2) {
        -150000
    } else {
        150000
    };
    motion.rates.track_slip = 50001;
    motion.rates.penetrating_force = 5000000;
    motion.travel = [0; 4];
    for (i, p) in motion.contacts.iter_mut().enumerate() {
        p.world = [
            if i < 2 { -1000 } else { 1000 },
            0,
            if i % 2 == 0 { 3000 } else { -3000 },
        ];
    }
    let mut contact = contact_pipeline::Pipeline::default();
    contact.sampling.materials = if case.is_multiple_of(3) {
        [1, 1]
    } else {
        [4, 1]
    };
    contact.sampling.primary.has_extra = step.is_multiple_of(3);
    contact.sampling.primary.extra[3] = 1024;
    contact.sampling.effect.active = 1;
    for (i, p) in contact.sampling.primary.points.iter_mut().enumerate() {
        p.kind = 0;
        p.raw_height = 0;
        contact.response.points[i].corner = i as u32;
    }
    let pose = motion::PoseInput {
        external: pose::ExternalBasis::Body,
        timer: 0,
        ambient_wobble: false,
        session_active: false,
        ambient_phase: 0,
        recoil_mode: 0,
        turret_yaw: 0,
    };
    let frame = vehicle::Frame {
        dt: [1, 16, 33][(case + step) % 3],
        number: step as u32,
        mode: 0,
        input: drive::Input {
            throttle: if step.is_multiple_of(2) { -65536 } else { 0 },
            brake: i32::from(!step.is_multiple_of(2)),
            ..drive::Input::default()
        },
        drive: drive::Environment::default(),
    };
    Input {
        motion,
        contact,
        powertrain: powertrain::State {
            track_speed: 250000,
            local_velocity: [0, 0, 250000],
            ..powertrain::State::default()
        },
        pose,
        frame,
        settings: chain::Settings {
            special: case.is_multiple_of(2),
            lights: lights::Settings {
                active: true,
                visible: 1,
                queued: false,
                reverse_braking: false,
                material: 103,
            },
            light_offset: [1024, 0, -2048],
            camera: ([0; 3], [0, 0, 4096]),
        },
        timer: if case.is_multiple_of(3) { 0 } else { 5 },
        slot: own as i16,
    }
}
impl Input {
    fn fields(&self, own: usize) -> Fields {
        let mut f = Fields::default();
        let o = object(own);
        let m = &self.motion;
        let c = &self.contact.sampling;
        f.word(0x5904fc, self.frame.dt as u32);
        f.word(0x590564, self.frame.number);
        f.word(0x52ee9c, self.frame.mode as u32);
        f.word(0x53e414, u32::from(self.settings.special));
        f.put(o + 0xde5, 1, own as u32);
        f.put(o + 0xded, 1, 1);
        f.word(0x54b4b0, 1);
        f.word(0x5468a8, self.settings.lights.material);
        f.ints(o, m.poses.base_position);
        f.ints(o + 0x78, m.poses.physical_position);
        f.ints(o + 0x50, m.velocity);
        f.ints(o + 24, m.poses.reference.0);
        f.word(o + 20, o + 24);
        f.word(o + 0xc0, m.rates.lateral_velocity as u32);
        f.word(o + 0xa0, m.rates.track_slip as u32);
        f.word(o + 0xf0, m.rates.penetrating_force as u32);
        f.word(o + 0xa4, self.powertrain.track_speed as u32);
        f.word(o + 0xec, self.powertrain.local_velocity[2] as u32);
        f.ints(o + 0x716, c.materials);
        f.word(o + 0x94, self.frame.input.throttle as u32);
        f.word(o + 0x98, self.frame.input.brake as u32);
        f.ints(o + 0x656, self.settings.light_offset);
        for (i, p) in m.contacts.iter().enumerate() {
            let a = o + 0x11c + i as u32 * 70;
            f.ints(a + 12, p.world);
            f.put(a + 24, 2, c.primary.points[i].kind as u32);
            f.word(a + 36, c.primary.points[i].raw_height as u32);
            f.word(a + 40, i as u32);
            f.word(a + 44, m.travel[i] as u32);
        }
        for (i, p) in m.poses.tracks.iter().enumerate() {
            f.ints(o + 0x3ea + i as u32 * 104, p.matrix.0);
        }
        for (a, p) in [(o + 0x2a6, m.poses.body), (o + 0x30e, m.poses.turret)] {
            f.ints(a, p.position);
            f.ints(a + 12, p.matrix.0);
        }
        f.put(o + 0xdee, 1, u32::from(c.primary.has_extra));
        f.ints(o + 0x26e, c.primary.extra);
        f.put(o + 0xe08, 1, c.effect.active as u32);
        f.put(o + 0xdd0, 2, self.timer as u16 as u32);
        f.put(o + 0xdce, 2, self.slot as u16 as u32);
        f.ints(0x74aee0, self.settings.camera.0);
        f.ints(0x74af18, self.settings.camera.1);
        f
    }
}
impl Fixture {
    fn run(&mut self, own: usize, inactive: bool, input: &mut Input) -> Result<(), i32> {
        let mut collision = vehicle::CollisionState {
            impact_accumulator: self.accumulators[own],
            ..vehicle::CollisionState::default()
        };
        let contacts = input.motion.contacts.map(|p| p.world);
        let basis = input.motion.poses.reference;
        let view = vehicle::AfterPose {
            entity: EntityId(own as u32 + 1),
            class: ClassId(6),
            player: own != 0,
            motion: &mut input.motion,
            collision: &mut collision,
            contact: &input.contact,
            powertrain: &input.powertrain,
            pose_input: &input.pose,
            frame: &input.frame,
        };
        let mut impact_vehicles: Vec<_> = (0..2)
            .map(|i| impact_particles::Vehicle {
                entity: EntityId(i as u32 + 1),
                contacts,
                forward_velocity: input.powertrain.local_velocity[2],
                external_basis: Some(basis),
                cooldown: self.cooldowns[i],
            })
            .collect();
        let mut owners: Vec<_> = self
            .debris_vehicles
            .iter_mut()
            .enumerate()
            .map(|(i, state)| debris::Owner {
                entity: EntityId(i as u32 + 1),
                state,
            })
            .collect();
        let mut sound = None;
        let mut damage = [];
        let mut corners = [];
        let mut effects = collision_effects::Bank {
            particles: &mut self.impacts,
            lines: &mut self.pool,
            vehicles: &mut impact_vehicles,
            damage: &mut damage,
            corners: &mut corners,
            frame: input.frame.number,
            tuning: impact_particles::Tuning::default(),
        };
        let l = &mut self.local[own];
        let local = chain::Local {
            spray: &mut l.spray,
            smoke: &mut l.smoke,
            marks: &mut l.marks,
            lights: &mut l.lights,
            temporary_timer: input.timer,
            temporary_slot: input.slot,
        };
        let world = chain::World {
            own,
            debris: &mut self.debris,
            debris_owners: &mut owners,
            pickup: &mut self.pickup,
            temporary: &mut self.temporary,
            shield: &mut self.shield,
            debris_sound: &mut sound,
        };
        let styles = spray::Tuning::environment(
            spray::Materials {
                filth: 101,
                crap: 102,
                splash: 103,
            },
            false,
            true,
        );
        let smoke = smoke::Tuning::default();
        let pickup = pickup::Tuning::new(0);
        let tuning = chain::Tuning {
            spray: &styles,
            smoke: &smoke,
            pickup: &pickup,
        };
        let run = if inactive {
            chain::inactive
        } else {
            chain::active
        };
        let result = run(
            view,
            local,
            world,
            &mut effects,
            &mut self.random,
            input.settings,
            tuning,
        );
        self.accumulators[own] = collision.impact_accumulator;
        self.cooldowns = std::array::from_fn(|i| impact_vehicles[i].cooldown);
        result
    }
}
#[test]
#[ignore = "requires native full scene oracle; set TANKRACER_VEHICLE_SCENE_REFERENCE"]
fn active_and_inactive_chains_match_c_with_all_shared_effects() {
    const CASES: usize = 40;
    let mut wire = vec![CASES as u32];
    let mut expected = Vec::new();
    let mut bounds = Vec::new();
    let mut errors = 0;
    let mut draws = 0;
    let mut successes = [0; 2];
    let mut changes = [0; 9];
    for case in 0..CASES {
        let mut f = fixture(case);
        let layout = f.fields();
        layout.wire(&mut wire);
        wire.push(8);
        expected.push(8);
        for step in 0..8 {
            let own = step % 2;
            let inactive = (case + step).is_multiple_of(4);
            let mut input = input(case, step, own);
            wire.extend([own as u32, u32::from(inactive)]);
            input.fields(own).wire(&mut wire);
            let mut seed = f.random;
            let before = (
                f.local[own].clone(),
                f.pickup.clone(),
                f.impacts.clone(),
                f.debris.clone(),
                f.temporary.clone(),
                f.shield.clone(),
            );
            let result = f.run(own, inactive, &mut input);
            errors += usize::from(result.is_err());
            successes[usize::from(inactive)] += usize::from(result.is_ok());
            let changed = [
                before.0.spray != f.local[own].spray,
                before.0.marks != f.local[own].marks,
                before.1 != f.pickup,
                before.2 != f.impacts,
                before.0.smoke != f.local[own].smoke,
                before.3 != f.debris,
                before.0.lights != f.local[own].lights,
                before.4 != f.temporary,
                before.5 != f.shield,
            ];
            for (count, change) in changes.iter_mut().zip(changed) {
                *count += usize::from(change);
            }
            let count = (0..=512)
                .find(|_| {
                    if seed == f.random {
                        true
                    } else {
                        random::next(&mut seed);
                        false
                    }
                })
                .expect("bounded draws");
            draws += count;
            let fields = f.fields();
            assert_eq!(fields.0.len(), layout.0.len());
            let start = expected.len();
            expected.extend([result.err().unwrap_or(0) as u32, count]);
            expected.extend(fields.values());
            bounds.push((
                case,
                step,
                start,
                expected.len(),
                layout.0.iter().map(|v| v.0).collect::<Vec<_>>(),
            ));
        }
    }
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests");
    fs::create_dir_all(&root).unwrap();
    let path = root.join("vehicle-scene-input.bin");
    fs::write(
        &path,
        wire.iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output =
        Command::new(std::env::var_os("TANKRACER_VEHICLE_SCENE_REFERENCE").expect("set oracle"))
            .stdin(Stdio::from(fs::File::open(path).unwrap()))
            .output()
            .unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"VA01");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(actual[0], CASES as u32);
    assert_eq!(actual.len() - 1, expected.len());
    for (word, (a, e)) in actual[1..].iter().zip(&expected).enumerate() {
        if a != e {
            let (case, step, start, _, addresses) = bounds
                .iter()
                .find(|(_, _, s, end, _)| word >= *s && word < *end)
                .unwrap();
            let field = word - start;
            panic!(
                "case={case} step={step} field={field} address={:x} C={a:08x} Rust={e:08x}",
                if field < 2 { 0 } else { addresses[field - 2] }
            );
        }
    }
    assert!(errors > 0 && draws > 0);
    assert!(successes.into_iter().all(|n| n > 0));
    assert!(
        changes.into_iter().all(|n| n > 0),
        "every component must change: {changes:?}"
    );
    println!(
        "{CASES} scenarios / {} full chains / {} words; errors={errors}, draws={draws}, active/inactive successes={successes:?}, component changes={changes:?}",
        CASES * 8,
        expected.len()
    );
}
