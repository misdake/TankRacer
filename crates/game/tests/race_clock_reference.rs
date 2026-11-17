//! Real clock -> Player input -> prefix/physics/after-pose, at branch boundaries.
use std::{fs, path::PathBuf, process::Command, sync::Arc};
use tankracer_assets::{archive::Archive, terrain::TerrainData};
use tankracer_game::{
    obstacle_collision::Invocation,
    player_input,
    race_clock::{Clock, Racer},
    scene_objects::collision::{Collision, Services, Sound},
    session::{
        self,
        loading::{Files, Selection},
    },
    vehicle_runtime::Devices,
};
#[path = "common/motion.rs"]
#[allow(dead_code)]
mod common;
#[path = "common/player.rs"]
mod player_codec;
#[path = "common/runtime.rs"]
mod runtime_codec;
#[path = "common/vehicle_scene.rs"]
#[allow(dead_code)]
mod scene_codec;
struct Headless;
impl Services for Headless {
    fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
    fn feedback(&mut self, _: i32, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
}
impl Devices for Headless {
    fn surface_feedback(&mut self) -> Result<(), i32> {
        Ok(())
    }
    fn impact_feedback(&mut self, _: i32) -> Result<(), i32> {
        Ok(())
    }
}
fn load(map: usize, car: u32) -> session::Session {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let read = |p: &str| fs::read(root.join(p)).unwrap();
    let data = read("DATA/FASTFILE.DAT");
    let shared = read("INGAME/FASTFILE.DAT");
    let vehicles =
        ["BRONZE", "SILVER", "GOLD"].map(|tier| read(&format!("TANKS/{tier}/FASTFILE.DAT")));
    let level = read(&format!(
        "LEVELS/{}/FASTFILE.DAT",
        session::loading::MAPS[map]
    ));
    let archive = Archive::parse(&level).unwrap();
    let bsm = archive
        .entries()
        .iter()
        .find(|e| e.name.ends_with(".BSM"))
        .unwrap();
    let terrain = Arc::new(TerrainData::decode_bsm(archive.get(bsm.name).unwrap()).unwrap());
    let loaded = session::loading::Prepared::load(
        terrain,
        Files {
            data: &data,
            vehicles: &vehicles.iter().map(Vec::as_slice).collect::<Vec<_>>(),
            shared: &shared,
            level: &level,
        },
        Selection::default_cup(map, car).unwrap(),
        false,
    )
    .unwrap()
    .finish()
    .unwrap();
    let mut s = session::Session::new(loaded.world, loaded.camera).unwrap();
    s.begin_race().unwrap();
    s
}
fn input(i: usize, strategy: usize) -> session::Input {
    let elapsed = if strategy == 6 {
        [1000, 1000, 990, 17, 16, 1000, 16, 17, 20, 1000, 16, 17][i]
    } else {
        [1000, 500, 490, 10, 990, 10, 16, 1000, 17, 20, 1000, 16][i]
    };
    session::Input {
        dt: elapsed,
        controls: session::Controls::Keys(player_input::Keys {
            throttle: match strategy {
                3 => i >= 1,
                4 => true,
                _ => i >= 3,
            },
            brake: i == 8,
            right: (7..=9).contains(&i),
            auxiliary: i == 8,
            ..Default::default()
        }),
        camera: Default::default(),
    }
}
fn snapshot(s: &session::Session) -> Vec<u32> {
    let mut out = runtime_codec::words(s.world(), true);
    let c = s.clock().unwrap();
    let r = s.player().race;
    out.extend([
        c.countdown as u32,
        c.release as u32,
        c.display as u32,
        r.elapsed as u32,
        r.laps as u16 as u32,
        u32::from(r.driving),
    ]);
    out
}
#[test]
fn raw_time_and_release_do_not_reorder_or_skip_physics() {
    let mut s = load(0, 1);
    for i in 0..6 {
        s.tick(input(i, 3), &mut Headless).unwrap();
    }
    assert_eq!(s.world().frame, 6);
    assert_eq!(s.clock().unwrap().countdown, 0);
    assert_eq!(s.player().race.elapsed, 0);
    assert!(s.player().race.driving);
    assert_eq!(s.player().input.fast_start, 1);
    s.tick(input(6, 3), &mut Headless).unwrap();
    assert_eq!(s.player().race.elapsed, 16);
    s.tick(input(7, 3), &mut Headless).unwrap();
    assert_eq!(s.player().race.elapsed, 1016);
    assert_eq!(s.world().objects.components.dt, 100);
    assert_eq!(s.clock().unwrap().release, 0);
    assert!(s.begin_race().is_err());
}
#[test]
fn crossing_zero_and_empty_racer_slots_preserve_source_boundaries() {
    let mut c = Clock {
        countdown: 10,
        release: 0,
        display: 3,
    };
    let mut racers = [Racer::default(); 2];
    c.tick(17, &mut racers);
    assert_eq!(
        c,
        Clock {
            countdown: 0,
            release: 1000,
            display: 4
        }
    );
    assert!(racers.iter().all(|r| r.driving && r.elapsed == 0));
    racers[0].elapsed = i32::MAX;
    c.tick(1001, &mut racers);
    assert_eq!(c.release, 0);
    assert_eq!(racers[0].elapsed, i32::MAX.wrapping_add(1001));
    assert_eq!(racers[1].elapsed, 1001);
}
#[test]
#[ignore = "requires TANKRACER_RACE_CLOCK_REFERENCE original clock/vehicle C oracle"]
fn launch_strategies_and_raw_time_match_c() {
    let oracle = std::env::var_os("TANKRACER_RACE_CLOCK_REFERENCE").expect("set race oracle");
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let mut fields = 0;
    for (map, car) in [(0, 1), (2, 6), (5, 11)] {
        for strategy in 3..=6 {
            let output = Command::new(&oracle)
                .arg(&root)
                .args([
                    map.to_string(),
                    car.to_string(),
                    "0".into(),
                    "12".into(),
                    strategy.to_string(),
                ])
                .output()
                .unwrap();
            assert!(
                output.status.success(),
                "{}",
                String::from_utf8_lossy(&output.stderr)
            );
            assert_eq!(&output.stdout[..4], b"VR01");
            let expected: Vec<u32> = output.stdout[4..]
                .chunks_exact(4)
                .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
                .collect();
            let mut at = 0;
            let mut s = load(map, car);
            for tick in 0..=12 {
                if tick != 0 {
                    assert_eq!(expected[at..at + 2], [0, 1]);
                    at += 2;
                    s.tick(input(tick - 1, strategy), &mut Headless).unwrap();
                }
                let actual = snapshot(&s);
                for (field, a) in actual.iter().enumerate() {
                    assert_eq!(
                        *a,
                        expected[at + field],
                        "map={map} car={car} strategy={strategy} tick={tick} field={field}"
                    );
                }
                at += actual.len();
                fields += actual.len();
            }
            assert_eq!(at, expected.len());
        }
    }
    eprintln!(
        "race clock: 12 sequences / 144 integrated updates / {fields} fields; timely/early/late throttle, exact release and overshoot"
    );
}
