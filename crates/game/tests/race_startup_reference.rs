//! Initial Player route binding against complete native C startup.
use std::{fs, path::PathBuf, process::Command, sync::Arc};
use tankracer_assets::{
    archive::Archive, level::Level, parameters::Parameters, terrain::TerrainData,
};
use tankracer_game::{session::loading, vehicle_runtime::SinglePlayer};

fn root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game")
}
fn prepare(map: usize, with_scenery: bool) -> loading::Prepared {
    let root = root();
    let read = |name: &str| fs::read(root.join(name)).unwrap();
    let data = read("DATA/FASTFILE.DAT");
    let shared = read("INGAME/FASTFILE.DAT");
    let vehicle = read("TANKS/BRONZE/FASTFILE.DAT");
    let level = read(&format!("LEVELS/{}/FASTFILE.DAT", loading::MAPS[map]));
    let archive = Archive::parse(&level).unwrap();
    let bsm = archive
        .entries()
        .iter()
        .find(|e| e.name.ends_with(".BSM"))
        .unwrap();
    let terrain = Arc::new(TerrainData::decode_bsm(archive.get(bsm.name).unwrap()).unwrap());
    loading::Prepared::load(
        terrain,
        loading::Files {
            data: &data,
            vehicles: &[&vehicle],
            shared: &shared,
            level: &level,
        },
        loading::Selection::default_cup(map, 1).unwrap(),
        with_scenery,
    )
    .unwrap()
}
fn snapshot(world: &SinglePlayer) -> Vec<u32> {
    let player = world.vehicles.entries[world.player.0].as_ref().unwrap();
    let mut out = player
        .physics
        .motion
        .poses
        .physical_position
        .map(|v| v as u32)
        .to_vec();
    out.extend([
        u32::from(player.route.checkpoint.is_some()),
        player.route.passed,
    ]);
    let target = player.route.checkpoint.map(|id| {
        let obstacle = world
            .identity
            .entities
            .record(id)
            .unwrap()
            .object
            .unwrap()
            .obstacle()
            .unwrap();
        let collider = &world.objects.world.colliders[obstacle.0];
        let point = world.objects.components.waypoints.point(obstacle).unwrap();
        (collider.pose.position, point.progress)
    });
    out.extend(target.map_or([0; 3], |t| t.0).map(|v| v as u32));
    out.push(target.map_or(0, |t| t.1) as u32);
    out
}

// Test-only audit. Never compensate for omitted constructors by skipping RNG.
fn omitted_rng_sites(map: usize, prepared: &loading::Prepared) -> (usize, Vec<String>) {
    let selection = loading::Selection::default_cup(map, 1).unwrap();
    let file =
        fs::read(root().join(format!("LEVELS/{}/FASTFILE.DAT", loading::MAPS[map]))).unwrap();
    let archive = Archive::parse(&file).unwrap();
    let script = Level::decode(
        archive
            .get(&format!(
                "{}{}.DAT",
                &loading::MAPS[map][..3],
                ["B", "S", "G"][selection.cup]
            ))
            .unwrap(),
    )
    .unwrap();
    let deferred = &prepared.scenery.as_ref().unwrap().deferred;
    let flocks = script.objects.iter().filter(|o| o.class == "Flock").count();
    let lap_random = script
        .objects
        .iter()
        .filter(|o| {
            deferred.contains_key(o.class)
                && Parameters::new(o.parameters)
                    .unwrap()
                    .integer(":lap_to_add")
                    == Some(-99)
        })
        .map(|o| format!("{}:{}", o.class, o.name))
        .collect();
    (flocks, lap_random)
}
fn draws_from_zero(target: u32) -> Option<i32> {
    let mut seed = 0;
    for draw in 0..100000 {
        if seed == target {
            return Some(draw);
        }
        tankracer_game::random::next(&mut seed);
    }
    None
}

#[test]
fn route_binding_uses_real_world_without_changing_physics_rng_or_scheduler() {
    for map in 0..8 {
        let prepared = prepare(map, true);
        let random = prepared.startup.shared.random;
        let objects = format!("{:?}", prepared.startup.objects.world.colliders);
        let loaded = prepared.finish().unwrap();
        let world = &loaded.world;
        assert_eq!(world.shared.random, random);
        assert_eq!(format!("{:?}", world.objects.world.colliders), objects);
        assert_eq!(world.frame, 0);
        assert_eq!(&snapshot(world)[3..5], &[1, 0], "map={map}");
    }
    let world = prepare(0, false).finish().unwrap().world;
    assert_eq!(
        world.vehicles.entries[world.player.0]
            .as_ref()
            .unwrap()
            .route,
        Default::default(),
        "camera-free fixture has no authored route"
    );
}

#[test]
#[ignore = "requires complete C startup; set TANKRACER_RACE_STARTUP_REFERENCE"]
fn all_eight_initial_player_routes_match_complete_c_startup() {
    let oracle =
        std::env::var_os("TANKRACER_RACE_STARTUP_REFERENCE").expect("set C startup oracle");
    for map in 0..8 {
        let prepared = prepare(map, true);
        let (flocks, lap_random) = omitted_rng_sites(map, &prepared);
        let world = prepared.finish().unwrap().world;
        let result = Command::new(&oracle)
            .arg(root())
            .arg(map.to_string())
            .output()
            .unwrap();
        assert!(
            result.status.success(),
            "map={map}: {}",
            String::from_utf8_lossy(&result.stderr)
        );
        assert_eq!(result.stdout.len(), 4 + 12 * 4);
        assert_eq!(&result.stdout[..4], b"RS01");
        let words: Vec<u32> = result.stdout[4..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(snapshot(&world), words[..9], "map={map}");
        eprintln!(
            "map={map} route/start fields match; C player={} target={} rng={}; Rust rng={}",
            words[9], words[10], words[11], world.shared.random
        );
        eprintln!(
            "  shared RNG draws: C={:?}, Rust={:?}; omitted Flock={flocks} (32 constructor draws each), random lap sites={lap_random:?}",
            draws_from_zero(words[11]),
            draws_from_zero(world.shared.random)
        );
    }
    eprintln!(
        "Race route startup: 8 complete C starts / 72 semantic fields; IDs/RNG diagnostic only"
    );
}
