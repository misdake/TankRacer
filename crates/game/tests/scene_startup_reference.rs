use std::{fs, path::PathBuf, process::Command};
use tankracer_assets::{archive::Archive, level::Level, model::ShapeArchive, terrain::TerrainData};
use tankracer_game::{
    dispatch::{Class, ClassId},
    object_query::EntityId,
    obstacle_collision::{Grid, World},
    scene_collision::{DEFORMABLE_HIT, Surfaces},
    scene_meshes::Meshes,
    scene_startup::{Crush, Resources},
    vehicle_model::{Bundles, ModelBank},
};
struct Reader<'a>(&'a [u8]);
impl<'a> Reader<'a> {
    fn word(&mut self) -> u32 {
        let (head, tail) = self.0.split_at(4);
        self.0 = tail;
        u32::from_le_bytes(head.try_into().unwrap())
    }
    fn text(&mut self) -> &'a str {
        let n = self.word() as usize;
        let (head, tail) = self.0.split_at(n);
        self.0 = tail;
        std::str::from_utf8(head).unwrap()
    }
    fn check(&mut self, value: impl Into<u32>, label: &str) {
        assert_eq!(self.word(), value.into(), "{label}");
    }
    fn ints(&mut self, values: impl IntoIterator<Item = i32>, label: &str) {
        for (i, value) in values.into_iter().enumerate() {
            assert_eq!(self.word() as i32, value, "{label} [{i}]");
        }
    }
}

