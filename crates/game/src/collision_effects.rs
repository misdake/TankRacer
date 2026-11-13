//! One real particle/damage implementation shared by tank, ground and object hits.
use crate::{
    damage, dispatch::MethodId, effect_pool::Pool, ground_collision, impact_particles,
    object_query::TankId, obstacle_collision, tank_collision,
};

/// Borrowed per-frame bindings to independently owned game components. The
/// impact vehicle's cooldown is persistent; its pose/contact fields are views
/// refreshed from authoritative dynamics before collision processing.
pub struct Bank<'a, 'mesh> {
    pub particles: &'a mut impact_particles::State,
    pub lines: &'a mut Pool,
    pub vehicles: &'a mut [impact_particles::Vehicle],
    pub damage: &'a mut [damage::Target<'mesh>],
    pub corners: &'a mut [[u32; 4]],
    pub frame: u32,
    pub tuning: impact_particles::Tuning,
}
impl Bank<'_, '_> {
    pub fn impact(
        &mut self,
        tank: TankId,
        contact: usize,
        kind: i32,
        random: &mut u32,
    ) -> Result<(), i32> {
        let corner = *self
            .corners
            .get(tank.0)
            .and_then(|c| c.get(contact))
            .ok_or(-1)?;
        let vehicle = self.vehicles.get_mut(tank.0).ok_or(-1)?;
        self.particles
            .emit(
                vehicle,
                corner,
                kind,
                &mut impact_particles::Context {
                    frame: self.frame,
                    random,
                    pool: self.lines,
                    tuning: self.tuning,
                },
            )
            .map(|_| ())
            .map_err(impact_particles::Error::status)
    }
    pub fn track_damage(&mut self, tank: TankId, kind: i32, point: [i32; 3]) -> Result<(), i32> {
        self.damage
            .get_mut(tank.0)
            .ok_or(-1)?
            .track(kind, point)
            .map_err(|_| -1)
    }
}

pub struct TankEffects<'a, 'bank, 'mesh> {
    pub bank: &'a mut Bank<'bank, 'mesh>,
    pub random: &'a mut u32,
    pub own: TankId,
}
impl tank_collision::Effects for TankEffects<'_, '_, '_> {
    fn impact(&mut self, tank: TankId, contact: usize, kind: i32) -> Result<(), i32> {
        self.bank.impact(tank, contact, kind, self.random)
    }
    fn track_damage(&mut self, tank: TankId, kind: i32, point: [i32; 3]) -> Result<(), i32> {
        self.bank.track_damage(tank, kind, point)
    }
}
impl ground_collision::Effects for TankEffects<'_, '_, '_> {
    fn impact(&mut self, contact: usize, kind: i32) -> Result<(), i32> {
        self.bank.impact(self.own, contact, kind, self.random)
    }
    fn track_damage(&mut self, kind: i32, point: [i32; 3]) -> Result<(), i32> {
        self.bank.track_damage(self.own, kind, point)
    }
}

/// Class methods and device feedback are supplied by the scene owner. No
/// collision, particle, damage or RNG operation is replaced by a notification.
pub trait ObjectServices {
    fn method(
        &mut self,
        method: MethodId,
        call: obstacle_collision::Invocation<'_>,
        effects: &mut Bank<'_, '_>,
    ) -> Result<(), i32>;
    fn feedback(
        &mut self,
        intensity: i32,
        vehicle: &mut obstacle_collision::State,
        random: &mut u32,
    ) -> Result<(), i32>;
}
pub struct ObjectEffects<'a, 'bank, 'mesh, S> {
    pub bank: &'a mut Bank<'bank, 'mesh>,
    pub services: &'a mut S,
    pub own: TankId,
}
impl<S: ObjectServices> obstacle_collision::Effects for ObjectEffects<'_, '_, '_, S> {
    fn method(
        &mut self,
        method: MethodId,
        call: obstacle_collision::Invocation<'_>,
    ) -> Result<(), i32> {
        self.services.method(method, call, self.bank)
    }
    fn feedback(
        &mut self,
        intensity: i32,
        vehicle: &mut obstacle_collision::State,
        random: &mut u32,
    ) -> Result<(), i32> {
        self.services.feedback(intensity, vehicle, random)
    }
    fn impact(
        &mut self,
        vehicle: &mut obstacle_collision::State,
        contact: usize,
        kind: i32,
        random: &mut u32,
    ) -> Result<(), i32> {
        let view = self.bank.vehicles.get_mut(self.own.0).ok_or(-1)?;
        view.entity = vehicle.entity;
        view.contacts = vehicle.contacts;
        view.forward_velocity = vehicle.forward_velocity;
        self.bank.impact(self.own, contact, kind, random)
    }
}
