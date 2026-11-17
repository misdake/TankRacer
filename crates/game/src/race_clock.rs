//! frame_race_initialize / frame_race_tick: raw-time race control before entities.
//! Release does not carry countdown overshoot into either the lap timer or GO.

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Racer {
    pub elapsed: i32,
    pub laps: i16,
    pub driving: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Clock {
    pub countdown: i32,
    pub release: i32,
    pub display: i32,
}
impl Default for Clock {
    fn default() -> Self {
        Self {
            countdown: 3000,
            release: 0,
            display: 1,
        }
    }
}
impl Clock {
    pub fn tick<'a>(&mut self, elapsed: i32, racers: impl IntoIterator<Item = &'a mut Racer>) {
        if self.countdown == 0 {
            if self.release != 0 {
                self.release = self.release.wrapping_sub(elapsed).max(0);
            }
            for racer in racers {
                racer.elapsed = racer.elapsed.wrapping_add(elapsed);
            }
            return;
        }
        self.countdown = self.countdown.wrapping_sub(elapsed);
        if self.countdown < 1 {
            self.countdown = 0;
            self.release = 1000;
            self.display = 4;
            for racer in racers {
                racer.driving = true;
            }
        } else {
            self.display = if self.countdown > 1500 {
                1
            } else {
                2 + i32::from(self.countdown < 1001)
            };
        }
    }
}

/// frame_time_physics_dt clamps physics independently of the raw race clock.
pub fn physics_dt(elapsed: i32) -> i32 {
    elapsed.clamp(10, 100)
}
