use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_assets::{
    archive::Archive, level::Level, object_catalog::Catalog, terrain::TerrainData,
};
use tankracer_game::{
    entity_factory,
    object_query::{EntityId, ObstacleId},
    obstacle_collision::Grid,
    obstacle_startup::{MaterialId, Materials},
    scene_loading::{self, Registry},
    scene_objects::Scene,
    vehicle_model::{ModelBank, ShapeSource, Space},
};
struct NoResources;
impl ShapeSource for NoResources {
    fn shape(&self, _: Space, name: &str) -> Option<&[u8]> {
        panic!("unexpected shape {name}");
    }
}
impl Materials for NoResources {
    fn resolve(&mut self, name: &str, _: u32) -> Result<Option<MaterialId>, i32> {
        panic!("unexpected material {name}");
    }
}
fn list(out: &mut Vec<u32>, values: impl IntoIterator<Item = u32>) {
    let start = out.len();
    out.push(0);
    out.extend(values);
    out[start] = (out.len() - start - 1) as u32;
}
fn snapshot(
    out: &mut Vec<u32>,
    scene: &Scene,
    state: &entity_factory::State<ObstacleId>,
    random: u32,
    result: u32,
    highest: usize,
) {
    out.extend([0, result, state.entities.record_count(), random]);
    for i in 1..=highest {
        let record = state.entities.record(EntityId(i as u32));
        out.push(u32::from(record.is_some()));
        let Some(record) = record else {
            continue;
        };
        let id = record.object.unwrap();
        let collider = &scene.world.colliders[id.0];
        let data = &scene.objects[id.0].as_ref().unwrap().data;
        let properties = &scene.world.objects[id.0];
        out.push(state.ticks.flags[i]);
        out.extend(collider.pose.position.map(|v| v as u32));
        out.extend(collider.pose.matrix.0.map(|v| v as u32));
        out.extend([
            u32::from(collider.external_basis.is_some()),
            data.cell.map_or(u32::MAX, |v| v.packed()),
            u32::from(data.visibility.distance),
            data.sort_position as u32,
            data.visibility.add_lap as u16 as u32,
            data.visibility.remove_lap as u16 as u32,
            properties.weight as u32,
            properties.threshold as u32,
            u32::from(data.collision_margin),
            u32::from(data.mesh.is_some()),
            u32::from(data.auxiliary.is_some()),
            u32::from(data.debris.is_some()),
        ]);
    }
    for values in [&scene.ordinary, &scene.special, &scene.lap_objects] {
        list(
            out,
            values.iter().map(|id| scene.world.colliders[id.0].entity.0),
        );
    }
    list(out, state.ticks.order.iter().map(|v| v.0));
    list(out, state.queues.notifications.iter().map(|v| v.0));
    list(out, state.queues.destruction.iter().map(|v| v.0));
    for cell in &scene.world.grid.cells {
        list(
            out,
            cell.iter().map(|id| scene.world.colliders[id.0].entity.0),
        );
    }
}
fn script(lap: i32, remove: bool) -> String {
    let remove = if remove { ":lap_to_remove 2" } else { "" };
    format!(
        "FSTPC 5\n\
        Obstacle First 1.125 -2.25 3.5 17 -31 45 nil (:lap_to_add {lap} {remove} :sortpos 65530 :solidity -200 :min_act_speed -2 :vis_dist 65000 :expand_coll_radius -9 )\n\
        Obstacle nil -12 0 17 0 90 0 nil (:lap_to_add 2 :lap_to_remove 0 )\n\
        Obstacle nil 8 0 -10 0 0 0 nil ()\n\
        Obstacle Last -4000 8 9000 -360 91 5 nil (:lap_to_add 3 :sortpos -32769 )\n\
        Obstacle Plain 4000 0 2 0 0 0 nil (:Solidity 17 )\n\
        Obstacle Random 0 0 0 0 0 0 nil (:lap_to_add -99 )\nPaths ()"
    )
}
fn terrain() -> TerrainData {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../assets/game/LEVELS/ENGLAND/FASTFILE.DAT");
    let bytes = fs::read(root).unwrap();
    let archive = Archive::parse(&bytes).unwrap();
    TerrainData::decode_bsm(archive.get("ENGLAND.BSM").unwrap()).unwrap()
}
#[test]
fn unsupported_classes_do_not_allocate_and_bad_resources_keep_the_created_binding() {
    struct InvalidShape;
    impl ShapeSource for InvalidShape {
        fn shape(&self, _: Space, name: &str) -> Option<&[u8]> {
            assert_eq!(name, "broken.sm");
            Some(&[1, 2, 3])
        }
    }
    let level =
        Level::decode(b"FSTPC 5 Obstacle Broken 3 4 5 0 0 0 broken.sm () Paths ()").unwrap();
    let record = level.objects[0];
    let terrain = terrain();
    let registry = Registry::default();
    let mut scene = registry.scene(Grid {
        origin: [0; 2],
        shifts: [12; 2],
        cells: std::array::from_fn(|_| vec![]),
    });
    let mut state = entity_factory::State::new(4).unwrap();
    let (mut models, mut random, catalog, mut materials) =
        (ModelBank::default(), 123, Catalog::default(), NoResources);
    {
        let mut loader = scene_loading::Loader::new(
            &mut scene,
            scene_loading::Resources {
                source: &InvalidShape,
                models: &mut models,
                terrain: &terrain,
                random: &mut random,
                catalog: &catalog,
                materials: &mut materials,
                matrix_tail: None,
            },
            &registry,
        );
        assert!(matches!(
            loader.load(
                &mut state,
                tankracer_assets::level::Object {
                    class: "Fall",
                    ..record
                }
            ),
            Err(scene_loading::Error::UnsupportedClass(_))
        ));
        assert_eq!(state.entities.record_count(), 0);
        assert!(loader.scene.objects.is_empty());
        assert!(matches!(
            loader.load(&mut state, record),
            Err(scene_loading::Error::Resource(_))
        ));
    }
    assert_eq!(random, 123);
    assert_eq!(state.entities.record_count(), 1);
    let symbol = state.symbols.intern("Broken").unwrap();
    assert_eq!(state.symbols.entity(symbol), Some(EntityId(1)));
    assert_eq!(state.current_slot, scene_loading::READ);
    let id = state.entities.record(EntityId(1)).unwrap().object.unwrap();
    assert_eq!(scene.world.colliders[id.0].pose.position, [0; 3]);
    assert_eq!(scene.objects[id.0].as_ref().unwrap().data.sort_position, -8);
    assert!(scene.objects[id.0].as_ref().unwrap().data.cell.is_none());
    assert!(scene.ordinary.is_empty() && state.ticks.order.is_empty());
    assert_eq!(state.ticks.flags[1], 8);
    state
        .context()
        .destroy(EntityId(1), registry.environment(100000), &mut scene)
        .unwrap();
    assert_eq!(state.entities.record_count(), 0);
    assert!(scene.objects[id.0].is_none());
}
#[test]
#[ignore = "requires fresh C World loader; set TANKRACER_SCENE_LOADING_REFERENCE"]
fn fresh_ids_shared_rng_lap_membership_and_complete_retirement_match_c() {
    let terrain = terrain();
    let registry = Registry::default();
    let catalog = Catalog::default();
    let (mut input, mut expected, mut ranges) = (vec![], vec![], vec![]);
    let mut scenarios = 0;
    for lap in [-99, 0, 1, 2, 3, 4] {
        for remove in [false, true] {
            for is_static in [false, true] {
                let script = script(lap, remove);
                let level = Level::decode(script.as_bytes()).unwrap();
                let seed = 0x11a294cd_u32.wrapping_mul(scenarios + 1);
                let distance = if remove { 199 } else { 150 };
                input.extend_from_slice(&(script.len() as u32).to_le_bytes());
                for value in [seed, u32::from(is_static), distance] {
                    input.extend_from_slice(&value.to_le_bytes());
                }
                input.extend_from_slice(script.as_bytes());
                let mut scene = registry.scene(Grid {
                    origin: [-5000; 2],
                    shifts: [12; 2],
                    cells: std::array::from_fn(|_| vec![]),
                });
                let mut state = entity_factory::State::new(4).unwrap();
                let mut random = seed;
                let mut models = ModelBank::default();
                let mut materials = NoResources;
                {
                    let mut loader = scene_loading::Loader::new(
                        &mut scene,
                        scene_loading::Resources {
                            source: &NoResources,
                            models: &mut models,
                            terrain: &terrain,
                            random: &mut random,
                            catalog: &catalog,
                            materials: &mut materials,
                            matrix_tail: None,
                        },
                        &registry,
                    );
                    loader.static_allocation = is_static;
                    loader.visibility_distance = distance as u16;
                    for (i, record) in level.objects.iter().copied().enumerate() {
                        assert_eq!(
                            loader.load(&mut state, record).unwrap(),
                            EntityId(i as u32 + 1)
                        );
                    }
                }
                let env = registry.environment(100000);
                for phase in 0..4 {
                    match phase {
                        0 => (),
                        1 | 3 => state
                            .context()
                            .drain(scene_loading::STOP, env, &mut scene)
                            .unwrap(),
                        2 => {
                            for i in 1..=level.objects.len() {
                                state
                                    .context()
                                    .dispatch(
                                        EntityId(i as u32),
                                        scene_loading::DEACTIVATE,
                                        None,
                                        env,
                                        &mut scene,
                                    )
                                    .unwrap();
                            }
                        }
                        _ => unreachable!(),
                    }
                    let start = expected.len();
                    snapshot(
                        &mut expected,
                        &scene,
                        &state,
                        random,
                        if phase == 0 { 1 } else { 0x7182 },
                        level.objects.len(),
                    );
                    ranges.push((start..expected.len(),format!("scenario {scenarios} lap={lap} remove={remove} static={is_static} phase={phase}")));
                }
                assert_eq!(state.entities.record_count(), 0);
                assert!(scene.objects.iter().all(Option::is_none));
                scenarios += 1;
            }
        }
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/scene_loading");
    fs::create_dir_all(&folder).unwrap();
    let input_path = folder.join("input.bin");
    fs::write(&input_path, input).unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_SCENE_LOADING_REFERENCE")
            .expect("set TANKRACER_SCENE_LOADING_REFERENCE"),
    )
    .stdin(Stdio::from(fs::File::open(input_path).unwrap()))
    .output()
    .unwrap();
    fs::write(folder.join("output.bin"), &output.stdout).unwrap();
    fs::write(
        folder.join("expected.bin"),
        expected
            .iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    assert!(
        output.status.success(),
        "{:?}: {}",
        output.status.code(),
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"SL01");
    let words: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
        .collect();
    if let Some(index) = words.iter().zip(&expected).position(|(a, b)| a != b) {
        let (range, label) = ranges.iter().find(|(r, _)| r.contains(&index)).unwrap();
        panic!(
            "{label}, word {}: C {}, Rust {}",
            index - range.start,
            words[index],
            expected[index]
        );
    }
    assert_eq!(words.len(), expected.len());
    eprintln!(
        "{scenarios} fresh World scripts / {} constructed objects / {} lifecycle checkpoints / {} semantic words exact",
        scenarios * 6,
        ranges.len(),
        words.len()
    );
}
