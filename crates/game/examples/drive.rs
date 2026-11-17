//! Terrain/vehicle milestone runner. No C library, dump, emulator or server.
use std::{error::Error, fs, path::PathBuf, sync::Arc, time::Instant};
use tankracer_assets::{archive::Archive, terrain::TerrainData};
use tankracer_game::{
    chase_camera, drive,
    obstacle_collision::Invocation,
    player_input,
    scene_objects::collision::{self, Collision, Sound},
    scenery,
    session::{self, loading},
    vehicle_runtime::Devices,
    vehicle_runtime::SinglePlayer,
};
struct Headless<'a> {
    scenery: Option<&'a scenery::Loaded>,
}
impl collision::Services for Headless<'_> {
    fn auxiliary_model(
        &mut self,
        name: &str,
    ) -> Result<Option<std::sync::Arc<tankracer_assets::model::Model>>, i32> {
        Ok(self.scenery.and_then(|s| s.auxiliary_model(name)))
    }
    fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
    fn feedback(&mut self, _: i32, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
}
impl Devices for Headless<'_> {
    fn surface_feedback(&mut self) -> Result<(), i32> {
        Ok(())
    }
    fn impact_feedback(&mut self, _: i32) -> Result<(), i32> {
        Ok(())
    }
}
enum Runner {
    Physics(Box<SinglePlayer>),
    Session(Box<session::Session>),
}
impl Runner {
    fn world(&self) -> &SinglePlayer {
        match self {
            Self::Physics(w) => w,
            Self::Session(s) => s.world(),
        }
    }
    fn camera(&self) -> Option<&chase_camera::State> {
        match self {
            Self::Session(s) => Some(s.camera()),
            _ => None,
        }
    }
    fn tick(
        &mut self,
        input: session::Input,
        devices: &mut impl Devices,
    ) -> Result<(), session::Error> {
        match self {
            Self::Session(s) => s.tick(input, devices),
            Self::Physics(w) => match input.controls {
                session::Controls::Keys(keys) => w.step_keys(keys, input.dt, true, 0, devices),
                session::Controls::Direct(value) => w.step(value, input.dt, true, devices),
            }
            .map_err(session::Error::World),
        }
    }
}
fn main() -> Result<(), Box<dyn Error>> {
    let args: Vec<_> = std::env::args().skip(1).collect();
    let root = args
        .first()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game"));
    let map: usize = args.get(1).map(|v| v.parse()).transpose()?.unwrap_or(0);
    let car: usize = args.get(2).map(|v| v.parse()).transpose()?.unwrap_or(1);
    let ticks: usize = args.get(3).map(|v| v.parse()).transpose()?.unwrap_or(240);
    let with_scenery = args.iter().skip(4).any(|v| v == "--scenery");
    let with_brake = args.iter().skip(4).any(|v| v == "--handbrake");
    let with_race = args.iter().skip(4).any(|v| v == "--race");
    let with_turret = args.iter().skip(4).any(|v| v == "--turret");
    let with_keys = with_turret || with_brake || args.iter().skip(4).any(|v| v == "--keys");
    let with_views = args.iter().skip(4).any(|v| v == "--views");
    let with_chase = with_race || with_views || args.iter().skip(4).any(|v| v == "--chase");
    let maps = tankracer_assets::level::CUP_MAPS;
    if map >= maps.len() || !(1..=15).contains(&car) || ticks == 0 {
        return Err("map 0..7, car 1..15, ticks > 0".into());
    }
    let data_file = fs::read(root.join("DATA/FASTFILE.DAT"))?;
    let files = ["BRONZE", "SILVER", "GOLD"]
        .map(|tier| fs::read(root.join("TANKS").join(tier).join("FASTFILE.DAT")))
        .into_iter()
        .collect::<Result<Vec<_>, _>>()?;
    let shared = fs::read(root.join("INGAME/FASTFILE.DAT"))?;
    let level_file = fs::read(root.join("LEVELS").join(maps[map]).join("FASTFILE.DAT"))?;
    let archive = Archive::parse(&level_file)?;
    let bsm = archive
        .entries()
        .iter()
        .find(|e| e.name.ends_with(".BSM"))
        .ok_or("missing world")?;
    let terrain = Arc::new(TerrainData::decode_bsm(archive.get(bsm.name).unwrap())?);
    let loaded = loading::Prepared::load(
        terrain,
        loading::Files {
            data: &data_file,
            vehicles: &files.iter().map(Vec::as_slice).collect::<Vec<_>>(),
            shared: &shared,
            level: &level_file,
        },
        loading::Selection::default_cup(map, car as u32)?,
        with_scenery,
    )?
    .finish()?;
    let mut game = loaded.world;
    let scenery = loaded.scenery;
    if with_brake {
        let v = &mut game.vehicles.entries[0].as_mut().unwrap().physics;
        v.motion.velocity = [20000, 0, 500000];
        v.powertrain.local_velocity = [20000, 0, 500000];
    }
    let initial = game.vehicles.entries[0]
        .as_ref()
        .ok_or("missing player")?
        .physics
        .motion
        .poses
        .physical_position;
    if with_scenery {
        let visible = game
            .objects
            .components
            .ordinary
            .iter()
            .chain(&game.objects.components.special)
            .filter(|id| {
                game.objects.components.objects[id.0]
                    .as_ref()
                    .is_some_and(|o| o.data.mesh.is_some())
            })
            .count();
        eprintln!(
            "scenery renderable={visible} ordinary={} special={} total={}",
            game.objects.components.ordinary.len(),
            game.objects.components.special.len(),
            game.objects.components.objects.iter().flatten().count()
        );
    }
    let mut runner = if with_chase {
        let mut session = session::Session::new(game, loaded.camera)?;
        if with_race {
            session.begin_race()?;
        }
        Runner::Session(Box::new(session))
    } else {
        Runner::Physics(Box::new(game))
    };
    let begin = Instant::now();
    let mut grounded = 0;
    for tick in 0..ticks {
        let mut devices = Headless {
            scenery: scenery.as_ref(),
        };
        let controls = if with_race {
            session::Controls::Keys(player_input::Keys {
                throttle: tick >= 1,
                brake: tick == 8,
                right: (7..=9).contains(&tick),
                auxiliary: tick == 8,
                ..Default::default()
            })
        } else if with_keys {
            session::Controls::Keys(player_input::Keys {
                throttle: true,
                right: (ticks / 3..ticks / 2).contains(&tick),
                auxiliary: with_brake && (4..8).contains(&(tick % 12)),
                turret_left: with_turret && (tick < 6 || tick == 12),
                turret_right: with_turret && (10..=12).contains(&tick),
                ..Default::default()
            })
        } else {
            session::Controls::Direct(drive::Input {
                throttle: 65536,
                steering: if (ticks / 3..ticks / 2).contains(&tick) {
                    12000
                } else {
                    0
                },
                ..Default::default()
            })
        };
        let result = runner.tick(
            session::Input {
                dt: if with_race {
                    [1000, 500, 490, 10, 990, 10, 16, 1000, 17, 20, 1000, 16][tick % 12]
                } else {
                    16
                },
                controls,
                camera: chase_camera::Keys {
                    cycle: with_views
                        && ((3..6).contains(&tick) || (9..12).contains(&tick) || tick == 21),
                    rear: with_views && (15..18).contains(&tick),
                },
            },
            &mut devices,
        );
        result.map_err(|e| format!("tick {tick}: {e:?}"))?;
        let game = runner.world();
        if with_turret {
            let p = game.vehicles.entries[0].as_ref().ok_or("Missing Player")?;
            let t = p.turret;
            let record: Vec<_> = [
                i32::from(p.physics.pose_input.turret_yaw),
                t.velocity,
                t.cooldown,
                i32::from(t.centering),
                i32::from(t.moving),
                i32::from(t.center_lock),
                i32::from(t.pressed[0]),
                i32::from(t.pressed[1]),
            ]
            .into_iter()
            .chain(p.physics.motion.poses.turret.matrix.0)
            .chain(p.physics.motion.poses.gun.position)
            .chain(p.physics.motion.poses.physical_position)
            .chain([game.shared.random as i32, game.frame as i32])
            .collect();
            println!("turret={record:?}");
        }
        if with_race && let Runner::Session(s) = &runner {
            let c = s.clock().ok_or("Missing race clock")?;
            let p = s.player();
            let record: Vec<i32> = [
                c.countdown,
                c.release,
                c.display,
                p.race.elapsed,
                i32::from(p.race.laps),
                i32::from(p.race.driving),
                i32::from(p.input.start_held),
                i32::from(p.input.fast_start),
                p.prefix.countdown_throttle,
                p.prefix.countdown_rpm,
            ]
            .into_iter()
            .chain(p.physics.motion.poses.physical_position)
            .chain(p.physics.motion.velocity)
            .chain([game.shared.random as i32, game.frame as i32])
            .collect();
            println!("race={record:?}");
        }
        if with_views {
            let camera = runner.camera().unwrap();
            let player = game.vehicles.entries[0].as_ref().unwrap();
            {
                let record: Vec<_> = [
                    camera.mode as i32,
                    i32::from(camera.rear),
                    i32::from(camera.cycle_held),
                ]
                .into_iter()
                .chain(camera.position)
                .chain(camera.matrix.0)
                .chain(camera.cockpit_local)
                .chain(player.physics.motion.poses.physical_position)
                .chain(player.physics.motion.velocity)
                .chain([game.shared.random as i32])
                .collect();
                println!("frame={record:?}");
            }
        }
        grounded += usize::from(
            game.vehicles.entries[0]
                .as_ref()
                .unwrap()
                .physics
                .contact
                .sampling
                .primary
                .points
                .iter()
                .any(|p| p.kind < 2),
        );
    }
    let micros = begin.elapsed().as_micros();
    let game = runner.world();
    let player = game.vehicles.entries[0]
        .as_ref()
        .ok_or("missing final player")?;
    let final_position = player.physics.motion.poses.physical_position;
    if initial == final_position || grounded == 0 {
        return Err("vehicle did not move on real terrain".into());
    }
    println!(
        "{} car={} ticks={} start={initial:?} end={final_position:?} velocity={:?} grounded={} rng={} us/tick={:.2}",
        maps[map],
        car,
        ticks,
        player.physics.motion.velocity,
        grounded,
        game.shared.random,
        micros as f64 / ticks as f64
    );
    if args.iter().skip(4).any(|v| v == "--route") {
        println!(
            "route={:?}",
            [
                u32::from(player.route.checkpoint.is_some()),
                player.route.checkpoint.map_or(0, |id| id.0),
                player.route.passed,
            ]
        );
    }
    if with_keys {
        let i = player.input.drive;
        println!(
            "controls={:?}",
            [
                i.throttle,
                i.brake,
                i.steering,
                i32::from(i.alternate),
                i32::from(i.alternate_lateral),
                i32::from(i.alternate_rotation),
                i32::from(player.brake.saved_turn),
                i32::from(player.brake.reverse_braking),
                player.model.config.tank.drive.max_turn
            ]
        );
    }
    Ok(())
}
