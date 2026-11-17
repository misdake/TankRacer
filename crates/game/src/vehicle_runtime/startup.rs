//! Resource-built world and derived immutable physics profile.
use super::*;

impl Startup {
    pub fn terrain(&self) -> &Arc<TerrainData> {
        &self.terrain
    }
    pub fn low_gravity(&self) -> bool {
        self.settings.environment_flag != 0
    }
    pub fn map(&self) -> u32 {
        self.settings.map
    }
    pub fn new(terrain: Arc<TerrainData>, settings: Settings) -> Result<Self, Error> {
        if settings.map >= 8 || !(0..=1).contains(&settings.mode) {
            return Err(Error::UnsupportedConfiguration);
        }
        let registry = Registry::default();
        let identity = factory::State::new(128).map_err(Error::Factory)?;
        let mut random = settings.seed;
        // Results initialization precedes environment shield initialization.
        // Intervening authored-world RNG remains a later full-startup concern.
        let noise = contact_pipeline::initialize_noise(&mut random);
        let mut shield = shield::State::default();
        shield.initialize(&mut random);
        let shared = Shared {
            random,
            input: player_input::Shared::default(),
            noise,
            pool: crate::effect_pool::Pool::default(),
            impacts: impact_particles::State::default(),
            impact_vehicle: impact_particles::Vehicle {
                entity: EntityId(0),
                contacts: [[0; 3]; 4],
                forward_velocity: 0,
                external_basis: None,
                cooldown: 0,
            },
            damage: damage::State::default(),
            debris: debris::State::default(),
            pickup: pickup::State::default(),
            temporary: temporary::State::default(),
            shield,
            view: prefix::View::default(),
            camera: boost::Camera::default(),
            spin: spin::Shared::default(),
            boost: boost::Shared::default(),
            debris_sound: None,
        };
        let mut objects = registry.scene(obstacle::Grid {
            origin: terrain.origin.map(|v| v.wrapping_mul(8)),
            shifts: terrain.shifts.map(|v| u32::from(v) + 3),
            cells: std::array::from_fn(|_| Vec::new()),
        });
        objects.components.terrain = Some(Arc::clone(&terrain));
        Ok(Self {
            identity,
            registry,
            objects,
            terrain,
            shared,
            settings,
        })
    }
    pub fn finish(self, surfaces: Surfaces, request: Request) -> Result<SinglePlayer, Error> {
        SinglePlayer::from_startup(self, surfaces, request)
    }
}
impl SinglePlayer {
    pub fn terrain(&self) -> &Arc<TerrainData> {
        &self.terrain
    }
    pub fn surfaces(&self) -> &Surfaces {
        &self.surfaces
    }
    /// Session configuration and its derived Profile are immutable together.
    /// Select another map/car/configuration by constructing a fresh world.
    pub fn settings(&self) -> Settings {
        self.settings
    }
    pub fn new(
        terrain: Arc<TerrainData>,
        surfaces: Surfaces,
        request: Request,
        settings: Settings,
    ) -> Result<Self, Error> {
        Startup::new(terrain, settings)?.finish(surfaces, request)
    }
    fn from_startup(startup: Startup, surfaces: Surfaces, request: Request) -> Result<Self, Error> {
        if request.environment.session_active {
            return Err(Error::UnsupportedConfiguration);
        }
        let Startup {
            mut identity,
            registry,
            objects,
            terrain,
            mut shared,
            settings,
        } = startup;
        let mut vehicles = Store::default();
        let (_, player) = vehicles
            .create_single_player(&mut identity, &registry, request, 100000)
            .map_err(Error::Player)?;
        let v = vehicles.entries[player.0]
            .as_ref()
            .ok_or(Error::MissingPlayer)?;
        let model = &v.model;
        let suspension = &model.config.tank.suspension;
        let bounds = model.geometry_bounds;
        let profile = Profile {
            contact: suspension.try_into().map_err(Error::Config)?,
            sampling: contact_pipeline::Environment {
                tracks: [Transform {
                    basis: crate::math::Matrix::IDENTITY.0,
                    origin: [0; 3],
                }; 2],
                auxiliary_local: model.auxiliary_local,
                force_surface: settings.special,
                effect_gain: contact::Coefficient {
                    multiplier: 3,
                    shift: 9,
                },
                load: suspension.into(),
                dt: 0,
                frame: 0,
                player: 1,
            },
            suspension: suspension::Environment {
                environment_flag: settings.environment_flag,
                mode: settings.map as i32,
                region_min: [0x4a77f, 0xfffb4856_u32 as i32],
                region_max: [0x813d2, 0xffff80ec_u32 as i32],
                player_multiplier: model.moon.player.multiplier,
                player_shift: u32::from(model.moon.player.shift as u8),
                other_multiplier: model.moon.other.multiplier,
                other_shift: u32::from(model.moon.other.shift as u8),
                penetration_shift: u32::from(suspension.susp_to_shape.shift as u8),
                ..suspension::Environment::default()
            },
            layout: [
                bounds.min[0],
                bounds.min[1],
                bounds.min[2],
                bounds.max[0],
                bounds.max[2],
            ],
            attitude: suspension.into(),
            drive: drive::Tuning::default(),
            body: body::Tuning::default(),
            spray: spray::Tuning::environment(
                settings.spray_materials,
                settings.map == 2,
                settings.hardware,
            ),
            smoke: smoke::Tuning::default(),
            pickup: pickup::Tuning::new(0),
            light_offset: model.config.tank.glow_offset,
            bumps: surfaces.entries.map(|s| s.bump.into()),
        };
        let tank = Tank {
            collider: Collider {
                entity: v.physics.entity,
                pose: v.physics.motion.poses.body,
                external_basis: Some(v.physics.motion.poses.reference),
                radius: model.parts[Part::Body.slot()]
                    .as_ref()
                    .ok_or(Error::MissingPlayer)?
                    .radius,
                bounds: model.collision_bounds,
            },
            slot: 0,
            active: true,
            disabled: false,
            ready: false,
            overlap_bounds: model.geometry_bounds,
            contacts: v.physics.motion.contacts.map(|p| p.world),
        };
        shared.impact_vehicle = impact_particles::Vehicle {
            entity: v.physics.entity,
            contacts: tank.contacts,
            forward_velocity: 0,
            external_basis: tank.collider.external_basis,
            cooldown: 0,
        };
        Ok(Self {
            identity,
            registry,
            vehicles,
            objects,
            terrain,
            surfaces,
            shared,
            player,
            frame: 0,
            race_active: true,
            camera_position: [0; 3],
            camera_forward: [0, 0, 4096],
            settings,
            profile,
            tank,
            peer: None,
        })
    }
}
