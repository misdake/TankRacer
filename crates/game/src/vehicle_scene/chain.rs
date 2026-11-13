//! Complete active/inactive after-pose ordering for single-player, single view.
//! Inputs are borrowed from dynamics before motion's display rescaling.
use super::{debris, lights, marks, pickup, shield, smoke, spray, temporary};
use crate::{collision_effects, impact_particles, vehicle};

pub struct Local<'a> {
    pub spray: &'a mut spray::State,
    pub marks: &'a mut marks::State,
    pub smoke: &'a mut smoke::State,
    pub lights: &'a mut lights::State,
    pub temporary_timer: i16,
    pub temporary_slot: i16,
}
pub struct World<'a, 'owner> {
    pub own: usize,
    pub debris: &'a mut debris::State,
    pub debris_owners: &'a mut [debris::Owner<'owner>],
    pub pickup: &'a mut pickup::State,
    pub temporary: &'a mut temporary::State,
    pub shield: &'a mut shield::State,
    pub debris_sound: &'a mut Option<u32>,
}
pub struct Tuning<'a> {
    pub spray: &'a spray::Tuning,
    pub smoke: &'a smoke::Tuning,
    pub pickup: &'a pickup::Tuning,
}
#[derive(Debug, Clone, Copy)]
pub struct Settings {
    pub special: bool,
    pub lights: lights::Settings,
    pub light_offset: [i32; 3],
    /// Primary camera in world units, followed by its Q12 forward basis row.
    pub camera: ([i32; 3], [i32; 3]),
}
fn finish(
    v: &vehicle::AfterPose<'_>,
    local: &mut Local<'_>,
    world: &mut World<'_, '_>,
    effects: &mut collision_effects::Bank<'_, '_>,
    random: &mut u32,
    settings: Settings,
) -> Result<(), i32> {
    local.lights.step(
        &lights::Input::after_pose(v, settings.lights, settings.light_offset),
        effects.lines,
    )?;
    world.temporary.tick(
        local.temporary_timer,
        local.temporary_slot,
        v.motion.poses.base_position,
        temporary::Context {
            pool: effects.lines,
            random,
        },
    )?;
    world
        .shield
        .tick(shield::Input::after_pose(v, None), random)
}
fn tick_impacts(
    v: &vehicle::AfterPose<'_>,
    own: usize,
    effects: &mut collision_effects::Bank<'_, '_>,
    random: &mut u32,
) -> Result<(), i32> {
    effects
        .particles
        .tick(
            effects.vehicles.get_mut(own).ok_or(-1)?,
            v.frame.dt,
            &mut impact_particles::Context {
                frame: v.frame.number,
                random,
                pool: effects.lines,
                tuning: effects.tuning,
            },
        )
        .map_err(impact_particles::Error::status)
}
/// Scene identity/impact views must already be refreshed from this newly
/// computed pose (vehicle::step_physics does so). No operation is deferred.
/// Multiplayer/split-view scheduling is outside this single-player contract.
pub fn active(
    v: vehicle::AfterPose<'_>,
    mut local: Local<'_>,
    mut world: World<'_, '_>,
    effects: &mut collision_effects::Bank<'_, '_>,
    random: &mut u32,
    settings: Settings,
    tuning: Tuning<'_>,
) -> Result<(), i32> {
    if !settings.special || !v.player {
        local.spray.step(
            &spray::Input::after_pose(&v, settings.lights.visible),
            tuning.spray,
        )?;
        local
            .marks
            .step(&marks::Input::after_pose(&v), effects.lines)?;
    }
    world.pickup.fade(
        v.entity,
        v.motion.poses.base_position,
        v.frame.number,
        v.frame.dt,
        tuning.pickup,
        effects.lines,
    )?;
    tick_impacts(&v, world.own, effects, random)?;
    local.smoke.step(
        &smoke::Input::after_pose(&v, settings.special),
        tuning.smoke,
        effects.particles,
        effects.vehicles.get_mut(world.own).ok_or(-1)?,
        &mut impact_particles::Context {
            frame: v.frame.number,
            random,
            pool: effects.lines,
            tuning: effects.tuning,
        },
    )?;
    let input = debris::Input::after_pose(&v, settings.camera);
    world.debris.step(
        world.debris_owners,
        world.own,
        &input,
        &mut debris::Context {
            kinds: &tuning.spray.kinds,
            random,
            impact_accumulator: &mut v.collision.impact_accumulator,
            sound_draw: world.debris_sound,
        },
    )?;
    finish(&v, &mut local, &mut world, effects, random, settings)
}
/// Used after the authoritative inactive dynamics path. Clearing particles is
/// count-gated: old payload is retained when the count already says zero.
pub fn inactive(
    v: vehicle::AfterPose<'_>,
    local: Local<'_>,
    world: World<'_, '_>,
    effects: &mut collision_effects::Bank<'_, '_>,
    random: &mut u32,
    settings: Settings,
    tuning: Tuning<'_>,
) -> Result<(), i32> {
    if local.spray.count != 0 {
        for p in &mut local.spray.particles {
            p.life = 0;
        }
        local.spray.count = 0;
    }
    world.pickup.fade(
        v.entity,
        v.motion.poses.base_position,
        v.frame.number,
        v.frame.dt,
        tuning.pickup,
        effects.lines,
    )?;
    local.lights.step(
        &lights::Input::after_pose(&v, settings.lights, settings.light_offset),
        effects.lines,
    )?;
    world.temporary.tick(
        local.temporary_timer,
        local.temporary_slot,
        v.motion.poses.base_position,
        temporary::Context {
            pool: effects.lines,
            random,
        },
    )?;
    world.debris.clear(
        v.player,
        world.debris_owners.get_mut(world.own).ok_or(-1)?.state,
    );
    tick_impacts(&v, world.own, effects, random)?;
    if local.smoke.count != 0 {
        for p in &mut local.smoke.particles {
            p.life = 0;
        }
        local.smoke.count = 0;
    }
    world
        .shield
        .tick(shield::Input::after_pose(&v, None), random)
}
