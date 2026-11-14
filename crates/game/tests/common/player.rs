//! Semantic player codec shared by constructor and fresh runtime qualification.
use super::common;
use tankracer_assets::config::Part;
use tankracer_game::vehicle_startup::Player;

fn append(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
pub fn encode(player: &Player) -> Vec<u32> {
    let v = &player.physics;
    let mut out = common::words(&v.motion);
    let p = &v.powertrain;
    append(&mut out, p.local_velocity);
    append(&mut out, [p.track_speed, p.boost_target, p.engine_display]);
    out.extend([u32::from(p.was_active), p.gear as u8 as u32, p.load as u32]);
    for (point, response) in v
        .contact
        .sampling
        .primary
        .points
        .iter()
        .zip(&v.contact.response.points)
    {
        append(&mut out, point.local);
        append(&mut out, [point.kind, point.previous_gap, point.raw_height]);
        out.push(point.flags);
        append(&mut out, point.normal);
        out.push(response.corner);
        append(&mut out, [response.rate, response.output]);
    }
    for point in player.model.auxiliary_local {
        append(&mut out, point);
    }
    let c = &v.collision;
    append(&mut out, [v.yaw_lock, i32::from(c.disabled), c.timer]);
    // Keep the C semantic order; zero-only constructor cases cannot detect a swap.
    out.push(c.impact_accumulator as u32);
    out.extend([
        c.collision_size,
        c.material as u32,
        u32::from(c.deformable.is_some()),
        c.obstacle_impulse as u32,
    ]);
    append(&mut out, c.tank_delta);
    append(&mut out, c.ground_delta);
    let p = &player.prefix;
    append(
        &mut out,
        [
            p.alternating_sign,
            p.countdown_throttle,
            p.countdown_rpm,
            p.target,
        ],
    );
    append(
        &mut out,
        [i32::from(p.temporary_timer), i32::from(p.temporary_slot)],
    );
    out.extend([p.restore_sidegrip.packed(), p.sidegrip.packed()]);
    let b = &p.boost;
    append(
        &mut out,
        [
            b.extra_speed,
            b.power,
            b.power_loss,
            i32::from(b.timer),
            b.camera_velocity,
            b.camera_timer,
        ],
    );
    out.extend([
        b.target.0,
        u32::from(b.active),
        u32::from(b.applied),
        u32::from(b.phase),
    ]);
    let s = &p.spin;
    out.extend([u32::from(s.flags), u32::from(s.visible)]);
    append(&mut out, [s.hold, s.color_timer]);
    out.extend([s.color, s.target_color]);
    append(&mut out, [s.angle, s.speed]);
    out.push(u32::from(s.disabled));
    if let Some(gun) = &player.model.parts[Part::Gun.slot()] {
        out.push(1);
        out.extend(player.model.gun_vertices.map(u32::from));
        append(&mut out, player.model.gun_original_z.map(i32::from));
        out.push(gun.geometry().vertices.len() as u32);
        for vertex in &gun.geometry().vertices {
            append(&mut out, vertex.map(i32::from));
        }
    } else {
        out.extend([0; 10]);
    }
    out.push(u32::from(player.lights.brake_mode));
    out.extend(player.lights.active.map(u32::from));
    out.extend(player.lights.handles.map(|h| h.0));
    out.extend(player.marks.active.map(u32::from));
    append(
        &mut out,
        player.marks.endpoints.into_iter().flatten().flatten(),
    );
    append(&mut out, player.marks.movement);
    out.extend(player.marks.handles.map(|h| h.0));
    out.extend([player.marks.count, player.marks.head, player.marks.tail]);
    for p in player.spray.particles {
        append(&mut out, p.position);
        append(&mut out, p.velocity);
        append(
            &mut out,
            [
                i32::from(p.life),
                i32::from(p.size as i16),
                i32::from(p.floor),
                i32::from(p.rotation as i16),
            ],
        );
        out.extend([u32::from(p.kind as u8), u32::from(p.side)]);
    }
    out.extend([
        player.spray.timer as u32,
        player.spray.side,
        player.spray.count,
    ]);
    for p in player.smoke.particles {
        append(&mut out, p.position);
        append(&mut out, p.velocity);
        append(
            &mut out,
            [
                i32::from(p.life),
                i32::from(p.size as i16),
                i32::from(p.rotation as i16),
            ],
        );
        out.extend([u32::from(p.direction), u32::from(p.expanding)]);
    }
    out.extend([
        player.smoke.timer as u32,
        player.smoke.side,
        player.smoke.force as u32,
        player.smoke.count,
    ]);
    out.extend(player.smoke.damage.map(u32::from));
    out.extend(player.smoke.sockets.map(u32::from));
    append(&mut out, player.smoke.damage_timers);
    out.extend([
        player.debris.active,
        player.debris.timer as u32,
        u32::from(player.debris.previous_plane),
    ]);
    out.extend([
        u32::from(player.input.drive.alternate),
        player.input.drive.alternate_lateral as i32 as u32,
        player.input.drive.alternate_rotation as i32 as u32,
        player.brake.saved_turn as i32 as u32,
        u32::from(player.brake.reverse_braking),
        player.model.config.tank.drive.max_turn as u32,
    ]);
    out
}