#[test]
#[ignore = "requires real map startup with C constructor capture; set TANKRACER_SCENE_STARTUP_REFERENCE"]
fn authored_crush_components_mesh_sharing_records_and_rng_match_real_map_construction() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let executable =
        std::env::var_os("TANKRACER_SCENE_STARTUP_REFERENCE").expect("scene constructor oracle");
    let (mut count, mut glows, mut shared) = (0, 0, 0);
    for (map, name) in [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ]
    .into_iter()
    .enumerate()
    {
        let bytes = fs::read(root.join("LEVELS").join(name).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        for cup in 0..3 {
            let script = format!("{}{}.DAT", &name[..3], ["B", "S", "G"][cup]);
            let level = Level::decode(archive.get(&script).unwrap()).unwrap();
            // CIRB/CIRS are authored empty placeholders, not runnable levels.
            if level.objects.is_empty() {
                continue;
            }
            let terrain =
                TerrainData::decode_bsm(archive.get(&format!("{name}.BSM")).unwrap()).unwrap();
            let source = Bundles {
                vehicle: vec![],
                shared: vec![],
                level: archive
                    .entries()
                    .iter()
                    .filter(|e| e.name.ends_with(".SS"))
                    .map(|e| ShapeArchive::parse(archive.get(e.name).unwrap()).unwrap())
                    .collect(),
            };
            let mut models = ModelBank::default();
            let mut meshes = Meshes::default();
            let mut objects = level
                .objects
                .iter()
                .filter(|o| o.class.eq_ignore_ascii_case("Crush"));
            let output = Command::new(&executable)
                .arg(&root)
                .arg(map.to_string())
                .arg(cup.to_string())
                .output()
                .unwrap();
            assert!(
                output.status.success(),
                "{name} cup={cup}: {:?} {}",
                output.status.code(),
                String::from_utf8_lossy(&output.stderr)
            );
            assert_eq!(&output.stdout[..4], b"SC01");
            let mut r = Reader(&output.stdout[4..]);
            let mut aliases = Vec::new();
            let mut constructed = Vec::new();
            while r.word() != 0 {
                let entity = EntityId(r.word());
                let mut random = r.word();
                let parameters = r.text();
                let record = objects
                    .next()
                    .expect("C produced more Crush objects than authored file");
                assert_eq!(parameters, record.parameters, "{name} {entity:?}");
                r.check(0_u32, "native constructor status");
                let built = Crush::build(
                    record,
                    entity,
                    &mut Resources {
                        source: &source,
                        models: &mut models,
                        meshes: &mut meshes,
                        terrain: &terrain,
                        random: &mut random,
                    },
                )
                .unwrap_or_else(|e| panic!("{name} {entity:?}: {e:?}"));
                let label = format!("{name} {entity:?}");
                let pointer = r.word();
                for &(old_pointer, old_id) in &aliases {
                    assert_eq!(
                        pointer == old_pointer,
                        built.surface.mesh == old_id,
                        "{label} mesh sharing"
                    );
                    shared += usize::from(pointer == old_pointer);
                }
                aliases.push((pointer, built.surface.mesh));
                r.ints(built.collider.pose.position, &format!("{label} position"));
                r.ints(built.collider.pose.matrix.0, &format!("{label} basis"));
                r.check(
                    u32::from(built.collider.external_basis == Some(built.collider.pose.matrix)),
                    "external basis",
                );
                r.check(u32::from(built.collider.radius), "radius");
                r.ints(
                    built
                        .collider
                        .bounds
                        .min
                        .into_iter()
                        .chain(built.collider.bounds.max),
                    &format!("{label} bounds"),
                );
                r.ints([built.weight], &format!("{label} weight"));
                r.check(
                    u32::from(built.visibility.distance),
                    &format!("{label} visibility"),
                );
                r.ints(
                    [
                        i32::from(built.surface.config.material),
                        i32::from(built.visibility.add_lap),
                        i32::from(built.visibility.remove_lap),
                    ],
                    &format!("{label} sort/lap"),
                );
                r.check(
                    u32::from(built.visibility.entered),
                    &format!("{label} entered"),
                );
                r.ints(
                    [built.ground_height, built.siren_timer],
                    &format!("{label} ground/siren"),
                );
                let s = &built.surface;
                r.ints(
                    [
                        s.config.floor,
                        s.config.compression,
                        s.config.drift_shift as i32,
                        s.sampling.edge_cap,
                    ],
                    &format!("{label} config"),
                );
                r.ints(
                    s.sampling.bounds.into_iter().flatten(),
                    &format!("{label} sampling bounds"),
                );
                for value in [s.state.dirty, 0, s.sampling.state.dirty] {
                    r.check(u32::from(value), &format!("{label} dirty"));
                }
                r.check(s.state.last_vehicle.map_or(0, |e| e.0), "last vehicle");
                r.ints([s.state.sound_timer], "sound timer");
                r.check(u32::from(s.config.has_sound), "sound presence");
                r.check(s.sampling.state.records.len() as u32, "record count");
                for record in &s.sampling.state.records {
                    r.ints(
                        record.position.into_iter().chain(record.world_position),
                        &format!("{label} glow position"),
                    );
                    r.check(
                        u32::from_le_bytes([
                            record.state,
                            record.color[0],
                            record.color[1],
                            record.color[2],
                        ]),
                        "state/color",
                    );
                    r.ints([record.timer], "record timer");
                    glows += 1;
                }
                let mesh = meshes.get(s.mesh).unwrap();
                r.check(mesh.vertices.len() as u32, "vertex count");
                r.ints(
                    mesh.vertices.iter().flatten().copied().map(i32::from),
                    &format!("{label} vertices"),
                );
                r.check(
                    u32::from(meshes.hint(s.mesh).unwrap()),
                    &format!("{label} mesh hint"),
                );
                for &flags in &s.vertex_flags {
                    r.check(u32::from(flags), &format!("{label} vertex flags"));
                }
                r.check(random, &format!("{label} RNG"));
                count += 1;
                constructed.push(built);
            }
            let active = constructed.iter().filter(|c| c.visibility.entered).count();
            let mut surfaces = Surfaces {
                entries: vec![],
                meshes,
            };
            let mut world = World {
                colliders: vec![],
                objects: vec![],
                deformables: vec![],
                classes: vec![std::sync::Arc::new(Class {
                    maximum_slot: 0,
                    methods: vec![Some(DEFORMABLE_HIT)],
                })],
                grid: Grid {
                    origin: [0; 2],
                    shifts: [0; 2],
                    cells: std::array::from_fn(|_| vec![]),
                },
            };
            for built in constructed {
                let placed = built
                    .install(&mut world, &mut surfaces, ClassId(0))
                    .unwrap();
                assert_eq!(
                    world.deformables.contains(&placed.id),
                    placed.visibility.entered
                );
                assert!(surfaces.entries[placed.id.0].is_some());
            }
            assert_eq!(world.deformables.len(), active);
            assert!(world.grid.cells.iter().all(Vec::is_empty));
            assert!(r.0.is_empty(), "{name} trailing bytes");
            assert!(objects.next().is_none(), "{name} C omitted authored Crush");
        }
    }
    assert!(count > 40 && glows > 0);
    eprintln!(
        "{count} authored Crush constructions / {glows} glow records / {shared} shared-mesh pairs exact against real C startup"
    );
}
