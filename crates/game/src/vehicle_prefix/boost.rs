//! Speed pickup and its synchronous camera/application sequence (43F720).
use crate::{object_query::EntityId, random};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct State {
    pub extra_speed: i32,
    pub power: i32,
    pub power_loss: i32,
    pub timer: i16,
    pub camera_velocity: i32,
    pub camera_timer: i32,
    pub target: EntityId,
    pub active: u8,
    pub applied: u8,
    pub phase: u8,
}
impl Default for State {
    fn default() -> Self {
        Self {
            extra_speed: 0,
            power: 0,
            power_loss: 0,
            timer: 0,
            camera_velocity: 0,
            camera_timer: 0,
            target: EntityId(0),
            active: 0,
            applied: 0,
            phase: 0,
        }
    }
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Camera {
    pub pitch: i32,
    pub height: i32,
    pub distance: i32,
}
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Shared {
    pub texture_frame: u32,
    pub texture_delta: u16,
    pub source_uv: [u16; 2],
    pub current_uv: [u16; 2],
    pub feedback: [i32; 3],
}
#[derive(Debug, Clone, Copy)]
pub struct Environment {
    pub dt: i32,
    pub frame: u32,
    pub player: bool,
    pub camera_preset: bool,
    pub camera_variant: bool,
    pub vehicle_mode: i16,
}
impl State {
    pub fn start(&mut self, target: EntityId, camera_preset: bool, sort: i16, shared: &mut Shared) {
        self.camera_velocity = 0;
        self.camera_timer = if camera_preset { 1000 } else { 0 };
        self.target = target;
        self.phase = 1;
        self.applied = 0;
        self.active = 1;
        shared.feedback = [i32::from(sort), 1300, 110];
    }
    /// The resolved target method (437BF0), after class dispatch selected it.
    pub fn apply(&mut self, increment: i32, power: i32, loss: i32) {
        self.extra_speed = self.extra_speed.wrapping_add(increment);
        self.timer = 5000;
        self.power = power;
        self.power_loss = loss;
        if self.extra_speed > 500000 {
            self.extra_speed = 500000;
        }
    }
    pub fn decay(&mut self, dt: i32, player: bool) {
        if self.timer > 0 {
            self.timer = self.timer.wrapping_sub(dt as i16);
            self.power = self
                .power
                .wrapping_sub(self.power_loss.wrapping_mul(dt))
                .max(0);
            if self.timer < 1 && (!player || self.phase == 8) {
                self.extra_speed = 0;
                self.timer = 0;
                self.power = 0;
                self.active = 0;
            }
        }
    }
    pub fn step(
        &mut self,
        env: Environment,
        camera: &mut Camera,
        shared: &mut Shared,
        seed: &mut u32,
        mut apply: impl FnMut(&mut Self, EntityId) -> Result<(), i32>,
    ) -> Result<(), i32> {
        if !env.player {
            if self.applied == 0 {
                self.applied = 1;
                apply(self, self.target)?;
            }
        } else if self.phase & 8 == 0 && env.vehicle_mode != 3 {
            self.camera(env, camera, &mut apply)?;
        }
        if self.applied == 0 {
            return Ok(());
        }
        if shared.texture_frame != env.frame {
            shared.texture_frame = env.frame;
            shared.texture_delta = ((random::next(seed) >> 4) & 63) as u16;
            shared.current_uv = shared
                .source_uv
                .map(|v| v.wrapping_sub(shared.texture_delta));
        }
        Ok(())
    }
    fn integrate(&self, camera: &mut Camera, dt: i32) {
        camera.distance = camera
            .distance
            .wrapping_add(dt.wrapping_mul(self.camera_velocity) >> 8);
    }
    fn camera(
        &mut self,
        env: Environment,
        camera: &mut Camera,
        apply: &mut impl FnMut(&mut Self, EntityId) -> Result<(), i32>,
    ) -> Result<(), i32> {
        let dt = env.dt;
        let variant = env.camera_variant;
        if env.camera_preset {
            if !variant && self.camera_timer != 0 {
                if self.camera_timer <= dt {
                    self.camera_timer = 0;
                    self.applied = 1;
                    apply(self, self.target)?;
                    self.phase = 8;
                } else {
                    self.camera_timer = self.camera_timer.wrapping_sub(dt);
                }
            }
            return Ok(());
        }
        if self.phase & 1 != 0 {
            if camera.distance < -7199 {
                if camera.pitch < 1 {
                    camera.pitch =
                        camera
                            .pitch
                            .wrapping_add(dt.wrapping_mul(if variant { 8 } else { 1 }));
                }
                if camera.height > if variant { 450 } else { 2000 } {
                    camera.height =
                        camera
                            .height
                            .wrapping_sub(dt.wrapping_mul(if variant { 8 } else { 1 }));
                }
                self.camera_velocity = if camera.distance < -7799 {
                    self.camera_velocity
                        .wrapping_add(dt.wrapping_mul(16))
                        .min(5000)
                } else {
                    self.camera_velocity
                        .wrapping_sub(dt.wrapping_mul(32))
                        .max(100)
                };
                self.integrate(camera, dt);
                return Ok(());
            }
            self.camera_velocity = self.camera_velocity.wrapping_sub(dt.wrapping_mul(8));
            self.integrate(camera, dt);
            self.camera_timer = self.camera_timer.wrapping_add(dt);
            if self.camera_timer >= 200 {
                if self.applied == 0 {
                    apply(self, self.target)?;
                    self.applied = 1;
                }
                if self.camera_timer >= 350 {
                    self.phase = 2;
                    self.camera_timer = 0;
                }
            }
        } else if self.phase & 2 != 0 {
            self.camera_velocity = self
                .camera_velocity
                .wrapping_sub(dt.wrapping_mul(if variant { 64 } else { 128 }))
                .max(-15000);
            if variant {
                camera.height = camera.height.wrapping_add(dt.wrapping_mul(10)).min(3000);
            } else {
                camera.pitch = camera.pitch.wrapping_sub(dt);
                camera.height = camera.height.min(5500);
                camera.pitch = camera.pitch.max(-1638);
            }
            self.integrate(camera, dt);
            if camera.distance < if variant { -8999 } else { -8199 } {
                self.phase = 4;
            }
        } else if self.phase & 4 != 0 {
            if self.camera_velocity != 0 {
                self.camera_velocity = self
                    .camera_velocity
                    .wrapping_add(dt.wrapping_mul(128))
                    .min(0);
                self.integrate(camera, dt);
            } else {
                self.phase = 8;
            }
        }
        Ok(())
    }
}
