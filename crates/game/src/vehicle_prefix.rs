//! Active 430A30 prefix, before the contact/drive/collision phase.
pub mod boost;
pub mod spin;

use crate::{
    effect_pool::{LineHandle, Pool},
    math::{self, Axis, Matrix},
};
use tankracer_assets::config::Gain;

/// Rotating scenery close to the primary camera (413A80). This is shared world
/// state, not a per-player copy. It consumes no RNG.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct View {
    pub enabled: bool,
    pub visible: bool,
    pub position: [i32; 3],
    pub angular_rate: i32,
    pub matrix: Matrix,
}
impl Default for View {
    fn default() -> Self {
        Self {
            enabled: false,
            visible: false,
            position: [0xfff8ee52_u32 as i32, 0xe290, 0x1f00a],
            angular_rate: 0x1061,
            matrix: Matrix::IDENTITY,
        }
    }
}
impl View {
    pub fn step(&mut self, camera: [i32; 3], dt: i32, frame: u32) -> Result<(), math::Error> {
        let near = self.enabled
            && [0, 2].into_iter().all(|i| {
                let coordinate = self.position[i] >> 3;
                let distance = coordinate.wrapping_sub(camera[i]);
                let distance = if distance < 0 {
                    camera[i].wrapping_sub(coordinate)
                } else {
                    distance
                };
                distance < 0x4b00
            });
        if !near {
            self.visible = false;
            return Ok(());
        }
        let angle = self.angular_rate.wrapping_mul(dt) >> 8;
        if angle != 0 {
            self.matrix.rotate(Axis::Y, angle as u32);
        }
        if frame & 31 == 0 {
            self.matrix.normalize()?;
        }
        self.visible = true;
        Ok(())
    }
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Temporary {
    pub available: u32,
    pub timestamps: [u32; 4],
    pub lines: [[LineHandle; 9]; 4],
}
impl Default for Temporary {
    fn default() -> Self {
        Self {
            available: 0,
            timestamps: [0; 4],
            lines: [[LineHandle(0); 9]; 4],
        }
    }
}
impl Temporary {
    /// Releases in row/column order, including repeated and exhausted handles.
    pub fn expire(&mut self, slot: i16, pool: &mut Pool) -> Result<(), i32> {
        let handles = self.lines.get(slot as usize).ok_or(-1)?;
        for handle in handles {
            pool.release(*handle).map_err(|_| -1)?;
        }
        self.available = slot as u32;
        self.timestamps[slot as usize] = 0;
        Ok(())
    }
}
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct State {
    pub alternating_sign: i32,
    pub countdown_throttle: i32,
    pub countdown_rpm: i32,
    pub target: i32,
    pub spin: spin::State,
    pub boost: boost::State,
    pub temporary_timer: i16,
    pub temporary_slot: i16,
    pub restore_sidegrip: Gain,
    pub sidegrip: Gain,
}
impl Default for State {
    fn default() -> Self {
        Self {
            alternating_sign: 1,
            countdown_throttle: 0,
            countdown_rpm: 0,
            target: 0,
            spin: spin::State::default(),
            boost: boost::State::default(),
            temporary_timer: 0,
            temporary_slot: -1,
            restore_sidegrip: Gain::default(),
            sidegrip: Gain::default(),
        }
    }
}
#[derive(Debug, Clone, Copy)]
pub struct Frame {
    pub boost: boost::Environment,
    pub drive_active: bool,
    pub rpm_limit: i32,
    pub spin: spin::Tuning,
    pub spin_work_limit: u32,
}
pub struct World<'a> {
    pub view: &'a mut View,
    /// Original camera query coordinates are in world units, not physics Q3.
    pub camera_position: [i32; 3],
    pub camera: &'a mut boost::Camera,
    pub spin: &'a mut spin::Shared,
    pub boost: &'a mut boost::Shared,
    pub temporary: &'a mut Temporary,
    pub pool: &'a mut Pool,
    pub random: &'a mut u32,
}
impl State {
    pub fn from_model(model: &crate::vehicle_model::VehicleModel) -> Self {
        Self {
            sidegrip: model.config.tank.drive.sidegrip_fraction,
            restore_sidegrip: model.reset_sidegrip,
            ..Self::default()
        }
    }

    /// Publish directly into the qualified physics inputs. This must happen even
    /// on an error: timer/grip changes before the failing call remain committed.
    /// Independent frame/player/active values come from the authoritative tick.
    pub fn prepare_vehicle(
        &mut self,
        vehicle: &mut crate::vehicle::State,
        drive: &mut tankracer_assets::config::Drive,
        frame: &mut crate::vehicle::Frame,
        mut tuning: Frame,
        world: World<'_>,
        apply: impl FnMut(&mut boost::State, crate::object_query::EntityId) -> Result<(), i32>,
    ) -> Result<(), i32> {
        tuning.boost.dt = frame.dt;
        tuning.boost.frame = frame.number;
        tuning.boost.player = vehicle.player;
        tuning.drive_active = frame.drive.active;
        let result = self.step(&mut vehicle.contact, tuning, world, apply);
        vehicle.pose_input.timer = self.temporary_timer as u16;
        drive.sidegrip_fraction = self.sidegrip;
        frame.drive.boost_charge = self.boost.timer;
        frame.drive.boost_power = self.boost.extra_speed;
        frame.drive.extra_drive = self.boost.power;
        result
    }

    /// `apply` is synchronous target-class dispatch, not a queued notification.
    /// The selected 437BF0 binding uses `boost::State::apply`; null/overridden
    /// methods and failures remain the authoritative scene registry's decision.
    pub fn step(
        &mut self,
        contact: &mut crate::contact_pipeline::Pipeline,
        frame: Frame,
        world: World<'_>,
        apply: impl FnMut(&mut boost::State, crate::object_query::EntityId) -> Result<(), i32>,
    ) -> Result<(), i32> {
        let dt = frame.boost.dt;
        if dt <= 0 {
            return Err(-2);
        }
        if frame.boost.player {
            world
                .view
                .step(world.camera_position, dt, frame.boost.frame)
                .map_err(|_| -2)?;
        }
        self.alternating_sign = self.alternating_sign.wrapping_neg();
        if !frame.drive_active {
            let addition = (self.countdown_throttle >> 5).wrapping_mul(dt >> 1) >> 1;
            self.countdown_rpm = self
                .countdown_rpm
                .wrapping_add(addition)
                .wrapping_sub(dt.wrapping_mul(256))
                .max(0);
            let maximum = frame.rpm_limit.wrapping_sub(10);
            if self.countdown_rpm > maximum {
                self.countdown_rpm = maximum;
            }
        }
        for contact in &mut contact.sampling.auxiliary {
            contact.cached = 0;
        }
        self.target = -1;
        self.boost.decay(dt, frame.boost.player);
        if self.spin.speed > 0 {
            self.spin.step(
                dt,
                frame.spin,
                frame.spin_work_limit,
                world.spin,
                world.random,
            )?;
        }
        if self.boost.active != 0 {
            self.boost
                .step(frame.boost, world.camera, world.boost, world.random, apply)?;
        }
        let timer = i32::from(self.temporary_timer);
        if timer != 0 {
            if dt < timer {
                self.temporary_timer = timer.wrapping_sub(dt) as i16;
            } else {
                self.temporary_timer = 0;
                self.sidegrip = self.restore_sidegrip;
                world.temporary.expire(self.temporary_slot, world.pool)?;
            }
        }
        Ok(())
    }
}
