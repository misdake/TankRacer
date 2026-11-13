//! Authoritative terrain -> surface/effect -> material/fallback -> suspension chain.
use crate::{
    contact::{self, Coefficient},
    deformation, geometry, motion, pose, powertrain,
    sampling::{AuxiliaryContact, Transform},
    suspension, terrain,
};
use tankracer_assets::terrain::{NodeId, TerrainData};

pub struct World<'a> {
    pub terrain: &'a TerrainData,
    pub surface_bumps: &'a [Coefficient],
    pub noise: [u16; 16],
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    Terrain(terrain::Error),
    MissingSurface(u32),
    EffectSlotWrite(u8),
}
impl From<terrain::Error> for Error {
    fn from(error: terrain::Error) -> Self {
        Self::Terrain(error)
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Point {
    pub local: [i32; 3],
    pub world: [i32; 3],
    pub kind: i32,
    pub gap: i32,
    pub previous_gap: i32,
    pub raw_height: i32,
    pub travel: i32,
    pub flags: u32,
    /// Only successful fallback queries update this field.
    pub normal: [i32; 3],
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Primary {
    pub points: [Point; 4],
    pub load: i32,
    pub airborne_count: i32,
    pub special_count: i32,
    pub flagged_count: i32,
    pub has_extra: bool,
    pub extra: [i32; 4],
    pub surfaces: [i32; 4],
    pub flagged_ground_count: i32,
    pub nodes: [Option<NodeId>; 4],
}

#[derive(Debug, Clone, Copy, Default)]
pub struct LoadConfig {
    pub base: i32,
    pub floor: i32,
    pub gain: Coefficient,
}

impl From<&tankracer_assets::config::Suspension> for LoadConfig {
    fn from(source: &tankracer_assets::config::Suspension) -> Self {
        Self {
            base: source.initial_weight,
            floor: source.initial_pressure,
            gain: source.pressure_fraction.into(),
        }
    }
}

/// Original route initialization consumes sixteen values of the shared game RNG.
pub fn initialize_noise(seed: &mut u32) -> [u16; 16] {
    std::array::from_fn(|_| (crate::random::next(seed) >> 4) as u16)
}

#[derive(Debug, Clone, Copy)]
pub struct Environment {
    pub tracks: [Transform; 2],
    pub auxiliary_local: [[i32; 3]; 2],
    pub force_surface: bool,
    pub effect_gain: Coefficient,
    pub load: LoadConfig,
    pub dt: i32,
    pub frame: u32,
    pub player: u32,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SurfaceEffect {
    pub timer: i32,
    pub restore: i32,
    pub active: i32,
    pub phase: i32,
    pub impulse: [i32; 2],
    pub pose: i32,
    /// Notification to the game-owned player effect slot, not an IO stub.
    pub clear_player_slot: bool,
}

impl SurfaceEffect {
    /// The seed belongs to the whole game; do not create a separate RNG per car.
    pub fn step(&mut self, dt: i32, frame: u32, seed: &mut u32) {
        self.clear_player_slot = false;
        if self.timer <= 0 {
            return;
        }
        self.timer = i32::from(self.timer.wrapping_sub(dt) as i16);
        crate::random::next(seed);
        self.phase = self
            .phase
            .wrapping_add((((*seed >> 4) & 65535) % 4000) as i32 - 2000);
        if frame & 3 == 0 {
            self.impulse[0] = self.impulse[0].wrapping_sub(100);
            self.impulse[1] = self.impulse[1].wrapping_sub(900);
        }
        self.pose = self
            .pose
            .wrapping_add(if frame & 1 == 0 { -200 } else { 200 });
        if self.timer <= 0 {
            self.pose = self.restore;
            self.clear_player_slot = self.active != 0;
            self.active = 0;
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Sampling {
    pub primary: Primary,
    pub effect: SurfaceEffect,
    pub auxiliary: [AuxiliaryContact; 2],
    pub materials: [i32; 2],
    pub fallback_mode: i32,
    pub feedback_required: bool,
    pub clear_player_slot: Option<u8>,
}

/// The immutable shape and its separately owned current activation state.
pub struct Deformable<'a> {
    pub geometry: &'a deformation::Geometry,
    pub state: &'a mut deformation::State,
}

impl Primary {
    fn sample(
        &mut self,
        world: &World<'_>,
        env: &Environment,
        effect_active: bool,
        mut deformable: Option<&mut Deformable<'_>>,
    ) -> Result<(), Error> {
        self.load = 0;
        self.airborne_count = 0;
        self.special_count = 0;
        self.flagged_count = 0;
        self.has_extra = false;
        self.flagged_ground_count = 0;
        for (i, point) in self.points.iter_mut().enumerate() {
            point.world = env.tracks[i / 2].point(point.local);
            let mut hit = terrain::Hit::default();
            if !terrain::query(world.terrain, point.world[0], point.world[2], &mut hit)? {
                point.gap = point.previous_gap;
                point.kind = 3;
                self.surfaces[i] = 0;
                self.nodes[i] = None;
                // Raw height, flags and previous gap intentionally retain old values.
                continue;
            }
            self.surfaces[i] = if env.force_surface {
                1
            } else {
                hit.surface as i32
            };
            self.nodes[i] = hit.node;
            point.raw_height = hit.height;
            point.flags = hit.flags & 65535;
            let gain = world
                .surface_bumps
                .get(self.surfaces[i] as usize)
                .ok_or(Error::MissingSurface(self.surfaces[i] as u32))?;
            let key = point.world[0]
                .wrapping_abs()
                .wrapping_shr(9)
                .wrapping_add(point.world[2].wrapping_abs().wrapping_shr(9))
                as u32
                & 15;
            let noise = i32::from(world.noise[key as usize]) - 127;
            let mut height = hit.height.wrapping_add(gain.apply_truncated(noise));
            if effect_active {
                height = height.wrapping_add(env.effect_gain.apply_truncated(noise));
            }
            if let Some(deform) = deformable.as_mut() {
                height =
                    height.wrapping_add(deform.state.sample(deform.geometry, point.world).offset);
            }
            if point.flags & 4 != 0 {
                self.flagged_ground_count += 1;
            }
            if hit.special == 1 {
                self.special_count += 1;
            }
            if point.flags & 0x2000 != 0 {
                self.flagged_count += 1;
            }
            if point.flags & 0x10 != 0 {
                self.has_extra = true;
                self.extra = hit.extra;
            }
            point.gap = point.world[1].wrapping_sub(height);
            point.previous_gap = point.gap;
            point.kind = if point.gap < -75 {
                0
            } else if point.gap < 75 {
                1
            } else {
                2
            };
            if point.kind == 2 {
                self.airborne_count += 1;
            }
        }
        self.load = suspension::load(
            self.points.each_ref().map(|p| p.kind),
            self.points.each_ref().map(|p| p.travel),
            env.load.base,
            env.load.floor,
            env.load.gain.multiplier,
            u32::from(env.load.gain.shift),
        );
        Ok(())
    }
}

impl Sampling {
    pub fn sample(
        &mut self,
        world: &World<'_>,
        env: &Environment,
        seed: &mut u32,
        deformable: Option<&mut Deformable<'_>>,
    ) -> Result<(), Error> {
        self.sample_with_effects(world, env, seed, deformable, |_| true)
    }
    fn sample_with_effects(
        &mut self,
        world: &World<'_>,
        env: &Environment,
        seed: &mut u32,
        deformable: Option<&mut Deformable<'_>>,
        mut clear_player_slot: impl FnMut(u8) -> bool,
    ) -> Result<(), Error> {
        self.clear_player_slot = None;
        self.primary
            .sample(world, env, self.effect.timer > 0, deformable)?;
        self.effect.step(env.dt, env.frame, seed);
        self.clear_player_slot = self.effect.clear_player_slot.then_some(env.player as u8);
        if let Some(slot) = self.clear_player_slot
            && !clear_player_slot(slot)
        {
            return Err(Error::EffectSlotWrite(slot));
        }
        self.fallback_mode = match self.primary.flagged_ground_count {
            4 => 2,
            0 => 1,
            _ => 0,
        };
        for side in 0..2 {
            if self.primary.surfaces[0] == self.primary.surfaces[2] {
                self.materials[side] = self.primary.surfaces[0];
            } else {
                self.auxiliary[side].sample(
                    world.terrain,
                    env.auxiliary_local[side],
                    &env.tracks[side],
                    env.force_surface,
                )?;
                self.materials[side] = self.auxiliary[side].surface;
            }
        }
        for point in &mut self.primary.points {
            if point.kind == 3
                && geometry::fallback(
                    world.terrain,
                    point.world,
                    self.fallback_mode,
                    &mut point.normal,
                )
            {
                point.kind = 4;
            }
        }
        self.feedback_required = env.player != 0;
        Ok(())
    }
}

pub struct Config<'a> {
    pub sampling: &'a Environment,
    pub suspension: &'a suspension::Environment,
    pub contact: &'a contact::Config,
    pub layout: [i32; 5],
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Pipeline {
    pub sampling: Sampling,
    pub response: suspension::State,
    pub pivot: [i32; 3],
}

/// Shared motion/drive fields have one authoritative owner. Copies inside the
/// pipeline are stage views refreshed before computation; its other fields,
/// including spring rates, caches and effect timers, remain persistent state.
pub struct Vehicle<'a> {
    pub motion: &'a mut motion::State,
    pub powertrain: &'a mut powertrain::State,
    pub pose: &'a mut pose::Config,
}

impl Pipeline {
    pub fn step(
        &mut self,
        world: &World<'_>,
        config: &Config<'_>,
        seed: &mut u32,
        deformable: Option<&mut Deformable<'_>>,
    ) -> Result<(), Error> {
        self.step_with_effects(world, config, seed, deformable, |_| true)
    }
    fn step_with_effects(
        &mut self,
        world: &World<'_>,
        config: &Config<'_>,
        seed: &mut u32,
        deformable: Option<&mut Deformable<'_>>,
        clear_player_slot: impl FnMut(u8) -> bool,
    ) -> Result<(), Error> {
        let env = config.sampling;
        self.sampling
            .sample_with_effects(world, env, seed, deformable, clear_player_slot)?;
        for (sample, response) in self
            .sampling
            .primary
            .points
            .iter()
            .zip(&mut self.response.points)
        {
            response.kind = sample.kind;
            response.gap = sample.gap;
            response.travel = sample.travel;
        }
        for (i, auxiliary) in self.sampling.auxiliary.iter().enumerate() {
            self.response.auxiliary_type[i] = auxiliary.kind;
            self.response.auxiliary_cached[i] = auxiliary.cached;
        }
        self.pivot = suspension::layout(
            self.sampling.primary.points.each_ref().map(|p| p.kind),
            config.layout,
        );
        self.response
            .step_resolved(config.contact, config.suspension, |side, kind| {
                let auxiliary = &mut self.sampling.auxiliary[side];
                auxiliary.sample(
                    world.terrain,
                    env.auxiliary_local[side],
                    &env.tracks[side],
                    env.force_surface,
                )?;
                *kind = auxiliary.kind;
                Ok::<(), Error>(())
            })?;
        for (sample, response) in self
            .sampling
            .primary
            .points
            .iter_mut()
            .zip(&self.response.points)
        {
            sample.travel = response.travel;
        }
        for (i, auxiliary) in self.sampling.auxiliary.iter_mut().enumerate() {
            auxiliary.kind = self.response.auxiliary_type[i];
            auxiliary.cached = self.response.auxiliary_cached[i];
        }
        Ok(())
    }

    /// Original vehicle-contact adapter commits local sampling/suspension/RNG
    /// only on success. Deformation and player-effect writes remain synchronous
    /// world operations even if a later query fails.
    pub fn step_vehicle(
        &mut self,
        vehicle: Vehicle<'_>,
        world: &World<'_>,
        config: &Config<'_>,
        seed: &mut u32,
        deformable: Option<&mut Deformable<'_>>,
        clear_player_slot: impl FnMut(u8) -> bool,
    ) -> Result<bool, Error> {
        let motion = vehicle.motion;
        let mut pending = self.clone();
        pending.pivot = motion.pivot;
        let mut next_seed = *seed;
        pending.response.angular = [motion.angular[0], motion.angular[2]];
        pending.response.world_vy = motion.velocity[1];
        pending.response.position_y = motion.poses.physical_position[1];
        pending.sampling.effect.impulse = [
            motion.articulation.recoil_rate,
            motion.articulation.pitch_rate,
        ];
        // The effect writes the turret's local Z offset (+30A), not body Y.
        pending.sampling.effect.pose = vehicle.pose.turret_offset[2];
        for (i, point) in pending.sampling.primary.points.iter_mut().enumerate() {
            point.world = motion.contacts[i].world;
            point.gap = motion.contacts[i].gap;
            point.travel = motion.travel[i];
        }
        let mut sampling = *config.sampling;
        sampling.force_surface &= sampling.player != 0;
        sampling.tracks = motion.poses.tracks.map(|p| Transform {
            basis: p.matrix.0,
            origin: p.position,
        });
        let mut suspension = *config.suspension;
        suspension.dt = sampling.dt;
        suspension.is_player = sampling.player != 0;
        suspension.base = [motion.poses.base_position[0], motion.poses.base_position[2]];
        pending.step_with_effects(
            world,
            &Config {
                sampling: &sampling,
                suspension: &suspension,
                contact: config.contact,
                layout: config.layout,
            },
            &mut next_seed,
            deformable,
            clear_player_slot,
        )?;
        motion.angular[0] = pending.response.angular[0];
        motion.angular[2] = pending.response.angular[1];
        motion.velocity[1] = pending.response.world_vy;
        motion.poses.physical_position[1] = pending.response.position_y;
        motion.articulation.recoil_rate = pending.sampling.effect.impulse[0];
        motion.articulation.pitch_rate = pending.sampling.effect.impulse[1];
        vehicle.pose.turret_offset[2] = pending.sampling.effect.pose;
        for (i, point) in pending.sampling.primary.points.iter().enumerate() {
            motion.contacts[i] = body_contact(point);
            motion.travel[i] = point.travel;
        }
        motion.pivot = pending.pivot;
        motion.rates.travel_sum = pending.response.travel_sum;
        motion.rates.penetrating_force = pending.response.penetrating_input_sum;
        vehicle.powertrain.load = pending.sampling.primary.load;
        *seed = next_seed;
        *self = pending;
        Ok(self.sampling.feedback_required)
    }
}

fn body_contact(point: &Point) -> crate::body::Contact {
    crate::body::Contact {
        world: point.world,
        gap: point.gap,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tankracer_assets::archive::Archive;

    fn ground() -> (TerrainData, [i32; 3]) {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("../../assets/game/LEVELS/ENGLAND/FASTFILE.DAT");
        let bytes = std::fs::read(path).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let file = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .unwrap();
        let data = TerrainData::decode_bsm(archive.get(file.name).unwrap()).unwrap();
        for node in &data.nodes {
            let mesh = &data.meshes[node.mesh as usize];
            for tri in &mesh.triangles {
                let mut point: [i32; 3] = std::array::from_fn(|i| {
                    mesh.position[i] * 8
                        + tri
                            .indices
                            .iter()
                            .map(|&v| i32::from(mesh.vertices[v as usize][i]))
                            .sum::<i32>()
                            * 8
                            / 3
                });
                let mut hit = terrain::Hit::default();
                if terrain::query(&data, point[0], point[2], &mut hit) == Ok(true) {
                    point[1] = hit.height;
                    return (data, point);
                }
            }
        }
        panic!("test map must contain a valid ground sample");
    }
    fn environment(origin: [i32; 3]) -> Environment {
        Environment {
            tracks: [Transform {
                origin,
                basis: [4096, 0, 0, 0, 4096, 0, 0, 0, 4096],
            }; 2],
            auxiliary_local: [[0; 3]; 2],
            force_surface: false,
            effect_gain: Coefficient {
                multiplier: 1,
                shift: 0,
            },
            load: LoadConfig {
                base: 0,
                floor: 0,
                gain: Coefficient {
                    multiplier: 1,
                    shift: 0,
                },
            },
            dt: 17,
            frame: 0,
            player: 257,
        }
    }

    #[test]
    fn vehicle_contact_failure_keeps_local_fields_and_rng_uncommitted() {
        let (terrain, mut origin) = ground();
        origin[1] += 75;
        let gains = [Coefficient::default(); 10];
        let world = World {
            terrain: &terrain,
            surface_bumps: &gains,
            noise: [128; 16],
        };
        let mut env = environment(origin);
        env.player = 1;
        let suspension = suspension::Environment::default();
        let zero = Coefficient::default();
        let contact = contact::Config {
            lower: -12000,
            upper: 12000,
            force_positive: zero,
            force_negative: zero,
            output_gain: zero,
            tilt_gain: zero,
            shape_shift: 3,
            restore_positive: zero,
            restore_negative: zero,
            damping_shift: 8,
            travel_dead_step: 1,
        };
        let config = Config {
            sampling: &env,
            suspension: &suspension,
            contact: &contact,
            layout: [0; 5],
        };
        let mut pipeline = Pipeline::default();
        pipeline.sampling.effect = SurfaceEffect {
            timer: 1,
            active: 1,
            restore: -123,
            ..SurfaceEffect::default()
        };
        let before = pipeline.clone();
        let mut motion = motion::State::default();
        motion.poses.tracks = [pose::Pose {
            position: origin,
            ..pose::Pose::default()
        }; 2];
        motion.articulation.recoil_rate = 73;
        let motion_before = motion.clone();
        let mut power = powertrain::State {
            load: 99,
            ..powertrain::State::default()
        };
        let power_before = power;
        let mut pose = pose::Config {
            track_offsets: [[0; 3]; 2],
            turret_offset: [0, 0, 321],
            gun_offset: [0; 3],
            attachment_offsets: [[0; 3]; 2],
            bottom_offset: [0; 3],
            track_length: 1,
            travel_gain: tankracer_assets::config::Gain::default(),
            gun_pivot: [0; 3],
        };
        let mut seed = 27;
        let mut slot_seen = None;
        assert_eq!(
            pipeline.step_vehicle(
                Vehicle {
                    motion: &mut motion,
                    powertrain: &mut power,
                    pose: &mut pose
                },
                &world,
                &config,
                &mut seed,
                None,
                |slot| {
                    slot_seen = Some(slot);
                    false
                }
            ),
            Err(Error::EffectSlotWrite(1))
        );
        assert_eq!(slot_seen, Some(1));
        assert_eq!(pipeline, before);
        assert_eq!(motion, motion_before);
        assert_eq!(power, power_before);
        assert_eq!(pose.turret_offset, [0, 0, 321]);
        assert_eq!(seed, 27);
    }

    #[test]
    fn expiring_effect_still_disturbs_current_samples_and_consumes_one_shared_rng_value() {
        let (data, mut origin) = ground();
        origin[1] += 75;
        let gains = [Coefficient::default(); 10];
        let world = World {
            terrain: &data,
            surface_bumps: &gains,
            noise: [128; 16],
        };
        let mut state = Sampling {
            effect: SurfaceEffect {
                timer: 1,
                active: 1,
                restore: 123,
                ..SurfaceEffect::default()
            },
            ..Sampling::default()
        };
        let mut seed = 0x12345678;
        state
            .sample(&world, &environment(origin), &mut seed, None)
            .unwrap();
        assert!(
            state
                .primary
                .points
                .iter()
                .all(|p| p.gap == 74 && p.kind == 1)
        );
        assert_eq!(state.effect.timer, -16);
        assert_eq!(state.effect.pose, 123);
        assert_eq!(state.clear_player_slot, Some(1));
        assert_eq!(seed, 0x75432777);
        state
            .sample(&world, &environment(origin), &mut seed, None)
            .unwrap();
        assert!(
            state
                .primary
                .points
                .iter()
                .all(|p| p.gap == 75 && p.kind == 2)
        );
        assert_eq!(state.clear_player_slot, None);
        assert_eq!(seed, 0x75432777); // Inactive effect consumes no RNG.
    }

    #[test]
    fn grounded_load_uses_travel_before_the_response_updates_it() {
        let (data, origin) = ground();
        let gains = [Coefficient::default(); 10];
        let world = World {
            terrain: &data,
            surface_bumps: &gains,
            noise: [127; 16],
        };
        let mut state = Pipeline::default();
        for p in &mut state.sampling.primary.points {
            p.travel = 100;
        }
        let mut env = environment(origin);
        env.dt = 1;
        let response_env = suspension::Environment {
            dt: 1,
            ..suspension::Environment::default()
        };
        let contact = contact::Config {
            lower: -1000,
            upper: 1000,
            restore_positive: Coefficient {
                multiplier: 1,
                shift: 0,
            },
            damping_shift: 8,
            ..contact::Config::default()
        };
        state
            .step(
                &world,
                &Config {
                    sampling: &env,
                    suspension: &response_env,
                    contact: &contact,
                    layout: [0; 5],
                },
                &mut 0,
                None,
            )
            .unwrap();
        assert_eq!(state.sampling.primary.load, 400);
        assert!(
            state
                .sampling
                .primary
                .points
                .iter()
                .all(|p| p.travel == -1000)
        );
    }

    #[test]
    fn missing_surface_is_an_error_after_hit_metadata_before_effect_or_response() {
        let (data, origin) = ground();
        let world = World {
            terrain: &data,
            surface_bumps: &[],
            noise: [127; 16],
        };
        let mut state = Sampling::default();
        state.effect.timer = 1;
        state.primary.points[0].previous_gap = 42;
        state.primary.load = 999;
        state.clear_player_slot = Some(1);
        let mut seed = 123;
        let result = state.sample(&world, &environment(origin), &mut seed, None);
        assert!(matches!(result, Err(Error::MissingSurface(_))));
        assert_eq!(state.primary.points[0].world, origin);
        assert_eq!(state.primary.points[0].raw_height, origin[1]);
        assert!(state.primary.nodes[0].is_some());
        assert_eq!(state.primary.points[0].previous_gap, 42);
        assert_eq!(state.primary.load, 0);
        assert_eq!(state.effect.timer, 1);
        assert_eq!(state.clear_player_slot, None);
        assert_eq!(seed, 123);
    }
}
