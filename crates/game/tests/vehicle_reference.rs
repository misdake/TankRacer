//! Compare the connected Rust physics phase with actual C vehicle-step writes.
use std::{fs, path::PathBuf, process::Command};
use tankracer_assets::{
    archive::Archive,
    config::{Damping, Drive, Gain, Surfaces},
    mesh::{Geometry, Triangle},
    terrain::TerrainData,
};
use tankracer_game::{
    attitude, body, collision_effects,
    contact::{self, Coefficient},
    contact_pipeline, damage,
    dispatch::{ClassId, MethodId},
    drive,
    effect_pool::{Line, LineHandle, Pool, Quad},
    impact_particles::{self, Batch, Particle},
    math::Matrix,
    object_query::{Bounds, Collider, EntityId, Tank, TankId},
    obstacle_collision as obstacle, pose, powertrain, suspension, tank_collision, vehicle,
    velocity,
};
#[path = "common/motion.rs"]
mod common;
use common::{Reader, words as motion_words};

fn gain(v: u32) -> Gain {
    Gain {
        multiplier: v as u16,
        shift: (v >> 16) as u16,
    }
}
fn damping(v: u32) -> Damping {
    Damping {
        step: v as i16,
        fraction: (v >> 16) as u8,
        shift: (v >> 24) as u8,
    }
}
fn append(out: &mut Vec<u32>, v: impl IntoIterator<Item = i32>) {
    out.extend(v.into_iter().map(|v| v as u32));
}
fn geometry(r: &mut Reader) -> Geometry {
    let nv = r.word() as usize;
    let nf = r.word() as usize;
    let vertices = r.vertices(nv);
    let triangles = (0..nf)
        .map(|_| {
            let bytes: Vec<_> = (0..6).flat_map(|_| r.word().to_le_bytes()).collect();
            Triangle {
                source_offset: 0,
                normal: std::array::from_fn(|i| {
                    i16::from_le_bytes(bytes[i * 2..i * 2 + 2].try_into().unwrap())
                }),
                color: u16::from_le_bytes(bytes[6..8].try_into().unwrap()),
                indices: bytes[8..11].try_into().unwrap(),
                sides: bytes[11],
                uv: std::array::from_fn(|i| bytes[12 + i * 2..14 + i * 2].try_into().unwrap()),
                flags: u16::from_le_bytes(bytes[18..20].try_into().unwrap()),
                material: None,
                material_source_offset: u32::from_le_bytes(bytes[20..24].try_into().unwrap()),
                billboard: None,
            }
        })
        .collect();
    Geometry {
        vertices,
        triangles,
    }
}
fn geometry_words(out: &mut Vec<u32>, g: &Geometry) {
    out.extend([g.vertices.len() as u32, g.triangles.len() as u32]);
    for v in &g.vertices {
        append(out, v.map(i32::from));
    }
    for t in &g.triangles {
        let mut bytes = [0; 24];
        for (i, v) in t.normal.iter().enumerate() {
            bytes[i * 2..i * 2 + 2].copy_from_slice(&v.to_le_bytes());
        }
        bytes[6..8].copy_from_slice(&t.color.to_le_bytes());
        bytes[8..11].copy_from_slice(&t.indices);
        bytes[11] = t.sides;
        for (i, uv) in t.uv.iter().enumerate() {
            bytes[12 + i * 2..14 + i * 2].copy_from_slice(uv);
        }
        bytes[18..20].copy_from_slice(&t.flags.to_le_bytes());
        bytes[20..24].copy_from_slice(&t.material_source_offset.to_le_bytes());
        out.extend(
            bytes
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap())),
        );
    }
}
struct Model {
    body: Geometry,
    gun: Vec<[i16; 3]>,
    has_gun: bool,
    gun_indices: [u8; 4],
    gun_z: [i16; 4],
    body_indices: [u8; 4],
}
struct Snapshot {
    states: Vec<vehicle::State>,
    models: Vec<Model>,
    damage: Vec<damage::State>,
    impacts: Vec<impact_particles::Vehicle>,
    tanks: Vec<Tank>,
    random: u32,
    slot: u32,
    player_effects: [u32; 2],
    peer: Option<tank_collision::PeerSnapshot>,
    pool: Pool,
    particles: impact_particles::State,
}
fn read_line(r: &mut Reader) -> Line {
    Line {
        points: [r.vector(), r.vector()],
        colors: [r.word().to_le_bytes(), r.word().to_le_bytes()],
    }
}
impl Snapshot {
    fn read(r: &mut Reader, n: usize) -> Self {
        let mut states = Vec::new();
        let mut models = Vec::new();
        let mut damage = Vec::new();
        let mut impacts = Vec::new();
        let mut tanks = Vec::new();
        for _ in 0..n {
            let entity = EntityId(r.word());
            let player = r.word() != 0;
            let motion = r.state();
            let powertrain = powertrain::State {
                local_velocity: r.vector(),
                track_speed: r.signed(),
                boost_target: r.signed(),
                engine_display: r.signed(),
                was_active: r.word() as u8,
                gear: r.signed() as i8,
                load: r.signed(),
            };
            let pose = pose::Config {
                track_offsets: [r.vector(), r.vector()],
                turret_offset: r.vector(),
                gun_offset: r.vector(),
                attachment_offsets: [r.vector(), r.vector()],
                bottom_offset: r.vector(),
                track_length: r.signed(),
                travel_gain: gain(r.word()),
                gun_pivot: r.vector(),
            };
            let timer = r.word() as u16;
            let ambient_wobble = r.word() != 0;
            let session_active = r.word() != 0;
            let ambient_phase = r.word();
            let recoil_mode = r.signed();
            let turret_yaw = r.signed() as i16;
            let tag = r.word();
            let matrix = Matrix(r.vector());
            let external = match tag {
                0 => pose::ExternalBasis::None,
                1 => pose::ExternalBasis::Body,
                2 => pose::ExternalBasis::Custom(matrix),
                _ => panic!("basis"),
            };
            let pose_input = tankracer_game::motion::PoseInput {
                timer,
                ambient_wobble,
                session_active,
                ambient_phase,
                recoil_mode,
                turret_yaw,
                external,
            };
            let yaw_lock = r.signed();
            let collision = vehicle::CollisionState {
                disabled: r.word() != 0,
                timer: r.signed(),
                impact_accumulator: r.signed(),
                collision_size: r.word(),
                material: r.signed(),
                deformable: match r.word() {
                    0 => None,
                    v => Some(EntityId(v)),
                },
                obstacle_impulse: r.signed(),
                tank_delta: r.vector(),
                ground_delta: r.vector(),
            };
            let mut contact = contact_pipeline::Pipeline::default();
            for i in 0..4 {
                let p = &mut contact.sampling.primary.points[i];
                p.local = r.vector();
                p.kind = r.signed();
                p.previous_gap = r.signed();
                p.raw_height = r.signed();
                p.flags = r.word();
                p.normal = r.vector();
                p.world = motion.contacts[i].world;
                p.gap = motion.contacts[i].gap;
                p.travel = motion.travel[i];
                contact.response.points[i] = suspension::Point {
                    kind: p.kind,
                    gap: p.gap,
                    corner: r.word(),
                    travel: p.travel,
                    rate: r.signed(),
                    output: r.signed(),
                };
            }
            let p = &mut contact.sampling.primary;
            p.load = powertrain.load;
            p.airborne_count = r.signed();
            p.special_count = r.signed();
            p.flagged_count = r.signed();
            p.has_extra = r.word() != 0;
            p.extra = r.vector();
            let e = &mut contact.sampling.effect;
            e.timer = r.signed();
            e.restore = r.signed();
            e.active = r.signed();
            e.phase = r.signed();
            e.impulse = [
                motion.articulation.recoil_rate,
                motion.articulation.pitch_rate,
            ];
            e.pose = pose.turret_offset[2];
            for a in &mut contact.sampling.auxiliary {
                a.world = r.vector();
                a.kind = r.signed();
                a.cached = r.signed();
            }
            contact.sampling.materials = r.vector();
            contact.response.tilt = r.vector();
            contact.response.vertical_delta = r.signed();
            contact.response.timer = r.signed();
            contact.response.angular = [motion.angular[0], motion.angular[2]];
            contact.response.world_vy = motion.velocity[1];
            contact.response.position_y = motion.poses.physical_position[1];
            contact.response.penetrating_input_sum = motion.rates.penetrating_force;
            contact.response.travel_sum = motion.rates.travel_sum;
            contact.pivot = motion.pivot;
            let d = damage::State {
                body: r.vector().map(|v| v as u8),
                turret: r.vector().map(|v| v as u8),
                tracks: r.vector().map(|v| v as u8),
            };
            let cooldown = r.word();
            let body_indices = r.vector().map(|v| v as u8);
            let body = geometry(r);
            let has_gun = r.word() != 0;
            let gun_indices = r.vector().map(|v| v as u8);
            let gun_z = r.vector().map(|v| v as i16);
            let nv = r.word() as usize;
            let gun = r.vertices(nv);
            let radius = r.word() as u16;
            let bounds = Bounds {
                min: r.vector(),
                max: r.vector(),
            };
            let overlap_bounds = Bounds {
                min: r.vector(),
                max: r.vector(),
            };
            let slot = r.word() as u8;
            let active = r.word() != 0;
            let basis = match external {
                pose::ExternalBasis::None => None,
                pose::ExternalBasis::Body => Some(motion.poses.reference),
                pose::ExternalBasis::Custom(m) => Some(m),
            };
            impacts.push(impact_particles::Vehicle {
                entity,
                contacts: motion.contacts.map(|p| p.world),
                forward_velocity: powertrain.local_velocity[2],
                external_basis: basis,
                cooldown,
            });
            tanks.push(Tank {
                collider: Collider {
                    entity,
                    pose: pose::Pose {
                        position: motion.poses.base_position,
                        matrix: motion.poses.reference,
                    },
                    external_basis: basis,
                    radius,
                    bounds,
                },
                slot,
                active,
                disabled: collision.disabled,
                ready: motion.ready,
                overlap_bounds,
                contacts: motion.contacts.map(|p| p.world),
            });
            states.push(vehicle::State {
                entity,
                class: ClassId(0),
                player,
                motion,
                powertrain,
                contact,
                pose,
                pose_input,
                yaw_lock,
                collision,
            });
            damage.push(d);
            models.push(Model {
                body,
                gun,
                has_gun,
                gun_indices,
                gun_z,
                body_indices,
            });
        }
        let random = r.word();
        let slot = r.word();
        let player_effects = [r.word(), r.word()];
        let peer = r.word();
        let velocity_xz = r.vector();
        let peer = if peer == 0 {
            None
        } else {
            Some(tank_collision::PeerSnapshot {
                tank: TankId(peer as usize - 1),
                velocity_xz,
            })
        };
        let next = r.word();
        let line_count = r.word();
        let quad_count = r.word();
        let mut pool = Pool {
            line_count,
            quad_count,
            ..Pool::default()
        };
        for l in &mut pool.lines {
            *l = read_line(r);
        }
        let points = std::array::from_fn(|_| r.vector());
        let material = r.word();
        let mut style = [0; 12];
        for b in style.chunks_exact_mut(4) {
            b.copy_from_slice(&r.word().to_le_bytes());
        }
        pool.quads[159] = Quad {
            points,
            material,
            style,
        };
        pool.exhausted_colors = [r.word().to_le_bytes(), r.word().to_le_bytes()];
        pool.overflow = read_line(r);
        let batches = std::array::from_fn(|_| {
            let particles = std::array::from_fn(|_| Particle {
                distance: r.vector().map(|v| v as i16),
                speed: r.vector(),
                pitch: r.vector(),
                yaw: r.vector(),
                line: LineHandle(r.word()),
                colors: [r.vector().map(|v| v as u8), r.vector().map(|v| v as u8)],
            });
            Batch {
                particles,
                kind: r.signed(),
                corner: r.word(),
                owner: match r.word() {
                    0 => None,
                    v => Some(EntityId(v)),
                },
                active: r.word() != 0,
                age: r.signed(),
                frame: r.word(),
            }
        });
        Self {
            states,
            models,
            damage,
            impacts,
            tanks,
            random,
            slot,
            player_effects,
            peer,
            pool,
            particles: impact_particles::State { batches, next },
        }
    }
    fn words(&self) -> Vec<u32> {
        let mut out = Vec::new();
        for i in 0..self.states.len() {
            let s = &self.states[i];
            let p = &s.powertrain;
            let m = &self.models[i];
            let c = &s.contact;
            let pose = &s.pose;
            let input = &s.pose_input;
            out.extend([s.entity.0, u32::from(s.player)]);
            out.extend(motion_words(&s.motion));
            append(&mut out, p.local_velocity);
            append(
                &mut out,
                [
                    p.track_speed,
                    p.boost_target,
                    p.engine_display,
                    i32::from(p.was_active),
                    i32::from(p.gear),
                    p.load,
                ],
            );
            for v in pose
                .track_offsets
                .into_iter()
                .chain([pose.turret_offset, pose.gun_offset])
                .chain(pose.attachment_offsets)
                .chain([pose.bottom_offset])
            {
                append(&mut out, v);
            }
            out.extend([pose.track_length as u32, pose.travel_gain.packed()]);
            append(&mut out, pose.gun_pivot);
            out.extend([
                u32::from(input.timer),
                u32::from(input.ambient_wobble),
                u32::from(input.session_active),
                input.ambient_phase,
                input.recoil_mode as u32,
                input.turret_yaw as i32 as u32,
            ]);
            let (tag, basis) = match input.external {
                pose::ExternalBasis::None => (0, Matrix::IDENTITY),
                pose::ExternalBasis::Body => (1, Matrix::IDENTITY),
                pose::ExternalBasis::Custom(m) => (2, m),
            };
            out.push(tag);
            append(&mut out, basis.0);
            out.push(s.yaw_lock as u32);
            let a = &s.collision;
            out.extend([
                u32::from(a.disabled),
                a.timer as u32,
                a.impact_accumulator as u32,
                a.collision_size,
                a.material as u32,
                a.deformable.map_or(0, |e| e.0),
                a.obstacle_impulse as u32,
            ]);
            append(&mut out, a.tank_delta);
            append(&mut out, a.ground_delta);
            for (p, q) in c.sampling.primary.points.iter().zip(c.response.points) {
                append(&mut out, p.local);
                append(&mut out, [p.kind, p.previous_gap, p.raw_height]);
                out.push(p.flags);
                append(&mut out, p.normal);
                out.push(q.corner);
                append(&mut out, [q.rate, q.output]);
            }
            let p = &c.sampling.primary;
            append(
                &mut out,
                [
                    p.airborne_count,
                    p.special_count,
                    p.flagged_count,
                    i32::from(p.has_extra),
                ],
            );
            append(&mut out, p.extra);
            let e = &c.sampling.effect;
            append(&mut out, [e.timer, e.restore, e.active, e.phase]);
            for a in &c.sampling.auxiliary {
                append(&mut out, a.world);
                append(&mut out, [a.kind, a.cached]);
            }
            append(&mut out, c.sampling.materials);
            append(&mut out, c.response.tilt);
            append(&mut out, [c.response.vertical_delta, c.response.timer]);
            let d = &self.damage[i];
            out.extend(
                d.body
                    .into_iter()
                    .chain(d.turret)
                    .chain(d.tracks)
                    .map(u32::from),
            );
            out.push(self.impacts[i].cooldown);
            out.extend(m.body_indices.map(u32::from));
            geometry_words(&mut out, &m.body);
            out.push(u32::from(m.has_gun));
            out.extend(m.gun_indices.map(u32::from));
            append(&mut out, m.gun_z.map(i32::from));
            out.push(m.gun.len() as u32);
            for v in &m.gun {
                append(&mut out, v.map(i32::from));
            }
            let t = &self.tanks[i];
            out.push(u32::from(t.collider.radius));
            append(&mut out, t.collider.bounds.min);
            append(&mut out, t.collider.bounds.max);
            append(&mut out, t.overlap_bounds.min);
            append(&mut out, t.overlap_bounds.max);
            out.extend([u32::from(t.slot), u32::from(t.active)]);
        }
        out.extend([
            self.random,
            self.slot,
            self.player_effects[0],
            self.player_effects[1],
        ]);
        out.push(self.peer.map_or(0, |p| p.tank.0 as u32 + 1));
        append(&mut out, self.peer.map_or([0; 2], |p| p.velocity_xz));
        out.extend([
            self.particles.next,
            self.pool.line_count,
            self.pool.quad_count,
        ]);
        let line = |out: &mut Vec<u32>, l: Line| {
            for p in l.points {
                append(out, p);
            }
            out.extend(l.colors.map(u32::from_le_bytes));
        };
        for l in self.pool.lines {
            line(&mut out, l);
        }
        let q = self.pool.quads[159];
        for p in q.points {
            append(&mut out, p);
        }
        out.push(q.material);
        out.extend(
            q.style
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap())),
        );
        out.extend(self.pool.exhausted_colors.map(u32::from_le_bytes));
        line(&mut out, self.pool.overflow);
        for b in self.particles.batches {
            for p in b.particles {
                append(&mut out, p.distance.map(i32::from));
                append(&mut out, p.speed);
                append(&mut out, p.pitch);
                append(&mut out, p.yaw);
                out.push(p.line.0);
                out.extend(p.colors.into_iter().flatten().map(u32::from));
            }
            out.extend([
                b.kind as u32,
                b.corner,
                b.owner.map_or(0, |e| e.0),
                u32::from(b.active),
                b.age as u32,
                b.frame,
            ]);
        }
        out
    }
}
struct Scene {
    own: TankId,
    fail: bool,
    calls: u32,
    dt: i32,
}
impl collision_effects::ObjectServices for Scene {
    fn method(
        &mut self,
        _: MethodId,
        _: obstacle::Invocation<'_>,
        _: &mut collision_effects::Bank<'_, '_>,
    ) -> Result<(), i32> {
        panic!("fixture object lists are empty")
    }
    fn feedback(&mut self, _: i32, _: &mut obstacle::State, _: &mut u32) -> Result<(), i32> {
        panic!("fixture has no deformable")
    }
}
impl vehicle::Services for Scene {
    fn contact_feedback(&mut self, _: &mut vehicle::State, _: &mut u32) -> Result<(), i32> {
        Ok(())
    }
    fn after_pose(
        &mut self,
        _: vehicle::AfterPose<'_>,
        bank: &mut collision_effects::Bank<'_, '_>,
        random: &mut u32,
    ) -> Result<(), i32> {
        self.calls += 1;
        bank.particles
            .tick(
                &mut bank.vehicles[self.own.0],
                self.dt,
                &mut impact_particles::Context {
                    frame: bank.frame,
                    random,
                    pool: bank.lines,
                    tuning: bank.tuning,
                },
            )
            .map_err(impact_particles::Error::status)?;
        if self.fail { Err(9) } else { Ok(()) }
    }
}
struct Settings {
    frame: vehicle::Frame,
    drive: Drive,
    surfaces: Surfaces,
    tuning: drive::Tuning,
    axis: [i32; 3],
    contact: contact::Config,
    layout: [i32; 5],
    noise: [u16; 16],
    sampling: contact_pipeline::Environment,
    suspension: suspension::Environment,
    attitude: attitude::Config,
    body: body::Tuning,
    collision: obstacle::Tuning,
    grid_origin: [i32; 2],
    grid_shifts: [u32; 2],
}
impl Settings {
    fn read(r: &mut Reader) -> Self {
        let dt = r.signed();
        let number = r.word();
        let mode = r.signed();
        let input = drive::Input {
            throttle: r.signed(),
            brake: r.signed(),
            steering: r.signed(),
            action_flags: r.word(),
            alternate: r.word() != 0,
            alternate_lateral: r.signed() as i16,
            alternate_rotation: r.signed() as i16,
        };
        let environment = drive::Environment {
            active: r.word() != 0,
            launch_eligible: r.word() != 0,
            boost_charge: r.signed() as i16,
            boost_power: r.signed(),
            extra_drive: r.signed(),
            ..drive::Environment::default()
        };
        let frame = vehicle::Frame {
            dt,
            number,
            mode,
            input,
            drive: environment,
        };
        let drive = Drive {
            max_rpm: r.signed(),
            max_power: r.signed(),
            max_turn: r.signed(),
            initial_power: r.signed(),
            power_mul: r.signed(),
            max_brakes: r.signed(),
            turn_fraction: gain(r.word()),
            hill_roll: gain(r.word()),
            understeer_fraction: gain(r.word()),
            rearweight_fraction: gain(r.word()),
            rearweight_accel_fraction: gain(r.word()),
            traction_limit: r.signed(),
            catchup_traction_limit: r.signed(),
            sidegrip_fraction: gain(r.word()),
            forward_drag: damping(r.word()),
            track_drag: damping(r.word()),
            track_air_drag: damping(r.word()),
            track_spin_drag: damping(r.word()),
            gravity_drag: damping(r.word()),
            rotational_drag: damping(r.word()),
            air_rotational_drag: damping(r.word()),
            sideways_drag: damping(r.word()),
            force_drag: damping(r.word()),
            turbo_amount: r.signed(),
            turbo_power_mul: r.signed(),
            ..Drive::default()
        };
        let mut surfaces = Surfaces::default();
        for s in &mut surfaces.entries {
            s.drag = gain(r.word());
            s.grip = gain(r.word());
            s.slide = std::array::from_fn(|_| gain(r.word()));
            s.bump = gain(r.word());
        }
        let tuning = drive::Tuning {
            gear_limits: r.vector(),
            gear_offsets: r.vector(),
            gear_gain: std::array::from_fn(|_| gain(r.word())),
            engine_damping: damping(r.word()),
            boost_approach: damping(r.word()),
            boost_decay: damping(r.word()),
        };
        let axis = r.vector();
        let contact = contact::Config {
            lower: r.signed(),
            upper: r.signed(),
            force_positive: Coefficient::from_packed(r.word()),
            force_negative: Coefficient::from_packed(r.word()),
            output_gain: Coefficient::from_packed(r.word()),
            tilt_gain: Coefficient::from_packed(r.word()),
            shape_shift: r.word(),
            restore_positive: Coefficient::from_packed(r.word()),
            restore_negative: Coefficient::from_packed(r.word()),
            damping_shift: (r.word() >> 24) as u8,
            travel_dead_step: r.signed() as i16,
        };
        let layout = r.vector();
        let noise = r.vector().map(|v| v as u16);
        let force_surface = r.word() != 0;
        let effect_gain = r.coefficient();
        let base = r.signed();
        let floor = r.signed();
        let load_gain = Coefficient::from_packed(r.word());
        let auxiliary_local = [r.vector(), r.vector()];
        let sampling = contact_pipeline::Environment {
            tracks: [tankracer_game::sampling::Transform {
                basis: Matrix::IDENTITY.0,
                origin: [0; 3],
            }; 2],
            auxiliary_local,
            force_surface,
            effect_gain,
            load: contact_pipeline::LoadConfig {
                base,
                floor,
                gain: load_gain,
            },
            dt,
            frame: number,
            player: 1,
        };
        let suspension = suspension::Environment {
            dt,
            environment_flag: r.signed(),
            session_active: r.signed(),
            mode: r.signed(),
            region_min: r.vector(),
            region_max: r.vector(),
            player_multiplier: r.word() as u16,
            player_shift: r.word(),
            other_multiplier: r.word() as u16,
            other_shift: r.word(),
            penetration_shift: r.word(),
            ..suspension::Environment::default()
        };
        let attitude = attitude::Config {
            longitudinal: r.coefficient(),
            lateral: r.coefficient(),
        };
        let body = body::Tuning {
            recoil_decay: r.word() as u8,
            recoil_step: r.signed() as i16,
            pitch_damping: damping(r.word()),
            lean_decay: r.word() as u8,
        };
        let collision = obstacle::Tuning {
            local_z: Coefficient::from_packed(r.word()),
            final_scale: Coefficient::from_packed(r.word()),
        };
        let grid_origin = r.vector();
        let grid_shifts = r.vector().map(|v| v as u32);
        Self {
            frame,
            drive,
            surfaces,
            tuning,
            axis,
            contact,
            layout,
            noise,
            sampling,
            suspension,
            attitude,
            body,
            collision,
            grid_origin,
            grid_shifts,
        }
    }
}

