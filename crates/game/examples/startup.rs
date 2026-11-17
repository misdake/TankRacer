//! Complete authored-world construction without C, a dump, emulator or server.
//! This stops before the race clock; optional actor ticks belong to M6.
use std::{error::Error, fs, path::PathBuf};
use tankracer_game::{
    session::loading::{Files, MAPS, Selection},
    world_startup::Constructed,
};

fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    let root = args
        .first()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game"));
    let map = args.get(1).map(|v| v.parse()).transpose()?.unwrap_or(0);
    let car = args.get(2).map(|v| v.parse()).transpose()?.unwrap_or(1);
    let mut selection = Selection::default_cup(map, car)?;
    if let Some(cup) = args.get(3) {
        selection.cup = cup.parse()?;
    }
    let read = |name: &str| fs::read(root.join(name));
    let data = read("DATA/FASTFILE.DAT")?;
    let vehicles = ["BRONZE", "SILVER", "GOLD"]
        .map(|tier| read(&format!("TANKS/{tier}/FASTFILE.DAT")))
        .into_iter()
        .collect::<Result<Vec<_>, _>>()?;
    let shared = read("INGAME/FASTFILE.DAT")?;
    let level = read(&format!("LEVELS/{}/FASTFILE.DAT", MAPS[map]))?;
    let constructed = Constructed::load(
        Files {
            data: &data,
            vehicles: &vehicles.iter().map(Vec::as_slice).collect::<Vec<_>>(),
            shared: &shared,
            level: &level,
        },
        selection,
    )?;
    let world = constructed.world();
    let player = world.vehicles.entries[world.player.0]
        .as_ref()
        .ok_or("Missing Player")?;
    println!(
        "[{}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}]",
        world.shared.random,
        player.physics.entity.0,
        constructed.camera.entity.0,
        constructed.finish.entity.0,
        constructed.scenery().construction.len(),
        world.objects.components.actors.entries().len(),
        constructed.camera.variant,
        constructed.camera.pose.position[0],
        constructed.camera.pose.position[1],
        constructed.camera.pose.position[2],
        player.route.checkpoint.map_or(0, |e| e.0),
        player.route.passed,
        world.identity.ticks.order.len(),
        world.identity.queues.notifications.len(),
        world.frame
    );
    Ok(())
}
