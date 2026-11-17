//! Compare construction order, every record's RNG boundary and initialized actor fields.
use std::{fs, path::PathBuf, process::Command};
use tankracer_game::{
    actor_startup::{Actor, train::Motion},
    session::loading::{Files, MAPS, Selection},
    world_startup::Constructed,
};
fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game")
}
fn load(map: usize, cup: usize, car: u32) -> Result<Constructed, Box<dyn std::error::Error>> {
    let r = root();
    let read = |p: &str| fs::read(r.join(p)).unwrap();
    let data = read("DATA/FASTFILE.DAT");
    let vehicles =
        ["BRONZE", "SILVER", "GOLD"].map(|tier| read(&format!("TANKS/{tier}/FASTFILE.DAT")));
    let shared = read("INGAME/FASTFILE.DAT");
    let level = read(&format!("LEVELS/{}/FASTFILE.DAT", MAPS[map]));
    Constructed::load(
        Files {
            data: &data,
            vehicles: &vehicles.iter().map(Vec::as_slice).collect::<Vec<_>>(),
            shared: &shared,
            level: &level,
        },
        Selection {
            map,
            cup,
            vehicle: car,
        },
    )
}
fn append(out: &mut Vec<u32>, v: impl IntoIterator<Item = i32>) {
    out.extend(v.into_iter().map(|v| v as u32));
}
fn actor(out: &mut Vec<u32>, a: &Actor) {
    match a {
        Actor::Noisy { sounds, selected } => {
            out.extend([*selected as u32, sounds.len() as u32]);
            for s in sounds {
                append(out, s.values);
            }
        }
        Actor::Smoker {
            white,
            billow,
            particles,
        } => {
            out.extend([u32::from(*white), u32::from(*billow)]);
            for p in particles.iter() {
                out.push(u32::from(p.life as u16));
                append(out, p.position);
            }
        }
        Actor::Trackcam(s) => {
            append(out, s.displacement);
            out.extend([s.flags, s.range as u32]);
            append(out, s.original_displacement);
            out.extend(s.angles);
            let short = |n: i16| n as u16 as u32;
            out.extend([short(s.movement[0].speed), short(s.movement[0].initial)]);
            for a in [&s.movement[1], &s.movement[2]] {
                out.extend([a.speed, a.initial, a.range, a.travel].map(short));
                out.extend(a.decay.map(u32::from));
            }
            out.extend([short(s.max_move), short(s.move_travel)]);
            out.extend(s.move_decay.map(u32::from));
            out.extend([s.zoom.speed, s.zoom.initial, s.zoom.range, s.zoom.travel].map(short));
            out.extend(s.zoom.decay.map(u32::from));
            out.extend(s.rotation.map(short));
        }
        Actor::Emitter(s) => {
            out.extend([s.timer as u32, u32::from(s.gun)]);
            append(out, s.offset);
            out.extend([s.pitch as u32, s.trigger]);
            append(out, s.bounds);
            out.push(s.period as u32);
            out.extend(s.color.map(u32::from));
            out.push(u32::from(s.alpha));
            out.push(u32::from(s.model.is_some()));
            out.push(s.size as u32);
            append(out, s.glow);
            out.push(u32::from(s.asteroid));
        }
        Actor::Train(s) => {
            out.extend([s.speed as u32, s.range as u32]);
            let mut radius = 0;
            let mut start = [0; 3];
            let mut end = [0; 3];
            let mut velocity = [0; 3];
            let mut rates = [0; 2];
            match s.motion {
                Motion::Path {
                    start: a,
                    end: b,
                    velocity: v,
                    ..
                } => {
                    start = a;
                    end = b;
                    velocity = v;
                }
                Motion::Eagle { radius: r, .. } => {
                    radius = r;
                    start = s.origin;
                }
                Motion::Ferris { radius: r } => radius = r,
                Motion::Yacht { rates: r } => {
                    start = s.origin;
                    rates = [r[1], r[2]];
                }
                _ => (),
            };
            out.push(radius as u32);
            append(out, start);
            append(out, end);
            append(out, velocity);
            out.extend(
                s.propeller
                    .as_ref()
                    .map_or([0; 2], |p| [p.target as u16 as u32, p.timer as u16 as u32]),
            );
            append(out, s.propeller.as_ref().map_or([0; 3], |p| p.pivot));
            append(out, rates);
            let center = match s.motion {
                Motion::Path { center, .. } => center,
                _ => [0; 3],
            };
            append(out, center);
            if matches!(s.motion, Motion::Ferris { .. }) {
                append(out, s.base_matrix.0);
            }
        }
        Actor::Flock(s) => {
            out.extend([s.kind, s.range as u32, s.maneuver as u16 as u32]);
            for b in s.birds {
                append(out, b.pose.position);
                append(out, b.pose.matrix.0);
                out.extend([b.speed as u32, u32::from(b.active)]);
            }
            out.push(s.models.len() as u32);
            for m in &s.models {
                out.extend([u32::from(m.radius), u32::from(m.render_hint)]);
            }
            out.push(s.extrema.len() as u32);
            for indices in &s.extrema {
                out.extend(indices.map(|i| i as u32));
            }
        }
        Actor::Powerup(s) => {
            out.extend([
                s.kind,
                s.ground as u32,
                s.fall_range as u32,
                u32::from(s.phase),
                s.added_lap as u16 as u32,
                s.regeneration as u32,
            ]);
        }
        Actor::Background {
            range,
            kind,
            position,
            shake,
            linked_target,
            ..
        } => {
            out.extend([*range as u32, *kind]);
            append(out, *position);
            out.extend(shake.map(|v| v as u16 as u32));
            out.push(linked_target.map_or(0, |e| e.0));
        }
        Actor::Lamps(s) => {
            out.push(s.entries.len() as u32);
            for l in &s.entries {
                append(out, l.position);
                out.extend(l.color.map(u32::from));
                out.extend([
                    u32::from(l.visible),
                    u32::from(l.kind),
                    u32::from(l.alternate),
                    l.remaining as u16 as u32,
                    l.flash_times[0] as u16 as u32,
                    l.flash_times[1] as u16 as u32,
                ]);
            }
        }
    }
}
fn snapshot(w: &Constructed) -> (Vec<u32>, Vec<(usize, u32, u32)>) {
    let world = w.world();
    let p = world.vehicles.entries[world.player.0].as_ref().unwrap();
    let mut out = vec![
        world.shared.random,
        p.physics.entity.0,
        w.camera.entity.0,
        w.finish.entity.0,
        w.scenery().construction.len() as u32,
    ];
    let mut spans = vec![];
    for step in &w.scenery().construction {
        let id = world
            .identity
            .entities
            .record(step.entity)
            .unwrap()
            .object
            .unwrap()
            .obstacle()
            .unwrap();
        let a = world
            .objects
            .components
            .actors
            .entries()
            .iter()
            .find(|(i, _)| *i == id)
            .map(|(_, a)| a);
        let kind = match a {
            None => 0,
            Some(Actor::Noisy { .. }) => 1,
            Some(Actor::Smoker { .. }) => 2,
            Some(Actor::Trackcam(..)) => 3,
            Some(Actor::Emitter(..)) => 4,
            Some(Actor::Train(..)) => 5,
            Some(Actor::Flock(..)) => 6,
            Some(Actor::Powerup(..)) => 7,
            Some(Actor::Background { .. }) => 8,
            Some(Actor::Lamps(..)) => 9,
        };
        spans.push((out.len(), step.entity.0, kind));
        out.extend([step.entity.0, step.random_before, step.random_after, kind]);
        let c = &world.objects.world.colliders[id.0];
        append(&mut out, c.pose.position);
        append(&mut out, c.pose.matrix.0);
        if let Some(a) = a {
            actor(&mut out, a);
        }
    }
    append(&mut out, w.camera.pose.position);
    out.extend(w.camera.angles);
    append(&mut out, w.camera.displacement);
    append(&mut out, w.camera.target_angles);
    for a in &w.camera.resize {
        out.extend(a.rate.to_le_bytes().map(u32::from));
        out.extend([u32::from(a.multiplier), u32::from(a.shift)]);
    }
    out.extend([
        u32::from(w.finish.surrogate),
        w.finish.flags,
        w.finish.configuration_cup as u32,
        u32::from(w.finish.player),
    ]);
    append(&mut out, w.finish.bounds.min);
    append(&mut out, w.finish.bounds.max);
    out.push(p.brake.saved_turn as u16 as u32);
    let g = world.objects.components.actors.pickup_geometry();
    out.extend([g.separation as u32, g.diameter]);
    fn list(out: &mut Vec<u32>, values: impl IntoIterator<Item = u32>) {
        let at = out.len();
        out.push(0);
        out.extend(values);
        out[at] = (out.len() - at - 1) as u32;
    }
    list(&mut out, world.identity.ticks.order.iter().map(|e| e.0));
    list(
        &mut out,
        world.identity.queues.notifications.iter().map(|e| e.0),
    );
    list(
        &mut out,
        world.identity.queues.destruction.iter().map(|e| e.0),
    );
    let entity = |id: &tankracer_game::object_query::ObstacleId| {
        world.objects.world.colliders[id.0].entity.0
    };
    list(
        &mut out,
        world
            .vehicles
            .visible
            .iter()
            .map(|i| {
                world.vehicles.entries[i.0]
                    .as_ref()
                    .unwrap()
                    .physics
                    .entity
                    .0
            })
            .chain(world.objects.components.special.iter().map(entity)),
    );
    list(
        &mut out,
        world.objects.components.ordinary.iter().map(entity),
    );
    list(&mut out, world.objects.world.deformables.iter().map(entity));
    list(
        &mut out,
        world.objects.components.lap_objects.iter().map(entity),
    );
    out.extend((1..=w.finish.entity.0).map(|id| world.identity.ticks.flags[id as usize] & 56));
    (out, spans)
}
#[test]
#[ignore = "requires TANKRACER_WORLD_STARTUP_REFERENCE complete native construction oracle"]
fn all_valid_cup_selections_match_complete_construction_and_actor_state() {
    let oracle = std::env::var_os("TANKRACER_WORLD_STARTUP_REFERENCE")
        .expect("set complete constructor oracle");
    let mut fields = 0;
    let mut starts = 0;
    let mut constructors = 0;
    let mut cases: Vec<_> = (0..8)
        .flat_map(|m| {
            (0..3)
                .filter(move |c| m != 7 || *c == 2)
                .map(move |c| (m, c, 1))
        })
        .collect();
    cases.extend([5, 10, 15].map(|car| (0, 0, car)));
    for (map, cup, car) in cases {
        let w = load(map, cup, car).unwrap();
        let output = Command::new(&oracle)
            .arg(root())
            .arg(map.to_string())
            .arg(cup.to_string())
            .arg(car.to_string())
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "map={map} cup={cup} car={car}: {}",
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], b"WS01");
        assert_eq!((output.stdout.len() - 4) % 4, 0);
        let expected: Vec<u32> = output.stdout[4..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        let (actual, spans) = snapshot(&w);
        let at = actual.iter().zip(&expected).position(|(a, b)| a != b);
        if let Some(at) = at {
            let evidence = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join(format!(
                "../../build/rust/tests/m5b-world-{map}-{cup}-{car}-diff.json"
            ));
            fs::write(
                evidence,
                format!("{{\"actual\":{actual:?},\"expected\":{expected:?}}}"),
            )
            .unwrap();
            let span = spans.iter().rev().find(|(start, _, _)| *start <= at);
            panic!(
                "map={map} cup={cup} car={car} field={at} span={span:?} Rust={:?}, C={:?}; lengths {} / {}",
                &actual[at.saturating_sub(3)..(at + 4).min(actual.len())],
                &expected[at.saturating_sub(3)..(at + 4).min(expected.len())],
                actual.len(),
                expected.len()
            );
        }
        assert_eq!(actual.len(), expected.len(), "map={map} field lengths");
        fields += actual.len();
        starts += 1;
        constructors += spans.len();
        eprintln!(
            "map={map} cup={cup} car={car}: {} constructors / {} fields; RNG {}",
            spans.len(),
            actual.len(),
            w.world().shared.random
        );
    }
    eprintln!(
        "Complete startup: {starts} selections / {constructors} constructors / {fields} fields including per-constructor RNG, IDs, poses and actual random actor state"
    );
    for cup in 0..2 {
        let output = Command::new(&oracle)
            .arg(root())
            .args(["7", &cup.to_string(), "1"])
            .output()
            .unwrap();
        assert!(
            !output.status.success(),
            "C unexpectedly accepts unused CIRCUIT cup {cup}"
        );
    }
}

#[test]
fn complete_construction_has_real_owners_and_no_updates() {
    for (map, cup) in [(0, 0), (5, 1)] {
        let w = load(map, cup, 1).unwrap();
        let world = w.world();
        let p = world.vehicles.entries[world.player.0].as_ref().unwrap();
        assert!(w.scenery().deferred.is_empty());
        assert_eq!(w.scenery().construction.len(), w.scenery().entities.len());
        assert!(!world.objects.components.actors.entries().is_empty());
        assert_eq!(w.camera.entity.0, p.physics.entity.0 + 1);
        assert_eq!(w.finish.entity.0, w.camera.entity.0 + 1);
        assert_eq!(w.finish.source, p.physics.entity);
        assert_eq!(w.finish.bounds, p.model.collision_bounds);
        assert!(p.route.checkpoint.is_some());
        assert_eq!(world.frame, 0);
    }
}

#[test]
fn unused_circuit_cups_fail_route_linkage() {
    for cup in 0..2 {
        let Err(error) = load(7, cup, 1) else {
            panic!("CIRCUIT cup {cup} unexpectedly constructed");
        };
        assert!(error.to_string().contains("waypoint linkage"), "{error}");
    }
}
