//! Preserve the pre-refactor driving schedule as an independent short oracle.
use std::{fs, sync::Arc};
use tankracer_assets::{archive::Archive, terrain::TerrainData};
use tankracer_game::{
    chase_camera, drive,
    obstacle_collision::Invocation,
    player_input,
    scene_objects::collision::{Collision, Services, Sound},
    session::{self, loading},
    vehicle_animation::Tracks,
    vehicle_runtime::{Devices, SinglePlayer},
};
struct Silent;
impl Services for Silent {
    fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
    fn feedback(&mut self, _: i32, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
}
impl Devices for Silent {
    fn surface_feedback(&mut self) -> Result<(), i32> {
        Ok(())
    }
    fn impact_feedback(&mut self, _: i32) -> Result<(), i32> {
        Ok(())
    }
}
fn load(map: usize) -> loading::Loaded {
    let root = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let read = |name: &str| fs::read(root.join(name)).unwrap();
    let data = read("DATA/FASTFILE.DAT");
    let shared = read("INGAME/FASTFILE.DAT");
    let car = read("TANKS/BRONZE/FASTFILE.DAT");
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
            vehicles: &[&car],
            shared: &shared,
            level: &level,
        },
        loading::Selection::default_cup(map, 1).unwrap(),
        true,
    )
    .unwrap()
    .finish()
    .unwrap()
}
fn snapshot(world: &SinglePlayer) -> String {
    let p = world.vehicles.entries[world.player.0].as_ref().unwrap();
    format!(
        "{:?}|{:?}|{:?}|{:?}|{:?}|{:?}|{:?}|{}|{}",
        p.physics,
        p.input,
        p.brake,
        p.prefix,
        world.objects.world.colliders,
        world.objects.world.objects,
        world.objects.components.movers,
        world.shared.random,
        world.frame
    )
}
#[test]
fn shared_session_preserves_manual_order_and_changes_no_physics() {
    for map in [0, 5] {
        for direct in [false, true] {
            let reference = load(map);
            let mut world = reference.world;
            let mut camera = chase_camera::State::new(
                &reference.camera,
                world.vehicles.entries[0].as_ref().unwrap().into(),
            )
            .unwrap();
            let mut tracks = Tracks::default();
            let loaded = load(map);
            let mut session = session::Session::new(loaded.world, loaded.camera).unwrap();
            for tick in 0..24 {
                let keys = player_input::Keys {
                    throttle: true,
                    right: (8..12).contains(&tick),
                    auxiliary: (4..8).contains(&tick),
                    ..Default::default()
                };
                let input = drive::Input {
                    throttle: 65536,
                    steering: if (8..12).contains(&tick) { 12000 } else { 0 },
                    ..Default::default()
                };
                let view = chase_camera::Keys {
                    cycle: tick == 3 || tick == 9 || tick == 21,
                    rear: (15..18).contains(&tick),
                };
                let p = world.vehicles.entries[0].as_ref().unwrap();
                camera
                    .tick(
                        &reference.camera,
                        p.into(),
                        chase_camera::Frame {
                            dt: 16,
                            keys: view,
                            hold_displacement: p.prefix.boost.active != 0,
                            ..Default::default()
                        },
                    )
                    .unwrap();
                world.camera_position = camera.published_position();
                world.camera_forward = camera.forward();
                if direct {
                    world.step(input, 16, true, &mut Silent)
                } else {
                    world.step_keys(keys, 16, true, 0, &mut Silent)
                }
                .unwrap();
                let p = world.vehicles.entries[0].as_ref().unwrap();
                tracks.advance(
                    p.physics.powertrain.track_speed,
                    p.physics.motion.angular[1],
                    p.brake.reverse_braking,
                );
                camera.feedback(p.model.offsets[tankracer_assets::config::Part::Turret.slot()]);
                session
                    .tick(
                        session::Input {
                            dt: 16,
                            controls: if direct {
                                session::Controls::Direct(input)
                            } else {
                                session::Controls::Keys(keys)
                            },
                            camera: view,
                        },
                        &mut Silent,
                    )
                    .unwrap();
                assert_eq!(
                    snapshot(session.world()),
                    snapshot(&world),
                    "map={map}, direct={direct}, tick={tick}"
                );
                assert_eq!(session.camera(), &camera);
                assert_eq!(session.tracks(), tracks);
                for (a, b) in session.player().model.parts.iter().zip(&p.model.parts) {
                    if let (Some(a), Some(b)) = (a, b) {
                        assert_eq!(a.geometry(), b.geometry());
                        assert_eq!(a.revision(), b.revision());
                    }
                }
            }
        }
    }
}

#[test]
fn partial_gun_error_still_invalidates_changed_geometry() {
    use tankracer_assets::config::Part;
    let mut world = load(0).world;
    let p = world.vehicles.entries[0].as_mut().unwrap();
    p.physics.motion.articulation.recoil = -160;
    p.model.gun_vertices[3] = 255; // Fail after the earlier valid vertex writes.
    let gun = p.model.parts[Part::Gun.slot()].as_ref().unwrap();
    let before = gun.geometry().clone();
    let revision = gun.revision();
    assert!(
        world
            .step(drive::Input::default(), 16, true, &mut Silent)
            .is_err()
    );
    let gun = world.vehicles.entries[0].as_ref().unwrap().model.parts[Part::Gun.slot()]
        .as_ref()
        .unwrap();
    assert_ne!(gun.geometry(), &before);
    assert_ne!(gun.revision(), revision);
}
