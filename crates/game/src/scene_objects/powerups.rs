//! Powerup phases execute against the same actor, collider, grid and factory.
use super::*;
use crate::{actor_startup::Actor, powerup};
impl View<'_> {
    pub(super) fn tick_powerup(
        &mut self,
        id: ObstacleId,
        mut call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        let player = self
            .vehicles
            .ok_or(-5)?
            .entries
            .first()
            .and_then(Option::as_ref)
            .ok_or(-4)?
            .physics
            .motion
            .poses
            .body
            .position;
        let geometry = *self.components.actors.pickup_geometry();
        let object = self.components.object(id)?;
        let frame = powerup::Frame {
            dt: self.components.dt,
            number: self.components.frame_group,
            entity: call.entity.0,
            player,
            camera: self.components.camera_position,
            visibility: object.data.visibility.distance,
            auxiliary_active: object.auxiliary_active != 0,
            separation: geometry.separation,
            diameter: geometry.diameter,
        };
        let mut pose = self.world.colliders.get(id.0).ok_or(-1)?.pose;
        let mut flags = self.world.objects.get(id.0).ok_or(-1)?.flags;
        let Actor::Powerup(pickup) = self.components.actors.get_mut(id).ok_or(-1)? else {
            return Err(-1);
        };
        let result = pickup.phase(
            &mut pose,
            &mut flags,
            frame,
            self.components.terrain.as_deref().ok_or(-1)?,
        );
        self.world.colliders[id.0].pose = pose;
        self.world.colliders[id.0].external_basis = Some(pose.matrix);
        self.world.objects[id.0].flags = flags;
        match result? {
            powerup::Action::None => (),
            powerup::Action::Activate => {
                self.components.special.push(id);
                self.insert_grid(id)?;
            }
            powerup::Action::Auxiliary => {
                call.context
                    .dispatch(
                        call.entity,
                        crate::scene_loading::AUXILIARY_UPDATE,
                        None,
                        env,
                        self,
                    )
                    .map_err(dispatch_code)?;
            }
        }
        let Actor::Powerup(pickup) = self.components.actors.get_mut(id).ok_or(-1)? else {
            return Err(-1);
        };
        pickup.finish_frame(&mut self.world.objects[id.0].flags, frame.dt);
        *call.result = call.entity.0;
        Ok(())
    }
}
