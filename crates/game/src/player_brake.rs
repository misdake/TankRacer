//! Player auxiliary brake and gradual override restoration (421240/421350).
//! Operates on authoritative drive inputs and model tuning, without shadow copies.
use crate::drive;

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct State {
    pub saved_turn: i16,
    pub reverse_braking: bool,
}
pub struct Vehicle<'a> {
    pub input: &'a mut drive::Input,
    pub max_turn: &'a mut i32,
    pub yaw_lock: &'a mut i32,
}
#[derive(Debug, Clone, Copy)]
pub struct Reference {
    /// Source surface slide multiplier is unsigned; the live override is signed.
    pub slide: u16,
    pub rotation: i16,
}
#[derive(Debug, Clone, Copy)]
pub struct Frame {
    pub dt: i32,
    pub speed: i32,
    pub mode: i32,
    pub temporary_timer: i16,
    pub down: bool,
}
impl State {
    pub fn step(&mut self, mut vehicle: Vehicle<'_>, frame: Frame, reference: Reference) {
        let input = &mut vehicle.input;
        if frame.temporary_timer == 0 {
            if frame.down && frame.speed.wrapping_abs() > 250000 {
                input.alternate_lateral = i32::from(input.alternate_lateral)
                    .wrapping_sub(frame.dt.wrapping_mul(2))
                    as i16;
                if input.alternate_lateral < 0 {
                    input.alternate_lateral = 0;
                }
                input.alternate_rotation = match frame.mode {
                    4 => 400,
                    5 => 200,
                    _ => 0,
                };
                if !input.alternate {
                    self.saved_turn = *vehicle.max_turn as i16;
                    *vehicle.max_turn = 310;
                    input.alternate = true;
                }
            } else if input.alternate {
                self.restore_fields(&mut vehicle, frame.dt, frame.mode, reference);
            }
            if frame.down {
                vehicle.input.brake = 40000;
                vehicle.input.throttle = 0;
            }
            self.reverse_braking = frame.down;
        } else if input.alternate {
            input.alternate_rotation = reference.rotation;
            input.alternate_lateral = reference.slide as i16;
            *vehicle.max_turn = i32::from(self.saved_turn);
            input.alternate = false;
            // The held/reverse-braking flag deliberately survives this branch.
        }
    }
    pub fn restore(&self, mut vehicle: Vehicle<'_>, dt: i32, mode: i32, reference: Reference) {
        self.restore_fields(&mut vehicle, dt, mode, reference);
    }
    fn restore_fields(&self, vehicle: &mut Vehicle<'_>, dt: i32, mode: i32, reference: Reference) {
        let input = &mut vehicle.input;
        if i32::from(input.alternate_lateral) != i32::from(reference.slide) {
            input.alternate_lateral =
                i32::from(input.alternate_lateral).wrapping_add(dt.wrapping_mul(2)) as i16;
            if i32::from(input.alternate_lateral) > i32::from(reference.slide) {
                input.alternate_lateral = reference.slide as i16;
            }
        }
        let current = input.alternate_rotation;
        let target = reference.rotation;
        let delta = (dt >> 2) as i16;
        if current < target {
            input.alternate_rotation = current.wrapping_add(delta);
            if input.alternate_rotation > target {
                input.alternate_rotation = target;
            }
        } else if current > target {
            input.alternate_rotation = current.wrapping_sub(delta);
            if input.alternate_rotation < target {
                input.alternate_rotation = target;
            }
        }
        let target = i32::from(self.saved_turn);
        if *vehicle.max_turn != target {
            *vehicle.max_turn = vehicle.max_turn.wrapping_sub(dt.wrapping_mul(4));
            if *vehicle.max_turn < target {
                *vehicle.max_turn = target;
            }
        }
        if i32::from(input.alternate_lateral) == i32::from(reference.slide)
            && input.alternate_rotation == reference.rotation
            && *vehicle.max_turn == target
        {
            input.alternate = false;
            if mode == 4 {
                *vehicle.yaw_lock = 2000;
            }
        }
    }
}
