//! Background target resolution follows route linkage, before startup queue drain.
use super::*;
use crate::{scene_entity::Handle, scene_loading, vehicle_runtime::Startup};
impl Store {
    pub(crate) fn link_backgrounds(&mut self, startup: &mut Startup) -> Result<(), Box<dyn Error>> {
        for (id, actor) in &mut self.entries {
            let Actor::Background {
                kind: 1,
                target: Some(name),
                linked_target,
                ..
            } = actor
            else {
                continue;
            };
            let symbol = startup
                .identity
                .symbols
                .intern(name)
                .map_err(|e| format!("Background symbol: {e:?}"))?;
            let entity = startup
                .identity
                .symbols
                .entity(symbol)
                .ok_or_else(|| format!("Unbound background target {name}"))?;
            let target = startup
                .identity
                .entities
                .record(entity)
                .and_then(|r| r.object)
                .and_then(Handle::obstacle)
                .ok_or("Background target is not a world object")?;
            *linked_target = Some(entity);
            startup.objects.world.colliders[id.0].pose.position =
                startup.objects.world.colliders[target.0].pose.position;
            let own_lap = startup.objects.components.objects[id.0]
                .as_ref()
                .ok_or("Missing background")?
                .data
                .visibility
                .add_lap;
            let target_lap = startup.objects.components.objects[target.0]
                .as_ref()
                .ok_or("Missing background target")?
                .data
                .visibility
                .add_lap;
            if target_lap != 0 && own_lap < target_lap {
                startup
                    .identity
                    .context()
                    .dispatch(
                        entity,
                        scene_loading::ENTER,
                        None,
                        startup.registry.environment(100000),
                        &mut startup.objects,
                    )
                    .map_err(|e| format!("Background target enter: {e:?}"))?;
                crate::spatial::remove_first(
                    &mut startup.objects.components.lap_objects,
                    &target,
                    100000,
                )
                .map_err(|e| format!("Background lap list: {e:?}"))?;
                startup.objects.components.objects[target.0]
                    .as_mut()
                    .ok_or("Missing linked object")?
                    .data
                    .visibility
                    .entered = true;
            }
        }
        Ok(())
    }
}
