use tankracer_game::{body, contact::Coefficient, math::Matrix, motion, pose};

pub struct Reader {
    pub words: Vec<u32>,
    pub at: usize,
}
impl Reader {
    pub fn word(&mut self) -> u32 {
        let v = self.words[self.at];
        self.at += 1;
        v
    }
    pub fn signed(&mut self) -> i32 {
        self.word() as i32
    }
    pub fn vector<const N: usize>(&mut self) -> [i32; N] {
        std::array::from_fn(|_| self.signed())
    }
    pub fn pose(&mut self) -> pose::Pose {
        pose::Pose {
            position: self.vector(),
            matrix: Matrix(self.vector()),
        }
    }
    pub fn spring(&mut self) -> body::Spring {
        body::Spring {
            position: self.signed(),
            rate: self.signed(),
        }
    }
    pub fn coefficient(&mut self) -> Coefficient {
        Coefficient {
            multiplier: self.word() as u16,
            shift: self.word() as u8,
        }
    }
    pub fn state(&mut self) -> motion::State {
        let poses = pose::State {
            physical_position: self.vector(),
            reference: Matrix(self.vector()),
            base_position: self.vector(),
            body: self.pose(),
            tracks: [self.pose(), self.pose()],
            turret: self.pose(),
            gun: self.pose(),
            attachments: [self.pose(), self.pose()],
            bottom_position: self.vector(),
            recoil_factor: self.word() as u8,
            recoil_shift: self.word() as u8,
        };
        let previous_position = self.vector();
        let velocity = self.vector();
        let angular = self.vector();
        let acceleration = self.vector();
        let pivot = self.vector();
        let articulation = body::Articulation {
            recoil: self.signed(),
            recoil_rate: self.signed(),
            pitch: self.signed(),
            pitch_rate: self.signed(),
        };
        let lean = body::Lean {
            lateral: self.spring(),
            longitudinal: self.spring(),
        };
        let contacts = std::array::from_fn(|_| body::Contact {
            world: self.vector(),
            gap: self.signed(),
        });
        let travel = self.vector();
        let hull = body::Hull {
            position: self.vector(),
            matrix: Matrix(self.vector()),
            vertices: std::array::from_fn(|_| self.vector().map(|v| v as i16)),
            radius: self.word() as u16,
        };
        let ready = self.word() != 0;
        let rates = motion::Rates {
            travel_sum: self.signed(),
            penetrating_force: self.signed(),
            lateral_velocity: self.signed(),
            track_slip: self.signed(),
        };
        motion::State {
            poses,
            previous_position,
            velocity,
            angular,
            acceleration,
            pivot,
            articulation,
            lean,
            contacts,
            travel,
            hull,
            ready,
            rates,
        }
    }
    pub fn vertices(&mut self, count: usize) -> Vec<[i16; 3]> {
        (0..count)
            .map(|_| self.vector().map(|v| v as i16))
            .collect()
    }
}
fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
pub fn words(s: &motion::State) -> Vec<u32> {
    let p = &s.poses;
    let mut out = Vec::new();
    append(&mut out, p.physical_position);
    append(&mut out, p.reference.0);
    append(&mut out, p.base_position);
    for p in [
        p.body,
        p.tracks[0],
        p.tracks[1],
        p.turret,
        p.gun,
        p.attachments[0],
        p.attachments[1],
    ] {
        append(&mut out, p.position);
        append(&mut out, p.matrix.0);
    }
    append(&mut out, p.bottom_position);
    out.extend([u32::from(p.recoil_factor), u32::from(p.recoil_shift)]);
    append(&mut out, s.previous_position);
    append(&mut out, s.velocity);
    append(&mut out, s.angular);
    append(&mut out, s.acceleration);
    append(&mut out, s.pivot);
    append(
        &mut out,
        [
            s.articulation.recoil,
            s.articulation.recoil_rate,
            s.articulation.pitch,
            s.articulation.pitch_rate,
        ],
    );
    for spring in [s.lean.lateral, s.lean.longitudinal] {
        append(&mut out, [spring.position, spring.rate]);
    }
    for c in s.contacts {
        append(&mut out, c.world);
        out.push(c.gap as u32);
    }
    append(&mut out, s.travel);
    append(&mut out, s.hull.position);
    append(&mut out, s.hull.matrix.0);
    for v in s.hull.vertices {
        append(&mut out, v.map(i32::from));
    }
    out.extend([u32::from(s.hull.radius), u32::from(s.ready)]);
    append(
        &mut out,
        [
            s.rates.travel_sum,
            s.rates.penetrating_force,
            s.rates.lateral_velocity,
            s.rates.track_slip,
        ],
    );
    assert_eq!(out.len(), 176);
    out
}
