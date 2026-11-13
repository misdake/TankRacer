//! Run the same native-qualified fixtures through real scene/factory ownership.
//! Fixture conversion happens once per short sequence, never between callbacks.
use super::*;
use tankracer_assets::config::Gain;
use tankracer_game::scene_entity::Handle;
use tankracer_game::{
    entity_factory as factory, fall,
    obstacle_startup::{Auxiliary, Debris, PlacedObstacle},
    scene_collision::Surfaces,
    scene_objects::{
        self, Interfaces, Parents, Scene, SoundId,
        collision::{Collision, Services, Sound},
    },
    scene_startup::Visibility,
};

pub struct SceneCase {
    pub scene: Scene,
    pub factory: factory::State<Handle>,
    pub random: u32,
    pub vehicle: State,
    pub probe: Probe,
}
impl SceneCase {
    pub fn new(world: &World, methods: &Methods, vehicle: &State, shared: &Shared) -> Self {
        let world = World {
            colliders: world.colliders.clone(),
            objects: world.objects.clone(),
            classes: world.classes.clone(),
            grid: Grid {
                origin: world.grid.origin,
                shifts: world.grid.shifts,
                cells: world.grid.cells.clone(),
            },
            deformables: world.deformables.clone(),
        };
        let mut scene = Scene::new(
            world,
            Surfaces::default(),
            Parents {
                world: ClassId(0),
                generic: ClassId(0),
                obstacle: ClassId(0),
            },
            Interfaces {
                activate: 0,
                deactivate: 0,
                auxiliary_tick: 0,
                auxiliary_complete: 0,
                debris_tick: 0,
            },
        );
        let mut factory = factory::State::new(16).unwrap();
        factory.current_slot = shared.current_slot;
        factory.symbols = methods.store.symbols.clone();
        let gain = Gain {
            multiplier: 0,
            shift: 0,
        };
        for (i, state) in methods.store.states.iter().enumerate() {
            let state = state.as_ref().unwrap();
            if methods.store.live[i] {
                let id = EntityId(i as u32 + 2);
                factory.entities.new_record(id, 100).unwrap();
                let record = factory.entities.record_mut(id).unwrap();
                record.class = Some(ClassId(i));
                record.object = Some(Handle::Obstacle(ObstacleId(i)));
            }
            factory.ticks.flags[i + 2] = methods.store.ticks.flags[i];
            scene.components.falls.push(Some(fall::State {
                angular_velocity: state.angular_velocity,
                rotation: state.rotation,
                turn_rate: state.turn_rate,
                last_vehicle: state.last_vehicle,
                attached: Some(Arc::clone(&state.attached)),
                ..Default::default()
            }));
            scene.components.objects.push(Some(scene_objects::Object {
                impact_sound: state.has_sound.then_some(SoundId(i as u32)),
                data: PlacedObstacle {
                    id: ObstacleId(i),
                    cell: state.cell,
                    source: None,
                    mesh: None,
                    visibility: Visibility {
                        distance: 0,
                        add_lap: 0,
                        remove_lap: 0,
                        entered: false,
                    },
                    sort_position: 0,
                    impact_threshold: 0,
                    collision_margin: 0,
                    auxiliary: state.auxiliary.then(|| Auxiliary {
                        shape_name: String::new(),
                        gravity: 0,
                        bounce: gain,
                        friction: gain,
                        push: gain,
                        velocity: gain,
                        spin: gain,
                    }),
                    debris: state.effect_flags.map(|flags| Debris {
                        pieces: vec![],
                        retired: 0,
                        flags: u32::from(flags),
                        sprite: None,
                        animation: None,
                        offset: [0; 3],
                        ground_height: 0,
                        gravity: 0,
                        friction: 0,
                        direction: 0,
                    }),
                },
                auxiliary_mesh: None,
                auxiliary_active: 0,
                auxiliary_motion: None,
                ground_height: None,
            }));
        }
        factory.ticks.order = methods
            .store
            .ticks
            .order
            .iter()
            .map(|id| EntityId(id.0 as u32 + 2))
            .collect();
        Self {
            scene,
            factory,
            random: shared.random,
            vehicle: vehicle.clone(),
            probe: methods.probe.clone(),
        }
    }
    pub fn run(&mut self, op: u32, env: HitEnvironment, collision_env: Environment) -> Vec<u32> {
        self.probe.calls = 0;
        self.probe.trace = 0;
        self.probe.rebindings = 0;
        let classes = self.scene.world.classes.clone();
        let environment = factory::Environment {
            classes: &classes,
            work_limit: env.work_limit,
            slots: factory::Slots {
                allocate: 0,
                initialize: 0,
                destroy: 0,
                release: 0,
            },
        };
        let (lifecycle, current_slot) = self.factory.split();
        let mut scene = Collision {
            components: &mut self.scene.components,
            lifecycle,
            environment,
        };
        let mut result = 0x7182;
        let status = if op == 1 {
            scene
                .method(
                    BREAKABLE_HIT,
                    Invocation {
                        dt: 40,
                        target: ObstacleId(0),
                        vehicle: &mut self.vehicle,
                        world: &mut self.scene.world,
                        random: &mut self.random,
                        current_slot,
                        result: &mut result,
                    },
                    env,
                    &mut self.probe,
                )
                .err()
                .unwrap_or(0)
        } else {
            let mut effects = Query {
                scene: &mut scene,
                probe: &mut self.probe,
                env,
            };
            match obstacle::obstacles(
                &mut self.scene.world,
                &mut self.vehicle,
                collision_env,
                obstacle::Globals {
                    random: &mut self.random,
                    current_slot,
                },
                &mut effects,
            ) {
                Ok(_) => 0,
                Err(obstacle::Error::Dispatch(tankracer_game::dispatch::Error::Method(code))) => {
                    code
                }
                Err(obstacle::Error::Arithmetic(_)) => -2,
                e => panic!("unexpected shared scene collision {e:?}"),
            }
        };
        assert!(
            self.factory.queues.notifications.is_empty()
                && self.factory.queues.destruction.is_empty()
        );
        let mut out = vec![
            status as u32,
            result,
            self.random,
            self.factory.current_slot,
            self.probe.calls,
            self.probe.trace,
        ];
        append(&mut out, self.vehicle.velocity);
        out.extend([
            self.vehicle.forward_velocity as u32,
            u32::from(self.vehicle.player),
            self.vehicle.impact_accumulator as u32,
            self.vehicle.collision_size,
        ]);
        for i in 0..3 {
            let p = &self.scene.world.objects[i];
            let s = self.scene.components.falls[i].as_ref().unwrap();
            out.extend([
                p.activity as u32,
                p.flags,
                s.angular_velocity as u32,
                s.rotation as u32,
                s.turn_rate as u32,
                s.last_vehicle.map_or(0, |id| id.0),
                self.factory.ticks.flags[i + 2],
                self.scene.components.objects[i]
                    .as_ref()
                    .unwrap()
                    .data
                    .debris
                    .as_ref()
                    .map_or(u32::MAX, |d| d.flags),
            ]);
        }
        list(
            &mut out,
            &self
                .factory
                .ticks
                .order
                .iter()
                .map(|id| ObstacleId(id.0 as usize - 2))
                .collect::<Vec<_>>(),
        );
        for cell in &self.scene.world.grid.cells {
            list(&mut out, cell);
        }
        out
    }
}
impl Probe {
    fn shared_call(
        &mut self,
        tag: u32,
        scene: &mut Collision<'_>,
        call: Invocation<'_>,
    ) -> Result<(), i32> {
        random::next(call.random);
        self.calls += 1;
        self.trace = self.trace.wrapping_mul(37).wrapping_add(tag);
        let state = scene.components.falls[call.target.0].as_mut().unwrap();
        state.angular_velocity = state.angular_velocity.wrapping_add(self.delta);
        state.rotation = state.rotation.wrapping_add(self.delta);
        state.turn_rate = state.turn_rate.wrapping_sub(self.delta);
        let p = &mut call.world.objects[call.target.0];
        p.activity = p.activity.wrapping_add(self.delta);
        p.flags ^= 16;
        call.vehicle.forward_velocity = call.vehicle.forward_velocity.wrapping_neg();
        if self.actions & 1 != 0
            && let Some(debris) = &mut scene.components.objects[call.target.0]
                .as_mut()
                .unwrap()
                .data
                .debris
        {
            debris.flags ^= 16;
        }
        if self.actions & 2 != 0 {
            call.vehicle.player = false;
        }
        if self.actions & 4 != 0 {
            for i in 0..3 {
                let name = scene.lifecycle.symbols.intern(&format!("last{i}")).unwrap();
                self.rebindings +=
                    u32::from(scene.lifecycle.symbols.entity(name) != Some(EntityId(4)));
                scene
                    .lifecycle
                    .symbols
                    .bind_entity(name, EntityId(4))
                    .unwrap();
            }
        }
        if self.calls == self.fail_at && self.fail_status != 0 {
            Err(self.fail_status)
        } else {
            Ok(())
        }
    }
}
impl Services for Probe {
    fn method(
        &mut self,
        method: MethodId,
        scene: &mut Collision<'_>,
        call: Invocation<'_>,
        has_payload: bool,
    ) -> Result<(), i32> {
        if method == ENABLE_TICK {
            return scene.scene_method(method, call, has_payload);
        }
        let entity = call.world.colliders[call.target.0].entity.0;
        let tag = u32::from(method.0) * 65536
            + entity * 256
            + u32::from(has_payload) * 128
            + if has_payload {
                call.vehicle.entity.0
            } else {
                0
            };
        *call.current_slot = call.current_slot.wrapping_add(7);
        *call.result = 1234;
        self.shared_call(tag, scene, call)
    }
    fn sound(
        &mut self,
        kind: Sound,
        scene: &mut Collision<'_>,
        call: Invocation<'_>,
    ) -> Result<(), i32> {
        assert_eq!(kind, Sound::Obstacle);
        let entity = call.world.colliders[call.target.0].entity.0;
        self.shared_call(0x600000 + entity, scene, call)
    }
    fn feedback(
        &mut self,
        intensity: i32,
        scene: &mut Collision<'_>,
        call: Invocation<'_>,
    ) -> Result<(), i32> {
        let entity = call.world.colliders[call.target.0].entity.0;
        self.shared_call(0x900000 + entity * 65536 + intensity as u32, scene, call)
    }
}
struct Query<'a, 'scene> {
    scene: &'a mut Collision<'scene>,
    probe: &'a mut Probe,
    env: HitEnvironment,
}
impl Effects for Query<'_, '_> {
    fn method(&mut self, method: MethodId, call: Invocation<'_>) -> Result<(), i32> {
        self.scene.method(method, call, self.env, self.probe)
    }
    fn feedback(&mut self, _: i32, _: &mut State, _: &mut u32) -> Result<(), i32> {
        panic!("unexpected ordinary feedback")
    }
    fn impact(&mut self, _: &mut State, _: usize, _: i32, _: &mut u32) -> Result<(), i32> {
        panic!("unexpected ordinary impact")
    }
}
