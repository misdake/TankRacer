//! Actual authored factory -> hit -> motion, checked against the native owner.
use super::{Reader, check_list};
use tankracer_game::scene_entity::Handle;
use tankracer_game::{
    breakable,
    dispatch::ClassId,
    entity_factory::{self as factory},
    math::{Axis, Matrix},
    object_query::{EntityId, ObstacleId},
    obstacle_collision::{self as collision, Invocation},
    scene_loading::{self, Registry},
    scene_objects::{
        Scene,
        collision::{Collision, Services, Sound},
    },
};
struct Devices;
impl Services for Devices {
    fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        panic!("no sound resource");
    }
    fn feedback(&mut self, _: i32, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        panic!("non-player peer");
    }
}
pub fn check(
    r: &mut Reader<'_>,
    scene: &mut Scene,
    state: &mut factory::State<Handle>,
    registry: &Registry,
    id: ObstacleId,
    seed: u32,
    label: &str,
) {
    let entity = scene.world.colliders[id.0].entity;
    let old_fall = scene.components.falls[id.0].clone();
    let old_collider = scene.world.colliders[id.0];
    let old_properties = scene.world.objects[id.0];
    let old_grid = scene.world.grid.cells.clone();
    let old_entities = state.entities.clone();
    let old_ticks = state.ticks.clone();
    let old_queues = state.queues.clone();
    let old_slot = state.current_slot;
    let old_dt = scene.components.dt;
    let peer_id = ObstacleId(scene.world.colliders.len());
    let mut matrix = Matrix::IDENTITY;
    matrix.rotate(Axis::Y, 8192);
    let mut peer = old_collider;
    peer.entity = EntityId(1);
    peer.pose.matrix = matrix;
    scene.world.colliders.push(peer);
    if state.entities.record(EntityId(1)).is_none() {
        state.entities.new_record(EntityId(1), 100000).unwrap();
    }
    let record = state.entities.record_mut(EntityId(1)).unwrap();
    record.class = Some(ClassId(0));
    record.object = Some(Handle::Obstacle(peer_id));
    let mut vehicle = collision::State {
        entity: EntityId(1),
        class: registry.world,
        base_position: [0; 3],
        position: [0; 3],
        previous_position: [0; 3],
        velocity: [250000, -150000, 500000],
        forward_velocity: if entity.0 & 1 != 0 { -400000 } else { 400000 },
        player: false,
        contacts: [[0; 3]; 4],
        impact_accumulator: 0,
        collision_size: 0,
        material_tag: 0,
        deformable: None,
    };
    state.current_slot = 777;
    let mut random = seed;
    for step in 0..6 {
        let (status, result) = if step == 0 || step == 5 {
            let (lifecycle, current_slot) = state.split();
            let env = registry.hit_environment(scene.components.fall_tuning, false, 100000);
            let mut effects = Collision {
                components: &mut scene.components,
                lifecycle,
                environment: registry.environment(100000),
            };
            let mut result = 0x7182;
            let status = effects
                .method(
                    breakable::BREAKABLE_HIT,
                    Invocation {
                        dt: 40,
                        target: id,
                        vehicle: &mut vehicle,
                        world: &mut scene.world,
                        random: &mut random,
                        current_slot,
                        result: &mut result,
                    },
                    env,
                    &mut Devices,
                )
                .err()
                .unwrap_or(0);
            (status, result)
        } else {
            scene.components.dt = [16, 33, 40, 1000][step - 1];
            let result = state.context().dispatch(
                entity,
                scene_loading::TICK,
                None,
                registry.environment(100000),
                scene,
            );
            match result {
                Ok(result) => (0, result),
                Err(factory::Error::Dispatch(tankracer_game::dispatch::Error::Method(code))) => {
                    (code, 0)
                }
                other => panic!("{label} tick error {other:?}"),
            }
        };
        r.check(
            [
                status,
                result as i32,
                random as i32,
                state.current_slot as i32,
            ],
            label,
        );
        r.check(scene.world.colliders[id.0].pose.matrix.0, label);
        let s = scene.components.falls[id.0].as_ref().unwrap();
        let p = scene.world.objects[id.0];
        r.check(
            [
                s.angular_velocity,
                s.rotation,
                s.turn_rate,
                s.slope_limit,
                p.activity,
                p.flags as i32,
                state.ticks.flags[entity.0 as usize] as i32,
                s.last_vehicle.map_or(0, |id| id.0) as i32,
            ],
            label,
        );
        check_list(r, state.ticks.order.iter().map(|id| id.0), label);
        check_list(r, state.queues.notifications.iter().map(|id| id.0), label);
        for cell in &scene.world.grid.cells {
            check_list(
                r,
                cell.iter().map(|id| scene.world.colliders[id.0].entity.0),
                label,
            );
        }
    }
    scene.components.falls[id.0] = old_fall;
    scene.world.colliders[id.0] = old_collider;
    scene.world.colliders.truncate(peer_id.0);
    scene.world.objects[id.0] = old_properties;
    scene.world.grid.cells = old_grid;
    scene.components.dt = old_dt;
    state.entities = old_entities;
    state.ticks = old_ticks;
    state.queues = old_queues;
    state.current_slot = old_slot;
}
