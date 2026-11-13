//! Bidirectional semantic test codec. Native addresses stay in the C oracle.
use super::common::Reader;
use tankracer_game::{
    effect_pool::{Pool, Quad},
    object_query::EntityId,
    vehicle_scene::{debris, lights, marks, pickup, shield, smoke, spray, temporary},
};

#[derive(Default)]
pub struct Local {
    pub spray: spray::State,
    pub smoke: smoke::State,
    pub lights: lights::State,
    pub marks: marks::State,
    pub debris: debris::Vehicle,
    pub temporary_slot: i16,
}
#[derive(Default)]
pub struct State {
    pub local: Vec<Local>,
    pub debris: debris::State,
    pub pickup: pickup::State,
    pub temporary: temporary::State,
    pub shield: shield::State,
}
enum Stream<'a> {
    Read(&'a mut Reader),
    Write(Vec<u32>),
}
impl Stream<'_> {
    fn value(&mut self, v: u32) -> u32 {
        match self {
            Self::Read(r) => r.word(),
            Self::Write(out) => {
                out.push(v);
                v
            }
        }
    }
    fn word(&mut self, v: &mut u32) {
        *v = self.value(*v);
    }
    fn signed(&mut self, v: &mut i32) {
        *v = self.value(*v as u32) as i32;
    }
    fn byte(&mut self, v: &mut u8) {
        *v = self.value(u32::from(*v)) as u8;
    }
    fn half(&mut self, v: &mut u16) {
        *v = self.value(u32::from(*v)) as u16;
    }
    fn ints(&mut self, v: &mut [i32]) {
        for v in v {
            self.signed(v);
        }
    }
    fn bytes(&mut self, v: &mut [u8]) {
        for v in v {
            self.byte(v);
        }
    }
    fn entity(&mut self, v: &mut Option<EntityId>) {
        *v = match self.value(v.map_or(0, |e| e.0)) {
            0 => None,
            n => Some(EntityId(n)),
        };
    }
    fn quad(&mut self, q: &mut Quad) {
        for p in &mut q.points {
            self.ints(p);
        }
        self.word(&mut q.material);
        for b in q.style.chunks_exact_mut(4) {
            let value = self.value(u32::from_le_bytes((&*b).try_into().unwrap()));
            b.copy_from_slice(&value.to_le_bytes());
        }
    }
}
impl State {
    pub fn read(r: &mut Reader, n: usize, pool: &mut Pool) -> Self {
        let mut s = Self {
            local: (0..n).map(|_| Local::default()).collect(),
            ..Self::default()
        };
        s.visit(&mut Stream::Read(r), pool);
        s
    }
    pub fn words(&mut self, pool: &mut Pool) -> Vec<u32> {
        let mut stream = Stream::Write(Vec::new());
        self.visit(&mut stream, pool);
        match stream {
            Stream::Write(v) => v,
            _ => unreachable!(),
        }
    }
    fn visit(&mut self, c: &mut Stream<'_>, pool: &mut Pool) {
        for l in &mut self.local {
            for p in &mut l.spray.particles {
                c.ints(&mut p.position);
                c.ints(&mut p.velocity);
                p.life = c.value(p.life as u16 as u32) as i16;
                c.half(&mut p.size);
                p.floor = c.value(p.floor as u16 as u32) as i16;
                c.half(&mut p.rotation);
                p.kind = c.value(p.kind as u8 as u32) as i8;
                c.byte(&mut p.side);
            }
            c.signed(&mut l.spray.timer);
            c.word(&mut l.spray.side);
            c.word(&mut l.spray.count);
            for p in &mut l.smoke.particles {
                c.ints(&mut p.position);
                c.ints(&mut p.velocity);
                p.life = c.value(p.life as u16 as u32) as i16;
                c.half(&mut p.size);
                c.half(&mut p.rotation);
                c.byte(&mut p.direction);
                c.byte(&mut p.expanding);
            }
            c.signed(&mut l.smoke.timer);
            c.word(&mut l.smoke.side);
            c.signed(&mut l.smoke.force);
            c.word(&mut l.smoke.count);
            c.bytes(&mut l.smoke.damage);
            c.bytes(&mut l.smoke.sockets);
            c.ints(&mut l.smoke.damage_timers);
            c.byte(&mut l.lights.brake_mode);
            c.bytes(&mut l.lights.active);
            for h in &mut l.lights.handles {
                c.word(&mut h.0);
            }
            c.bytes(&mut l.marks.active);
            for side in &mut l.marks.endpoints {
                for p in side {
                    c.ints(p);
                }
            }
            c.ints(&mut l.marks.movement);
            for h in &mut l.marks.handles {
                c.word(&mut h.0);
            }
            c.word(&mut l.marks.count);
            c.word(&mut l.marks.head);
            c.word(&mut l.marks.tail);
            c.word(&mut l.debris.active);
            c.signed(&mut l.debris.timer);
            c.byte(&mut l.debris.previous_plane);
            l.temporary_slot = c.value(l.temporary_slot as u16 as u32) as i16;
        }
        for b in &mut self.debris.banks {
            for p in &mut b.particles {
                c.ints(&mut p.velocity);
                c.ints(&mut p.position);
                c.bytes(&mut p.color);
                c.byte(&mut p.active);
                c.word(&mut p.size);
            }
            c.signed(&mut b.floor);
            c.entity(&mut b.owner);
        }
        for b in &mut self.pickup.banks {
            c.byte(&mut b.active);
            c.byte(&mut b.projected);
            c.bytes(&mut b.follow);
            c.entity(&mut b.target);
            c.signed(&mut b.life);
            c.word(&mut b.frame);
            c.ints(&mut b.origin);
            c.ints(&mut b.position);
            c.signed(&mut b.speed);
            c.signed(&mut b.inner_radius);
            c.signed(&mut b.outer_radius);
            c.signed(&mut b.flare_radius);
            for r in &mut b.rays {
                for a in &mut r.angles {
                    c.half(a);
                }
                c.ints(&mut r.angular_velocity);
                c.word(&mut r.palette_index);
                c.word(&mut r.handle.0);
                c.quad(&mut r.quad);
            }
            for r in &mut b.rings {
                c.quad(&mut r.quad);
                c.word(&mut r.handle.0);
            }
        }
        c.word(&mut self.temporary.available);
        for i in 0..4 {
            c.word(&mut self.temporary.timestamps[i]);
            for j in 0..3 {
                let r = &mut self.temporary.rows[i][j];
                c.ints(&mut r.first);
                c.ints(&mut r.last);
                r.colors = c.value(u32::from_le_bytes(r.colors)).to_le_bytes();
                let phases = c.value(u32::from(r.phases[0]) | (u32::from(r.phases[1]) << 16));
                r.phases = [phases as u16, (phases >> 16) as u16];
                for h in &mut self.temporary.lines[i][j * 3..j * 3 + 3] {
                    c.word(&mut h.0);
                }
            }
        }
        for (i, b) in self.shield.banks.iter_mut().enumerate() {
            for o in &mut b.orbits {
                c.signed(&mut o.pitch);
                c.signed(&mut o.pitch_target);
                c.signed(&mut o.yaw);
                c.signed(&mut o.yaw_rate);
                c.signed(&mut o.yaw_rate_target);
                c.signed(&mut o.size);
                c.byte(&mut o.growing);
                c.half(&mut o.spin);
                c.ints(&mut o.position);
            }
            if i > 1 {
                c.word(&mut b.tail);
            }
        }
        for q in &mut pool.quads {
            c.quad(q);
        }
        c.quad(&mut pool.exhausted_quad);
    }
}