#[test]
#[ignore = "requires full C vehicle oracle; set TANKRACER_VEHICLE_REFERENCE"]
fn connected_physics_real_particles_damage_and_peer_writes_match_c() {
    let assets = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let executable =
        std::env::var_os("TANKRACER_VEHICLE_REFERENCE").expect("set TANKRACER_VEHICLE_REFERENCE");
    let maps = [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ];
    let mut records = 0;
    let mut peer_hits = 0;
    let mut wears = 0;
    let mut words = 0;
    let mut tank_counts = std::collections::BTreeSet::new();
    for (map, car) in (1..=15).map(|v| (0, v)).chain((1..8).map(|m| (m, 1))) {
        let bytes = fs::read(assets.join("LEVELS").join(maps[map]).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let file = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .unwrap();
        let terrain = TerrainData::decode_bsm(archive.get(file.name).unwrap()).unwrap();
        let output = Command::new(&executable)
            .arg(&assets)
            .arg(map.to_string())
            .arg(car.to_string())
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "map={map} car={car} {:?}: {}",
            output.status.code(),
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], b"VC01");
        let mut r = Reader {
            words: output.stdout[4..]
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
                .collect(),
            at: 0,
        };
        while r.at < r.words.len() {
            let variant = r.word();
            let step = r.word();
            let cfg = Settings::read(&mut r);
            let n = r.word() as usize;
            tank_counts.insert(n);
            let own = TankId(r.word() as usize);
            let slots = std::array::from_fn(|_| match r.word() {
                0 => None,
                v => Some(TankId(v as usize - 1)),
            });
            let mut before = Snapshot::read(&mut r, n);
            let expected_status = r.signed();
            let expected = Snapshot::read(&mut r, n);
            let expected_calls = r.word();
            let initial_velocities: Vec<_> =
                before.states.iter().map(|s| s.motion.velocity).collect();
            let bumps = cfg.surfaces.entries.map(|s| s.bump.into());
            let terrain_world = contact_pipeline::World {
                terrain: &terrain,
                surface_bumps: &bumps,
                noise: cfg.noise,
            };
            let mut objects = obstacle::World {
                colliders: Vec::new(),
                objects: Vec::new(),
                classes: Vec::new(),
                grid: obstacle::Grid {
                    origin: cfg.grid_origin,
                    shifts: cfg.grid_shifts,
                    cells: std::array::from_fn(|_| Vec::new()),
                },
                deformables: Vec::new(),
            };
            let mut corners: Vec<_> = before
                .states
                .iter()
                .map(|s| s.contact.response.points.map(|p| p.corner))
                .collect();
            let mut gun = None;
            let mut targets = Vec::with_capacity(n);
            for (i, (state, model)) in before
                .damage
                .iter_mut()
                .zip(before.models.iter_mut())
                .enumerate()
            {
                if i == own.0 && model.has_gun {
                    gun = Some(body::GunMesh {
                        vertices: &mut model.gun,
                        indices: model.gun_indices,
                        original_z: model.gun_z,
                    });
                }
                targets.push(damage::Target {
                    state,
                    player: before.states[i].player,
                    body_pose: before.states[i].motion.poses.body,
                    body_vertices: model.body_indices,
                    body_mesh: before.states[i].player.then_some(&mut model.body),
                });
            }
            let mut shared = obstacle::Shared {
                random: before.random,
                current_slot: before.slot,
            };
            let resources = vehicle::Resources {
                gun,
                deformable: None,
            };
            let mut scene = Scene {
                own,
                fail: variant == 7,
                calls: 0,
                dt: cfg.frame.dt,
            };
            let result = {
                let bank = collision_effects::Bank {
                    particles: &mut before.particles,
                    lines: &mut before.pool,
                    vehicles: &mut before.impacts,
                    damage: &mut targets,
                    corners: &mut corners,
                    frame: cfg.frame.number,
                    tuning: impact_particles::Tuning::default(),
                };
                let mut world = vehicle::World {
                    terrain: &terrain_world,
                    objects: &mut objects,
                    tanks: &mut before.tanks,
                    slots: &slots,
                    peer: &mut before.peer,
                    shared: &mut shared,
                    effects: bank,
                    player_effects: &mut before.player_effects,
                };
                let config = vehicle::Config {
                    contact: contact_pipeline::Config {
                        sampling: &cfg.sampling,
                        suspension: &cfg.suspension,
                        contact: &cfg.contact,
                        layout: cfg.layout,
                    },
                    powertrain: powertrain::Config {
                        drive: &cfg.drive,
                        surfaces: &cfg.surfaces,
                        tuning: &cfg.tuning,
                        yaw: velocity::YawConfig {
                            axis: cfg.axis,
                            ..(&cfg.drive).into()
                        },
                    },
                    attitude: &cfg.attitude,
                    body: &cfg.body,
                    obstacles: obstacle::Environment {
                        mode: cfg.frame.mode,
                        dt: cfg.frame.dt,
                        feedback: false,
                        player_class: ClassId(0),
                        breakable_class: ClassId(1),
                        obstacle_slot: 0,
                        deformable_slot: 0,
                        tuning: cfg.collision,
                    },
                    separation_limit: 65536,
                };
                vehicle::step_physics(
                    &mut before.states,
                    own,
                    &mut world,
                    &config,
                    &cfg.frame,
                    resources,
                    &mut scene,
                )
            };
            let status = result.as_ref().map_or_else(vehicle::Error::status, |()| 0);
            assert_eq!(
                status, expected_status,
                "map={map} car={car} variant={variant} step={step}, result={result:?}"
            );
            drop(targets);
            before.random = shared.random;
            before.slot = shared.current_slot;
            let actual = before.words();
            let expected = expected.words();
            assert_eq!(actual.len(), expected.len());
            for (i, (a, b)) in actual.iter().zip(&expected).enumerate() {
                assert_eq!(
                    a, b,
                    "map={map} car={car} variant={variant} step={step} field={i}"
                );
            }
            assert_eq!(scene.calls, expected_calls);
            peer_hits += usize::from(
                before
                    .states
                    .iter()
                    .enumerate()
                    .any(|(i, s)| i != own.0 && s.motion.velocity != initial_velocities[i]),
            );
            wears += usize::from(before.damage.iter().any(|d| d.tracks != [0; 2]));
            records += 1;
            words += actual.len();
        }
    }
    assert!(
        peer_hits > 0 && wears > 0,
        "peer hits={peer_hits}, wear observations={wears}"
    );
    eprintln!(
        "{records} connected vehicle records / {words} words exact; peer writes={peer_hits}, wear observations={wears}, tank counts={tank_counts:?}"
    );
}
