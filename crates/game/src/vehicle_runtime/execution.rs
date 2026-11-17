//! Existing scheduler and physics/IO bridge; call order is preserved.
use super::*;

impl SinglePlayer {
    pub(super) fn advance(
        &mut self,
        input: drive::Input,
        keys: Option<(player_input::Keys, i32)>,
        dt: i32,
        drive_active: bool,
        devices: &mut impl Devices,
    ) -> Result<(), Error> {
        if dt <= 0 {
            return Err(Error::UnsupportedConfiguration);
        }
        self.frame = self.frame.wrapping_add(1);
        self.shared.debris_sound = None;
        self.objects.components.dt = dt;
        self.objects.components.frame_group = self.frame;
        self.objects.components.mode = self.settings.mode;
        self.objects.components.camera_position = self.camera_position;
        let hit = self
            .registry
            .hit_environment(self.objects.components.fall_tuning, false, 65536);
        let mut execution = Execution {
            keys,
            race_active: self.race_active,
            vehicles: &mut self.vehicles,
            objects: &mut self.objects,
            shared: &mut self.shared,
            terrain: &self.terrain,
            surfaces: &self.surfaces,
            profile: &self.profile,
            settings: self.settings,
            frame: vehicle::Frame {
                dt,
                number: self.frame,
                mode: self.settings.mode,
                input,
                drive: drive::Environment {
                    active: drive_active,
                    ..drive::Environment::default()
                },
            },
            camera: (self.camera_position, self.camera_forward),
            tank: &mut self.tank,
            peer: &mut self.peer,
            devices,
            hit,
            breakable_class: self.registry.fall,
        };
        let environment = self.registry.environment(100000);
        self.identity
            .context()
            .tick(scene_loading::TICK, environment, &mut execution)
            .map_err(Error::Factory)?;
        self.identity
            .context()
            .drain(scene_loading::STOP, environment, &mut execution)
            .map_err(Error::Factory)
    }
}
struct Execution<'a, D> {
    keys: Option<(player_input::Keys, i32)>,
    race_active: bool,
    vehicles: &'a mut Store,
    objects: &'a mut scene_objects::Scene,
    shared: &'a mut Shared,
    terrain: &'a TerrainData,
    surfaces: &'a Surfaces,
    profile: &'a Profile,
    settings: Settings,
    frame: vehicle::Frame,
    camera: ([i32; 3], [i32; 3]),
    tank: &'a mut Tank,
    peer: &'a mut Option<tank_collision::PeerSnapshot>,
    devices: &'a mut D,
    hit: crate::breakable::Environment,
    breakable_class: crate::dispatch::ClassId,
}
impl<D: Devices> Methods<Handle> for Execution<'_, D> {
    fn invoke(
        &mut self,
        method: MethodId,
        call: factory::Invocation<'_, Handle>,
        env: factory::Environment<'_>,
    ) -> Result<(), i32> {
        if method == vehicle_loading::APPLY_BOOST {
            let id = call
                .context
                .entities
                .record(call.entity)
                .and_then(|r| r.object)
                .and_then(Handle::vehicle)
                .ok_or(-1)?;
            let player = self
                .vehicles
                .entries
                .get_mut(id.0)
                .and_then(Option::as_mut)
                .ok_or(-1)?;
            player
                .prefix
                .boost
                .apply(player.model.config.tank.drive.turbo_amount, 65000, 50);
            *call.result = call.entity.0;
            return Ok(());
        }
        if method != vehicle_loading::TICK {
            return match call
                .context
                .entities
                .record(call.entity)
                .and_then(|r| r.object)
            {
                Some(Handle::Vehicle(_)) => vehicle_loading::Lifecycle.invoke(method, call, env),
                _ => self
                    .objects
                    .updates_with_vehicles(&mut self.shared.random, self.vehicles)
                    .invoke(method, call, env),
            };
        }
        let id = call
            .context
            .entities
            .record(call.entity)
            .and_then(|r| r.object)
            .and_then(Handle::vehicle)
            .ok_or(-1)?;
        // First runnable slice is single-player without opponents; no car state
        // is moved/cloned to manufacture a contiguous array every frame.
        if id.0 != 0 || self.vehicles.entries.len() != 1 {
            return Err(-5);
        }
        let player = self.vehicles.entries[0].as_mut().ok_or(-1)?;
        let v = &mut player.physics;
        let model = &mut player.model;
        if let Some((mut keys, start_stage)) = self.keys {
            let outcome = player.input.step(
                &mut player.prefix.countdown_throttle,
                &mut self.shared.input,
                player_input::Frame {
                    dt: self.frame.dt,
                    driving: self.frame.drive.active,
                    player: 0,
                    speed: v.powertrain.local_velocity[2],
                    start_stage,
                    analog: false,
                    analog_longitudinal: false,
                    twin_tracks: false,
                    track_values: [0; 2],
                },
                player_input::Tuning::default(),
                &mut keys,
            )?;
            if outcome.driving {
                crate::player_collision::step(&mut v.collision, self.frame.dt, self.race_active);
                player.turret.tick(
                    &mut v.pose_input.turret_yaw,
                    crate::turret::Keys {
                        left: keys.turret_left,
                        right: keys.turret_right,
                    },
                    self.frame.dt,
                );
                let fast = outcome.auxiliary_down
                    && v.powertrain.local_velocity[2].wrapping_abs() > 250000;
                let restoring =
                    player.input.drive.alternate && (player.prefix.temporary_timer != 0 || !fast);
                // The source does not read the previous material on an ordinary
                // unmodified tick or while entering/holding the fast brake.
                let slide = if restoring {
                    let material =
                        usize::try_from(v.contact.sampling.materials[0]).map_err(|_| -1)?;
                    self.surfaces.entries.get(material).ok_or(-1)?.slide[0].multiplier
                } else {
                    0
                };
                player.brake.step(
                    player_brake::Vehicle {
                        input: &mut player.input.drive,
                        max_turn: &mut model.config.tank.drive.max_turn,
                        yaw_lock: &mut v.yaw_lock,
                    },
                    player_brake::Frame {
                        dt: self.frame.dt,
                        speed: v.powertrain.local_velocity[2],
                        mode: self.settings.mode,
                        temporary_timer: player.prefix.temporary_timer,
                        down: outcome.auxiliary_down,
                    },
                    player_brake::Reference {
                        slide,
                        rotation: model.config.tank.drive.rotational_drag.step,
                    },
                );
            }
            self.frame.input = player.input.drive;
            self.frame.drive.launch_eligible = player.input.fast_start != 0;
        }
        let factory::Context {
            entities,
            symbols,
            current_slot,
            ticks,
            queues,
        } = call.context;
        let entity = v.entity;
        let turbo_amount = model.config.tank.drive.turbo_amount;
        let prefix_frame = prefix::Frame {
            boost: boost::Environment {
                dt: self.frame.dt,
                frame: self.frame.number,
                player: true,
                camera_preset: false,
                camera_variant: false,
                vehicle_mode: 0,
            },
            drive_active: self.frame.drive.active,
            rpm_limit: self.profile.drive.gear_limits[0],
            spin: spin::Tuning::default(),
            spin_work_limit: 65536,
        };
        player.prefix.prepare_vehicle(
            v,
            &mut model.config.tank.drive,
            &mut self.frame,
            prefix_frame,
            prefix::World {
                view: &mut self.shared.view,
                camera_position: self.camera.0,
                camera: &mut self.shared.camera,
                spin: &mut self.shared.spin,
                boost: &mut self.shared.boost,
                temporary: &mut self.shared.temporary,
                pool: &mut self.shared.pool,
                random: &mut self.shared.random,
            },
            |state, target| {
                let class = entities
                    .record(target)
                    .and_then(|r| r.class)
                    .and_then(|id| env.classes.get(id.0))
                    .ok_or(-1)?;
                crate::dispatch::call_entity(
                    class,
                    vehicle_loading::BOOST_SLOT,
                    current_slot,
                    &mut 0,
                    |method, _, out| {
                        if method != vehicle_loading::APPLY_BOOST || target != entity {
                            return Err(-5);
                        }
                        state.apply(turbo_amount, 65000, 50);
                        *out = target.0;
                        Ok(())
                    },
                )
                .map_err(|e| match e {
                    crate::dispatch::Error::Method(c) => c,
                    _ => -1,
                })
            },
        )?;
        let drive = &model.config.tank.drive;
        let config = vehicle::Config {
            contact: contact_pipeline::Config {
                sampling: &self.profile.sampling,
                suspension: &self.profile.suspension,
                contact: &self.profile.contact,
                layout: self.profile.layout,
            },
            powertrain: powertrain::Config {
                drive,
                surfaces: self.surfaces,
                tuning: &self.profile.drive,
                yaw: velocity::YawConfig::from(drive),
            },
            attitude: &self.profile.attitude,
            body: &self.profile.body,
            obstacles: obstacle::Environment {
                mode: self.frame.mode,
                dt: self.frame.dt,
                feedback: false,
                player_class: v.class,
                breakable_class: self.breakable_class,
                obstacle_slot: scene_loading::OBSTACLE_CONTACT,
                deformable_slot: scene_loading::DEFORMABLE_CONTACT,
                tuning: obstacle::Tuning::default(),
            },
            separation_limit: 65536,
        };
        let body_before = self.shared.damage.body;
        let gun_z = |model: &crate::vehicle_model::VehicleModel| -> Option<[Option<i16>; 4]> {
            let vertices = &model.parts[Part::Gun.slot()].as_ref()?.geometry().vertices;
            Some(
                model
                    .gun_vertices
                    .map(|index| vertices.get(index as usize).map(|p| p[2])),
            )
        };
        let gun_before = gun_z(model);
        let (body_parts, gun_parts) = model.parts.split_at_mut(Part::Gun.slot());
        let mut damage = [damage::Target {
            state: &mut self.shared.damage,
            player: true,
            body_pose: v.motion.poses.body,
            body_vertices: model.damage_vertices[0],
            body_mesh: body_parts[Part::Body.slot()]
                .as_mut()
                .and_then(|m| m.owned.as_mut()),
        }];
        let gun = gun_parts[0]
            .as_mut()
            .and_then(|m| m.owned.as_mut())
            .map(|m| body::GunMesh {
                vertices: &mut m.vertices,
                indices: model.gun_vertices,
                original_z: model.gun_original_z,
            });
        let mut corners = [v.contact.response.points.map(|p| p.corner)];
        let mut effect_owners = [
            self.shared.shield.banks[0].tail,
            self.shared.shield.banks[1].tail,
        ];
        let terrain = contact_pipeline::World {
            terrain: self.terrain,
            surface_bumps: &self.profile.bumps,
            noise: self.shared.noise,
        };
        let mut slots = [None; 9];
        slots[0] = Some(TankId(0));
        let mut services = AfterPose {
            collision: scene_objects::collision::Collision {
                components: &mut self.objects.components,
                lifecycle: factory::Lifecycle {
                    entities,
                    symbols,
                    ticks,
                    queues,
                },
                environment: env,
            },
            devices: self.devices,
            hit: self.hit,
            local: chain::Local {
                spray: &mut player.spray,
                smoke: &mut player.smoke,
                marks: &mut player.marks,
                lights: &mut player.lights,
                temporary_timer: player.prefix.temporary_timer,
                temporary_slot: player.prefix.temporary_slot,
            },
            debris_vehicle: &mut player.debris,
            debris: &mut self.shared.debris,
            pickup: &mut self.shared.pickup,
            temporary: &mut self.shared.temporary,
            shield: &mut self.shared.shield,
            sound: &mut self.shared.debris_sound,
            tuning: chain::Tuning {
                spray: &self.profile.spray,
                smoke: &self.profile.smoke,
                pickup: &self.profile.pickup,
            },
            settings: chain::Settings {
                special: self.settings.special,
                lights: crate::vehicle_scene::lights::Settings {
                    active: true,
                    visible: 1,
                    queued: false,
                    reverse_braking: player.brake.reverse_braking,
                    material: self.settings.light_material,
                },
                light_offset: self.profile.light_offset,
                camera: self.camera,
            },
        };
        let result = vehicle::step_physics(
            std::slice::from_mut(v),
            TankId(0),
            &mut vehicle::World {
                terrain: &terrain,
                objects: &mut self.objects.world,
                tanks: std::slice::from_mut(self.tank),
                slots: &slots,
                peer: self.peer,
                shared: obstacle::Globals {
                    random: &mut self.shared.random,
                    current_slot,
                },
                effects: collision_effects::Bank {
                    particles: &mut self.shared.impacts,
                    lines: &mut self.shared.pool,
                    vehicles: std::slice::from_mut(&mut self.shared.impact_vehicle),
                    damage: &mut damage,
                    corners: &mut corners,
                    frame: self.frame.number,
                    tuning: impact_particles::Tuning::default(),
                },
                player_effects: &mut effect_owners,
            },
            &config,
            &self.frame,
            vehicle::Resources { gun },
            &mut services,
        );
        // Publish mesh revisions at the mutation boundary, including partial
        // errors. Physics writes only a one-shot body dent and four gun Z values.
        if self.shared.damage.body != body_before
            && let Some(body) = &mut model.parts[Part::Body.slot()]
        {
            body.changed();
        }
        if gun_z(model) != gun_before
            && let Some(gun) = &mut model.parts[Part::Gun.slot()]
        {
            gun.changed();
        }
        // These two scalar stage views alias shield tails in the original. No
        // kernel in this stage reads a tail; publish even an error's prefix.
        self.shared.shield.banks[0].tail = effect_owners[0];
        self.shared.shield.banks[1].tail = effect_owners[1];
        result.map_err(|e| e.status())?;
        *call.result = call.entity.0;
        Ok(())
    }
}
struct AfterPose<'a, D> {
    collision: scene_objects::collision::Collision<'a>,
    devices: &'a mut D,
    hit: crate::breakable::Environment,
    local: chain::Local<'a>,
    debris_vehicle: &'a mut debris::Vehicle,
    debris: &'a mut debris::State,
    pickup: &'a mut pickup::State,
    temporary: &'a mut temporary::State,
    shield: &'a mut shield::State,
    sound: &'a mut Option<u32>,
    tuning: chain::Tuning<'a>,
    settings: chain::Settings,
}
impl<D: Devices> collision_effects::ObjectServices for AfterPose<'_, D> {
    fn method(
        &mut self,
        method: MethodId,
        call: obstacle::Invocation<'_>,
        _: &mut collision_effects::Bank<'_, '_>,
    ) -> Result<(), i32> {
        self.collision.method(
            method,
            call,
            self.hit,
            &mut SingleVehicleDevices(self.devices),
        )
    }
    fn feedback(
        &mut self,
        intensity: i32,
        _: &mut obstacle::State,
        _: &mut u32,
    ) -> Result<(), i32> {
        self.devices.impact_feedback(intensity)
    }
}
/// Game blast bindings live with the runtime's authoritative vehicle, while
/// resource/device operations remain delegated to the host adapter.
struct SingleVehicleDevices<'a, D>(&'a mut D);
impl<D: Devices> scene_objects::collision::Services for SingleVehicleDevices<'_, D> {
    fn method(
        &mut self,
        method: MethodId,
        scene: &mut scene_objects::collision::Collision<'_>,
        call: obstacle::Invocation<'_>,
        payload: bool,
    ) -> Result<(), i32> {
        self.0.method(method, scene, call, payload)
    }
    fn auxiliary_model(
        &mut self,
        name: &str,
    ) -> Result<Option<Arc<tankracer_assets::model::Model>>, i32> {
        self.0.auxiliary_model(name)
    }
    fn auxiliary_height(
        &mut self,
        scene: &mut scene_objects::collision::Collision<'_>,
        call: obstacle::Invocation<'_>,
    ) -> Result<i32, i32> {
        self.0.auxiliary_height(scene, call)
    }
    fn paired_vertex_tail(&mut self) -> Option<u16> {
        self.0.paired_vertex_tail()
    }
    fn blast_target(
        &mut self,
        slot: usize,
        _: &mut scene_objects::collision::Collision<'_>,
        call: obstacle::Invocation<'_>,
    ) -> Result<Option<crate::shatter::BlastTarget>, i32> {
        Ok((slot == 0).then_some(crate::shatter::BlastTarget {
            entity: call.vehicle.entity,
            position: call.vehicle.base_position,
            protection_timer: call.vehicle.blast.protection_timer,
        }))
    }
    fn blast_impulse(
        &mut self,
        target: EntityId,
        scene: &mut scene_objects::collision::Collision<'_>,
        call: obstacle::Invocation<'_>,
    ) -> Result<(), i32> {
        if target != call.vehicle.entity || ![0, 1].contains(&scene.components.mode) {
            return Err(-5);
        }
        let source = call.world.colliders[call.target.0].pose.position;
        let feedback = crate::blast::apply(
            crate::blast::Target {
                state: &mut call.vehicle.blast,
                velocity: &mut call.vehicle.velocity,
                angular: &mut call.vehicle.angular,
                marker: &mut call.vehicle.collision_size,
            },
            crate::blast::Input {
                position: call.vehicle.position,
                source,
                mode: 4,
                damage: false,
                max_health: 40000,
                record_axes: false,
                mark: true,
            },
            call.random,
        )?;
        if feedback && call.vehicle.player {
            self.0.feedback(1, scene, call)?;
        }
        Ok(())
    }
    fn sound(
        &mut self,
        kind: scene_objects::collision::Sound,
        scene: &mut scene_objects::collision::Collision<'_>,
        call: obstacle::Invocation<'_>,
    ) -> Result<(), i32> {
        self.0.sound(kind, scene, call)
    }
    fn feedback(
        &mut self,
        n: i32,
        scene: &mut scene_objects::collision::Collision<'_>,
        call: obstacle::Invocation<'_>,
    ) -> Result<(), i32> {
        self.0.feedback(n, scene, call)
    }
}
impl<D: Devices> vehicle::Services for AfterPose<'_, D> {
    fn with_deformable<R>(
        &mut self,
        entity: Option<EntityId>,
        colliders: &[Collider],
        run: impl FnOnce(Option<&mut contact_pipeline::Deformable<'_>>) -> R,
    ) -> Result<R, i32> {
        self.collision
            .components
            .surfaces
            .with_sampling(entity, colliders, run)
    }
    fn contact_feedback(&mut self, _: &mut vehicle::State, _: &mut u32) -> Result<(), i32> {
        self.devices.surface_feedback()
    }
    fn after_pose(
        &mut self,
        v: vehicle::AfterPose<'_>,
        effects: &mut collision_effects::Bank<'_, '_>,
        random: &mut u32,
    ) -> Result<(), i32> {
        let mut owners = [debris::Owner {
            entity: v.entity,
            state: self.debris_vehicle,
        }];
        chain::active(
            v,
            chain::Local {
                spray: self.local.spray,
                marks: self.local.marks,
                smoke: self.local.smoke,
                lights: self.local.lights,
                temporary_timer: self.local.temporary_timer,
                temporary_slot: self.local.temporary_slot,
            },
            chain::World {
                own: 0,
                debris: self.debris,
                debris_owners: &mut owners,
                pickup: self.pickup,
                temporary: self.temporary,
                shield: self.shield,
                debris_sound: self.sound,
            },
            effects,
            random,
            self.settings,
            chain::Tuning {
                spray: self.tuning.spray,
                smoke: self.tuning.smoke,
                pickup: self.tuning.pickup,
            },
        )
    }
}
