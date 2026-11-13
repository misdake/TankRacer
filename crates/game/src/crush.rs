//! Concrete Crush lifecycle (407F70/407790/4077E0/407F00). Contact sampling,
//! record updates and drawing share the same buffer and mutable mesh handles.
use crate::{
    deformation,
    dispatch::MethodId,
    entity_factory::{Environment, Invocation},
    lifecycle::{self, Queues, Ticks},
    object_query::{EntityId, ObstacleId},
    scene_collision::Deformable,
    scene_meshes::Meshes,
    spatial,
};

pub const TICK: MethodId = MethodId(16);
#[derive(Debug, Clone, Default)]
pub struct State {
    pub siren_timer: i32,
    pub sound_channel: u32,
    pub highlighted: bool,
    pub private_mesh: bool,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    InvalidLimit,
    Limit,
    DetachedRecords,
    Lifecycle(lifecycle::Error<EntityId>),
    Spatial(spatial::Error),
    Mesh,
}
pub fn set_highlight(surface: &mut Deformable, mode: u32) {
    surface.runtime.highlighted = mode == 1;
}
/// Update records before requesting deferred stop. Errors retain the siren and
/// completed record writes; the detached flag is committed before queueing.
pub fn tick(
    surface: &mut Deformable,
    dt: i32,
    id: EntityId,
    ticks: &mut Ticks<EntityId>,
    queues: &mut Queues<EntityId>,
    limit: u32,
) -> Result<(), Error> {
    if limit == 0 {
        return Err(Error::InvalidLimit);
    }
    surface.runtime.sound_channel = 0;
    let timer = &mut surface.runtime.siren_timer;
    if surface.sampling.state.dirty != 0 && *timer != 0 {
        *timer = if *timer < 11026 {
            0
        } else {
            timer.wrapping_sub(dt.wrapping_mul(8))
        };
    }
    let state = &mut surface.sampling.state;
    if !state.records_attached && !state.records.is_empty() {
        return Err(Error::DetachedRecords);
    }
    let mut retired = 0;
    for (index, record) in state.records.iter_mut().enumerate() {
        if index >= limit as usize {
            return Err(Error::Limit);
        }
        match record.state {
            0 | 1 => {
                if record.timer > dt {
                    record.timer = record.timer.wrapping_sub(dt);
                } else {
                    record.state ^= 1;
                    record.timer = record.timer.wrapping_add(500);
                }
            }
            2 => {
                if i32::from(record.color[0] as i8) <= dt {
                    record.state = 3;
                    retired += 1;
                } else {
                    record.color = [record.color[0].wrapping_sub(dt as u8); 3];
                }
            }
            3 => retired += 1,
            _ => (),
        }
    }
    if retired == state.records.len() && surface.runtime.siren_timer == 0 {
        state.records_attached = false;
        queues
            .queue(ticks, id, false, limit)
            .map_err(Error::Lifecycle)?;
    }
    Ok(())
}
/// Binding for the shared entity method table; no duplicate record/flag state.
pub fn invoke_tick(
    surface: &mut Deformable,
    call: Invocation<'_, ObstacleId>,
    env: Environment<'_>,
    dt: i32,
) -> Result<(), i32> {
    tick(
        surface,
        dt,
        call.entity,
        call.context.ticks,
        call.context.queues,
        env.work_limit,
    )
    .map_err(code)?;
    *call.result = call.entity.0;
    Ok(())
}
/// Ordered collision and drawing memberships are distinct from tick membership.
/// Enter appends before its caller invokes the actual activation interface.
pub fn enter(
    id: ObstacleId,
    collision: &mut Vec<ObstacleId>,
    drawing: &mut Vec<ObstacleId>,
    records: &deformation::State,
    limit: u32,
) -> Result<bool, Error> {
    if limit == 0 {
        return Err(Error::InvalidLimit);
    }
    collision.push(id);
    drawing.push(id);
    Ok(records.records_attached)
}
pub struct Membership<'a> {
    pub collision: &'a mut Vec<ObstacleId>,
    pub drawing: &'a mut Vec<ObstacleId>,
}
/// Returns notification presence, without exposing a guest list-node address.
/// Node-address identity is outside this typed method interface.
pub fn leave(
    id: ObstacleId,
    entity: EntityId,
    membership: Membership<'_>,
    records: &deformation::State,
    ticks: &mut Ticks<EntityId>,
    queues: &mut Queues<EntityId>,
    limit: u32,
) -> Result<bool, Error> {
    if limit == 0 {
        return Err(Error::InvalidLimit);
    }
    spatial::remove_first(membership.collision, &id, limit).map_err(Error::Spatial)?;
    spatial::remove_first(membership.drawing, &id, limit).map_err(Error::Spatial)?;
    if records.records_attached {
        queues
            .queue(ticks, entity, false, limit)
            .map_err(Error::Lifecycle)?;
        Ok(true)
    } else {
        Ok(false)
    }
}
/// Class-owned cleanup precedes the caller's actual parent-class destructor.
/// Shared meshes survive; private geometry and live buffers are detached first.
pub fn destroy_owned(
    surface: &mut Deformable,
    meshes: &mut Meshes,
    limit: u32,
) -> Result<(), Error> {
    if limit == 0 {
        return Err(Error::InvalidLimit);
    }
    surface.vertex_flags = Vec::new();
    if surface.runtime.private_mesh && meshes.get(surface.mesh).is_some() {
        meshes.release_private(surface.mesh).ok_or(Error::Mesh)?;
    }
    surface.sampling.state.records_attached = false;
    Ok(())
}
pub fn code(error: Error) -> i32 {
    match error {
        Error::Limit
        | Error::Spatial(spatial::Error::Limit)
        | Error::Lifecycle(lifecycle::Error::Spatial(spatial::Error::Limit)) => -3,
        Error::DetachedRecords => -4,
        Error::Lifecycle(lifecycle::Error::Method(value)) => value,
        _ => -1,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{deformable_hit, scene_collision::Sampling};
    use tankracer_assets::{mesh::Geometry, model::Model};

    fn model() -> Model {
        Model {
            geometry: Geometry {
                vertices: vec![[0, 100, 0]],
                triangles: vec![],
            },
            materials: vec![],
            radius: 0,
            render_hint: 0,
            bounds: [[0; 3]; 2],
            vertex_normals: None,
        }
    }
    fn surface(mesh: crate::scene_meshes::MeshId) -> Deformable {
        Deformable {
            config: deformable_hit::Config {
                floor: 50,
                compression: 1,
                drift_shift: 1,
                material: 0,
                has_sound: false,
            },
            state: Default::default(),
            mesh,
            vertex_flags: vec![4],
            runtime: State::default(),
            sampling: Sampling {
                bounds: [[-1000, 1000]; 2],
                edge_cap: 45000,
                state: deformation::State {
                    records_attached: true,
                    dirty: 1,
                    records: vec![deformation::Record {
                        state: 2,
                        color: [127, 3, 9],
                        ..Default::default()
                    }],
                    ..Default::default()
                },
            },
        }
    }
    #[test]
    fn siren_threshold_and_retirement_keep_declared_records_but_disable_activation() {
        let mut meshes = Meshes::default();
        let mesh = meshes.bind("car.sm", &model());
        let mut surface = surface(mesh);
        surface.runtime.siren_timer = 11026;
        let mut ticks = Ticks {
            flags: vec![0; 2],
            ..Default::default()
        };
        let mut queues = Queues::default();
        tick(&mut surface, 1500, EntityId(1), &mut ticks, &mut queues, 10).unwrap();
        assert_eq!(surface.runtime.siren_timer, -974);
        assert!(surface.sampling.state.records_attached);
        assert_eq!(surface.sampling.state.records[0].state, 3);
        tick(&mut surface, 16, EntityId(1), &mut ticks, &mut queues, 10).unwrap();
        assert!(!surface.sampling.state.records_attached);
        assert_eq!(surface.sampling.state.records.len(), 1);
        assert_eq!(queues.notifications, [EntityId(1)]);
        // Hidden retained data must stay inaccessible even if it could activate.
        surface.sampling.state.records[0].state = 0;
        let sampled = surface.sampling.state.sample(
            &deformation::Geometry {
                sample_bounds: [[-1000, 1000]; 2],
                edges: [-16000, 16000, -16000, 16000],
                edge_cap: 45000,
                vertices: &meshes.get(mesh).unwrap().vertices,
            },
            [0; 3],
        );
        assert_eq!(sampled.vertex_height, 100);
        assert_eq!(sampled.activated_records, 0);
        assert_eq!(surface.sampling.state.records[0].state, 0);
    }
    #[test]
    fn private_cleanup_preserves_shared_geometry_and_invalid_limit_writes_nothing() {
        let mut meshes = Meshes::default();
        let shared = meshes.bind("car.sm", &model());
        let private = meshes.private_copy(shared).unwrap();
        let mut surface = surface(private);
        surface.runtime.private_mesh = true;
        assert_eq!(
            destroy_owned(&mut surface, &mut meshes, 0),
            Err(Error::InvalidLimit)
        );
        assert!(!surface.vertex_flags.is_empty());
        assert!(meshes.get(private).is_some());
        destroy_owned(&mut surface, &mut meshes, 1).unwrap();
        assert!(surface.vertex_flags.is_empty());
        assert!(!surface.sampling.state.records_attached);
        assert!(meshes.get(private).is_none());
        assert!(meshes.get(shared).is_some());
        assert_eq!(meshes.release_private(shared), None);
        assert!(meshes.get(shared).is_some());
    }
}
