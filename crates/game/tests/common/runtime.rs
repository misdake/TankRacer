//! Shared semantic runtime snapshot for short C integration sequences.
use super::{player_codec, scene_codec};
use tankracer_game::vehicle_runtime::SinglePlayer;
pub fn words(game: &SinglePlayer, controls: bool) -> Vec<u32> {
    let v = game.vehicles.entries[0].as_ref().unwrap();
    let mut out = player_codec::encode(v);
    out.push(game.shared.random);
    out.extend(game.shared.noise.map(u32::from));
    out.extend(game.shared.shield.banks[..2].iter().map(|b| b.tail));
    let pool = &game.shared.pool;
    out.extend([pool.line_count, pool.quad_count]);
    for l in &pool.lines {
        out.extend(l.points.iter().flatten().map(|&v| v as u32));
        out.extend(l.colors.map(u32::from_le_bytes));
    }
    // A line handle of -1 aliases the final quad and the following colors.
    let q = pool.quads[159];
    out.extend(q.points.iter().flatten().map(|&v| v as u32));
    out.push(q.material);
    out.extend(
        q.style
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap())),
    );
    out.extend(pool.exhausted_colors.map(u32::from_le_bytes));
    out.extend(pool.overflow.points.iter().flatten().map(|&v| v as u32));
    out.extend(pool.overflow.colors.map(u32::from_le_bytes));
    let mut scene = scene_codec::State {
        local: vec![scene_codec::Local {
            spray: v.spray.clone(),
            smoke: v.smoke.clone(),
            lights: v.lights.clone(),
            marks: v.marks.clone(),
            debris: v.debris,
            temporary_slot: v.prefix.temporary_slot,
        }],
        debris: game.shared.debris.clone(),
        pickup: game.shared.pickup.clone(),
        temporary: game.shared.temporary.clone(),
        shield: game.shared.shield.clone(),
    };
    let mut records = tankracer_game::effect_pool::Pool {
        quads: game.shared.pool.quads,
        exhausted_quad: game.shared.pool.exhausted_quad,
        ..Default::default()
    };
    out.extend(scene.words(&mut records));
    if controls {
        let input = v.input;
        out.extend([
            input.drive.throttle as u32,
            input.drive.brake as u32,
            input.drive.steering as u32,
            u32::from(input.start_held),
            u32::from(input.fast_start),
            u32::from(game.shared.input.steering_analog_latch),
        ]);
    }
    out
}
