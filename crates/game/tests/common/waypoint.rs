// Test-only semantic route graph and shared owner snapshot.
fn list(out: &mut Vec<u32>, v: impl IntoIterator<Item = u32>) {
    let i = out.len();
    out.push(0);
    out.extend(v);
    out[i] = (out.len() - i - 1) as u32;
}
fn append(out: &mut Vec<u32>, v: impl IntoIterator<Item = i32>) {
    out.extend(v.into_iter().map(|v| v as u32));
}
pub fn snapshot(
    scene: &tankracer_game::scene_objects::Scene,
    identity: &tankracer_game::entity_factory::State<tankracer_game::scene_entity::Handle>,
) -> Vec<u32> {
    let r = &scene.components.waypoints;
    let mut out = vec![];
    append(&mut out, r.origin);
    out.extend(r.shifts);
    append(&mut out, r.radius);
    out.extend([
        r.backwards_check.map_or(0, |e| e.0),
        r.fallback.map_or(0, |e| e.0),
        r.backwards_index as u32,
    ]);
    list(
        &mut out,
        r.order
            .iter()
            .map(|id| scene.world.colliders[id.0].entity.0),
    );
    for &id in &r.order {
        let c = scene.world.colliders[id.0];
        let p = r.point(id).unwrap();
        let data = &scene.components.objects[id.0].as_ref().unwrap().data;
        out.extend([c.entity.0, identity.ticks.flags[c.entity.0 as usize]]);
        append(&mut out, c.pose.position);
        append(&mut out, c.pose.matrix.0);
        out.extend([
            data.sort_position as i32 as u32,
            u32::from(data.visibility.distance),
            u32::from(data.mesh.is_some()),
        ]);
        out.extend(
            p.next
                .map(|n| n.resolve(&identity.symbols).map_or(0, |e| e.0)),
        );
        out.extend([
            u32::from(p.lap),
            u32::from(p.hairpin),
            u32::from(p.braking_range),
        ]);
        append(&mut out, [p.gate_size, p.width, p.max_speed, p.progress]);
        out.extend(p.weather.map(u32::from));
        out.extend(p.strength.map(u32::from));
        out.extend([u32::from(p.activate_lap), u32::from(p.check)]);
    }
    out.push(r.lap_start.map_or(0, |e| e.0));
    for cell in &r.cells {
        list(
            &mut out,
            cell.iter().map(|id| scene.world.colliders[id.0].entity.0),
        );
    }
    out.extend(r.weather.iter().flatten().copied().map(u32::from));
    list(&mut out, identity.ticks.order.iter().map(|e| e.0));
    list(&mut out, identity.queues.notifications.iter().map(|e| e.0));
    list(&mut out, identity.queues.destruction.iter().map(|e| e.0));
    list(
        &mut out,
        scene
            .components
            .ordinary
            .iter()
            .map(|id| scene.world.colliders[id.0].entity.0),
    );
    out
}
