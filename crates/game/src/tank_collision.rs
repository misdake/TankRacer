//! Ordered tank overlap/separation and query-to-response dispatch (42D9A0/42CE20).
use crate::{
    collision::{self, Trajectory},
    geometry::approximate_distance,
    math,
    object_query::{self, Hit, Tank, TankId},
    vector,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct State {
    pub velocity: [i32; 3],
    pub local_velocity: [i32; 3],
    pub previous_position: [i32; 3],
    pub collision_timer: i32,
    pub player: bool,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PeerSnapshot {
    pub tank: TankId,
    pub velocity_xz: [i32; 2],
}
#[derive(Debug, Clone, Copy)]
pub struct Environment {
    pub mode: i32,
    pub dt: i32,
    pub separation_limit: u32,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Query(object_query::Error),
    MissingTank(TankId),
    AliasedPair(TankId),
    Limit,
    Effect(i32),
}

/// Required, synchronous shared-world operations. Impact updates particles/RNG;
/// track damage updates wear and the player body mesh before returning.
pub trait Effects {
    fn impact(&mut self, tank: TankId, contact: usize, kind: i32) -> Result<(), i32>;
    fn track_damage(&mut self, tank: TankId, kind: i32, world: [i32; 3]) -> Result<(), i32>;
}
pub struct Pair<'a> {
    pub ids: [TankId; 2],
    pub geometry: [&'a Tank; 2],
    pub states: [&'a mut State; 2],
}
// Source body_reference_vectors, frame_vehicle_defaults.h: magnitude 1024,
// not the 4096 unit vectors used by wall reflection.
const NORMALS: [[i32; 3]; 4] = [[-1024, 0, 0], [1024, 0, 0], [0, 0, 1024], [0, 0, -1024]];
fn facing(a: &Tank, b: &Tank) -> (u32, [i32; 3]) {
    let mut direction = a
        .collider
        .pose
        .matrix
        .world_to_local(std::array::from_fn(|i| {
            b.collider.pose.position[i].wrapping_sub(a.collider.pose.position[i])
        }));
    vector::normalize(&mut direction);
    let code = collision::direction(direction[0], direction[2]);
    (
        code,
        a.collider
            .pose
            .matrix
            .local_to_world(NORMALS[code as usize]),
    )
}
fn separate(state: &mut State, normal: [i32; 3], factor: i32) {
    for i in [0, 2] {
        state.velocity[i] = state.velocity[i].wrapping_sub(normal[i].wrapping_mul(factor));
    }
}
impl Pair<'_> {
    fn approaching(&self, dt: i32) -> bool {
        collision::approaching(
            Trajectory {
                position: self.geometry[0].collider.pose.position,
                velocity: self.states[0].velocity,
            },
            Trajectory {
                position: self.geometry[1].collider.pose.position,
                velocity: self.states[1].velocity,
            },
            dt,
        )
    }
    fn exchange(&mut self, normal: [i32; 3]) {
        let incoming = self.states[1].velocity;
        let [a, b] = &mut self.states;
        collision::exchange(
            self.geometry[0].collider.pose.matrix,
            a.local_velocity,
            &mut a.velocity,
            &mut b.velocity,
            normal,
            incoming,
        );
    }
    /// False means no contact overlap: dispatch may still use a swept response.
    /// The separation loop always performs its first iteration before testing
    /// approach or speed. Errors preserve all prior effects and velocity writes.
    pub fn resolve_overlap(
        &mut self,
        speed: i32,
        env: Environment,
        effects: &mut impl Effects,
    ) -> Result<bool, Error> {
        let mut overlap = self.geometry[1]
            .contacts
            .iter()
            .any(|&point| object_query::contains(self.geometry[0], point));
        for (index, &point) in self.geometry[0].contacts.iter().enumerate() {
            if object_query::contains(self.geometry[1], point) {
                if self.states[0].player && env.mode != 4 && env.mode != 5 {
                    effects
                        .impact(self.ids[0], index, if index < 2 { 3 } else { 4 })
                        .map_err(|code| Error::Effect(if code < 0 { code } else { -5 }))?;
                }
                overlap = true;
            }
        }
        if !overlap {
            return Ok(false);
        }
        let (code_a, normal_a) = facing(self.geometry[0], self.geometry[1]);
        let (code_b, normal_b) = facing(self.geometry[1], self.geometry[0]);
        if speed < 450000 {
            let sum = |states: &[&mut State; 2]| {
                math::length2(states[0].velocity[0], states[0].velocity[2])
                    .wrapping_add(math::length2(states[1].velocity[0], states[1].velocity[2]))
            };
            let threshold = sum(&self.states).wrapping_add(20000);
            let limit = if env.separation_limit == 0 {
                65536
            } else {
                env.separation_limit
            };
            let mut iterations = 0;
            loop {
                if iterations == limit {
                    return Err(Error::Limit);
                }
                iterations += 1;
                separate(self.states[0], normal_a, 10);
                separate(self.states[1], normal_b, 10);
                if !(sum(&self.states) < threshold && self.approaching(env.dt)) {
                    break;
                }
            }
            if speed > 250000 {
                if code_a >= 2 {
                    effects
                        .track_damage(
                            self.ids[0],
                            code_a as i32,
                            self.geometry[1].collider.pose.position,
                        )
                        .map_err(Error::Effect)?;
                }
                if code_b >= 2 {
                    effects
                        .track_damage(
                            self.ids[1],
                            code_b as i32,
                            self.geometry[0].collider.pose.position,
                        )
                        .map_err(Error::Effect)?;
                }
            }
        } else if self.approaching(env.dt) {
            if code_a < 2 && code_b < 2 {
                separate(self.states[0], normal_a, 50);
                separate(self.states[1], normal_b, 50);
            } else {
                self.exchange(normal_b);
            }
        }
        Ok(true)
    }
}

/// The geometry and mutable states are parallel world components. The world
/// owns unique tank slots; malformed handles/aliasing are explicit errors.
pub fn step(
    own: TankId,
    geometry: &[Tank],
    states: &mut [State],
    slots: &[Option<TankId>; 9],
    env: Environment,
    peer: &mut Option<PeerSnapshot>,
    effects: &mut impl Effects,
) -> Result<Option<TankId>, Error> {
    let tank = geometry.get(own.0).ok_or(Error::MissingTank(own))?;
    let state = states.get(own.0).ok_or(Error::MissingTank(own))?;
    let start = tank.collider.pose.position;
    let end = std::array::from_fn(|i| {
        start[i]
            .wrapping_mul(2)
            .wrapping_sub(state.previous_position[i])
    });
    let mut hit = Hit::default();
    object_query::tanks(tank, geometry, slots, start, end, &mut hit).map_err(Error::Query)?;
    let Some(other) = hit.object else {
        return Ok(None);
    };
    let other_state = states.get(other.0).ok_or(Error::MissingTank(other))?;
    let relative = [
        other_state.velocity[0].wrapping_sub(state.velocity[0]),
        0,
        other_state.velocity[2].wrapping_sub(state.velocity[2]),
    ];
    if env.mode == 4 || env.mode == 5 {
        *peer = Some(PeerSnapshot {
            tank: other,
            velocity_xz: [other_state.velocity[0], other_state.velocity[2]],
        });
    }
    if own == other {
        return Err(Error::AliasedPair(own));
    }
    let pair_states = states
        .get_disjoint_mut([own.0, other.0])
        .map_err(|_| Error::MissingTank(other))?;
    let mut pair = Pair {
        ids: [own, other],
        geometry: [tank, &geometry[other.0]],
        states: pair_states,
    };
    if !pair.resolve_overlap(approximate_distance(relative), env, effects)?
        && pair.approaching(env.dt)
    {
        let (_, normal) = facing(pair.geometry[1], pair.geometry[0]);
        pair.exchange(normal);
    }
    pair.states[0].collision_timer = pair.states[0]
        .collision_timer
        .wrapping_add(env.dt.wrapping_mul(4));
    Ok(Some(other))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        geometry::Bounds,
        object_query::{Collider, EntityId},
        pose::Pose,
    };
    struct Refuse;
    impl Effects for Refuse {
        fn impact(&mut self, _: TankId, _: usize, _: i32) -> Result<(), i32> {
            Err(-13)
        }
        fn track_damage(&mut self, _: TankId, _: i32, _: [i32; 3]) -> Result<(), i32> {
            panic!("unexpected damage")
        }
    }
    fn tank(position: [i32; 3]) -> Tank {
        let bounds = Bounds {
            min: [-100; 3],
            max: [100; 3],
        };
        Tank {
            collider: Collider {
                entity: EntityId(1),
                pose: Pose {
                    position,
                    ..Pose::default()
                },
                external_basis: None,
                radius: 20,
                bounds,
            },
            slot: 0,
            active: true,
            disabled: false,
            ready: true,
            overlap_bounds: bounds,
            contacts: [[0; 3]; 4],
        }
    }
    fn state(player: bool) -> State {
        State {
            velocity: [0; 3],
            local_velocity: [0; 3],
            previous_position: [0; 3],
            collision_timer: 0,
            player,
        }
    }
    #[test]
    fn real_cardinal_magnitude_and_mandatory_first_separation_are_preserved() {
        let (a, b) = (tank([0; 3]), tank([0, 0, 1]));
        let (mut sa, mut sb) = (state(false), state(false));
        let mut pair = Pair {
            ids: [TankId(0), TankId(1)],
            geometry: [&a, &b],
            states: [&mut sa, &mut sb],
        };
        assert_eq!(
            pair.resolve_overlap(
                0,
                Environment {
                    mode: 0,
                    dt: 0,
                    separation_limit: 1
                },
                &mut Refuse
            ),
            Ok(true)
        );
        assert_eq!(sa.velocity, [0, 0, -10240]);
        assert_eq!(sb.velocity, [0, 0, 10240]);
    }
    #[test]
    fn impact_failure_precedes_velocity_and_timer_commits() {
        let (a, b) = (tank([0; 3]), tank([0, 0, 1]));
        let (mut sa, mut sb) = (state(true), state(false));
        let mut pair = Pair {
            ids: [TankId(0), TankId(1)],
            geometry: [&a, &b],
            states: [&mut sa, &mut sb],
        };
        assert_eq!(
            pair.resolve_overlap(
                300000,
                Environment {
                    mode: 0,
                    dt: 40,
                    separation_limit: 0
                },
                &mut Refuse
            ),
            Err(Error::Effect(-13))
        );
        assert_eq!(sa, state(true));
        assert_eq!(sb, state(false));
    }
}
