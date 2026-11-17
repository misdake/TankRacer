//! Original Powerup module dispatch, not a claim of complete-world frame parity.
use super::*;
use crate::{actor_startup::Actor, spatial::Cell};
use std::{
    fs,
    io::Write,
    path::PathBuf,
    process::{Command, Stdio},
};
fn load(map: usize) -> Constructed {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let read = |p: &str| fs::read(root.join(p)).unwrap();
    let data = read("DATA/FASTFILE.DAT");
    let shared = read("INGAME/FASTFILE.DAT");
    let vehicle = read("TANKS/BRONZE/FASTFILE.DAT");
    let level = read(&format!("LEVELS/{}/FASTFILE.DAT", loading::MAPS[map]));
    Constructed::load(
        Files {
            data: &data,
            vehicles: &[&vehicle],
            shared: &shared,
            level: &level,
        },
        Selection::default_cup(map, 1).unwrap(),
    )
    .unwrap()
}
fn append(out: &mut Vec<u32>, v: impl IntoIterator<Item = i32>) {
    out.extend(v.into_iter().map(|v| v as u32));
}
fn snapshot(w: &Constructed, id: crate::object_query::ObstacleId) -> Vec<u32> {
    let world = &w.world;
    let collider = &world.objects.world.colliders[id.0];
    let mut out = vec![];
    append(&mut out, collider.pose.position);
    append(&mut out, collider.pose.matrix.0);
    out.push(world.objects.world.objects[id.0].flags);
    let (_, Actor::Powerup(p)) = world
        .objects
        .components
        .actors
        .entries()
        .iter()
        .find(|(i, _)| *i == id)
        .unwrap()
    else {
        panic!("missing pickup");
    };
    out.push(p.ground as u32);
    append(&mut out, p.flight.angular);
    append(&mut out, p.flight.normal);
    append(&mut out, p.flight.parachute.map_or([0; 3], |p| p.position));
    append(&mut out, p.flight.parachute.map_or([0; 9], |p| p.matrix.0));
    out.extend([
        p.flight.blink_time as u32,
        p.flight.blink_count,
        p.flight.delay as u32,
        u32::from(p.flight.disabled),
    ]);
    let object = world.objects.components.objects[id.0].as_ref().unwrap();
    out.extend([
        object.auxiliary_active as u16 as u32,
        object.data.cell.map_or(u32::MAX, Cell::packed),
        world.shared.random,
    ]);
    let values: Vec<_> = world
        .vehicles
        .visible
        .iter()
        .map(|id| {
            world.vehicles.entries[id.0]
                .as_ref()
                .unwrap()
                .physics
                .entity
                .0
        })
        .chain(
            world
                .objects
                .components
                .special
                .iter()
                .map(|id| world.objects.world.colliders[id.0].entity.0),
        )
        .collect();
    out.push(values.len() as u32);
    out.extend(values);
    out
}
#[test]
#[ignore = "requires TANKRACER_POWERUP_REFERENCE native isolated Powerup dispatch"]
fn powerup_phases_match_original_dispatch() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let oracle = std::env::var_os("TANKRACER_POWERUP_REFERENCE").expect("set Powerup oracle");
    let mut fields = 0;
    for map in [0, 2, 5] {
        let mut w = load(map);
        let (id, range, ground) = w
            .world
            .objects
            .components
            .actors
            .entries()
            .iter()
            .find_map(|(id, a)| match a {
                Actor::Powerup(p) if p.phase == 1 => Some((*id, p.fall_range, p.ground)),
                _ => None,
            })
            .unwrap();
        let entity = w.world.objects.world.colliders[id.0].entity;
        let origin = w.world.objects.world.colliders[id.0].pose.position;
        let diameter = w.world.objects.components.actors.pickup_geometry().diameter as i32;
        let mut input = vec![29];
        let mut expected = snapshot(&w, id);
        for tick in 0..29 {
            let dt = if tick == 12 { 100 } else { 16 };
            let number = if tick == 27 { entity.0 } else { tick + 1 };
            let mut player = origin;
            if tick == 0 {
                player[0] = player[0].wrapping_add(range);
            }
            let mut camera = origin.map(|v| v >> 3);
            if tick == 26 {
                camera[0] += 100000;
            }
            input.extend([dt, number]);
            input.extend(player.map(|v| v as u32));
            input.extend(camera.map(|v| v as u32));
            input.push(u32::from(tick == 2));
            if tick == 2 {
                input.push(ground.wrapping_add(diameter).wrapping_add(1) as u32);
                w.world.objects.world.colliders[id.0].pose.position[1] =
                    ground.wrapping_add(diameter).wrapping_add(1);
            }
            w.world.vehicles.entries[0]
                .as_mut()
                .unwrap()
                .physics
                .motion
                .poses
                .body
                .position = player;
            let scene = &mut w.world.objects;
            scene.components.dt = dt as i32;
            scene.components.frame_group = number;
            scene.components.camera_position = camera;
            w.world
                .identity
                .context()
                .dispatch(
                    entity,
                    scene_loading::TICK,
                    None,
                    w.world.registry.environment(100000),
                    &mut scene.updates_with_vehicles(&mut w.world.shared.random, &w.world.vehicles),
                )
                .unwrap();
            expected.push(0);
            expected.extend(snapshot(&w, id));
        }
        let mut child = Command::new(&oracle)
            .arg(&root)
            .args([map.to_string(), entity.0.to_string()])
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()
            .unwrap();
        child
            .stdin
            .take()
            .unwrap()
            .write_all(
                &input
                    .into_iter()
                    .flat_map(u32::to_le_bytes)
                    .collect::<Vec<_>>(),
            )
            .unwrap();
        let output = child.wait_with_output().unwrap();
        assert!(output.status.success(), "map={map}");
        let actual: Vec<u32> = output
            .stdout
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        for (i, (a, b)) in actual.iter().zip(&expected).enumerate() {
            assert_eq!(*a, *b, "map={map} field={i}");
        }
        assert_eq!(actual.len(), expected.len());
        fields += actual.len();
    }
    eprintln!(
        "Powerup: 87 original module updates / {fields} fields; strict wake range, falling, landing, blinking, ground spin/culling and normalization"
    );
}
