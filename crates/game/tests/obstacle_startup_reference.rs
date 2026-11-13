use std::{fs, path::PathBuf, process::Command};
use tankracer_assets::{
    archive::Archive, level::Level, model::ShapeArchive, object_catalog::Catalog,
    terrain::TerrainData,
};
use tankracer_game::{
    dispatch::{Class, ClassId},
    object_query::EntityId,
    obstacle_collision::{Grid, World},
    obstacle_startup::{Environment, Initial, MaterialId, Materials, Obstacle},
    scene_meshes::Meshes,
    scene_startup::Resources,
    vehicle_model::{Bundles, ModelBank, Space},
};
struct Reader<'a>(&'a [u8]);
impl<'a> Reader<'a> {
    fn word(&mut self) -> u32 {
        let (a, b) = self.0.split_at(4);
        self.0 = b;
        u32::from_le_bytes(a.try_into().unwrap())
    }
    fn ints<const N: usize>(&mut self) -> [i32; N] {
        std::array::from_fn(|_| self.word() as i32)
    }
    fn text(&mut self) -> &'a str {
        let n = self.word() as usize;
        let (a, b) = self.0.split_at(n);
        self.0 = b;
        std::str::from_utf8(a).unwrap()
    }
    fn check(&mut self, values: impl IntoIterator<Item = i32>, label: &str) {
        for (i, value) in values.into_iter().enumerate() {
            assert_eq!(self.word() as i32, value, "{label} [{i}]");
        }
    }
    fn present(&mut self, yes: bool, label: &str) {
        self.check([i32::from(yes)], label);
    }
    fn shape(&mut self) -> Option<Vec<[i16; 3]>> {
        if self.word() == 0 {
            return None;
        }
        let n = self.word();
        Some((0..n).map(|_| self.ints().map(|v| v as i16)).collect())
    }
    fn material(&mut self, material: Option<MaterialId>, label: &str) {
        let id = self.word();
        assert_eq!(id, material.map_or(0, |m| m.0), "{label}");
        if id != 0 {
            let _name = self.text();
        }
    }
}
struct Requests {
    expected: Vec<(u32, u32, String)>,
    cursor: usize,
}
impl Materials for Requests {
    fn resolve(&mut self, name: &str, mode: u32) -> Result<Option<MaterialId>, i32> {
        let (id, expected_mode, expected_name) = &self.expected[self.cursor];
        self.cursor += 1;
        assert_eq!((name, mode), (expected_name.as_str(), *expected_mode));
        Ok((*id != 0).then_some(MaterialId(*id)))
    }
}
#[test]
#[ignore = "requires actual common Obstacle constructor capture; set TANKRACER_OBSTACLE_STARTUP_REFERENCE"]
fn authored_parent_parameters_auxiliary_debris_and_rng_match_actual_c_startup() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let data = fs::read(root.join("DATA/FASTFILE.DAT")).unwrap();
    let data = Archive::parse(&data).unwrap();
    let catalog = Catalog::decode(data.get("OBPARAMS.DAT").unwrap()).unwrap();
    let ingame_bytes = fs::read(root.join("INGAME/FASTFILE.DAT")).unwrap();
    let ingame_archive = Archive::parse(&ingame_bytes).unwrap();
    let executable = std::env::var_os("TANKRACER_OBSTACLE_STARTUP_REFERENCE")
        .expect("Obstacle constructor oracle");
    let (mut objects, mut auxiliaries, mut debris, mut pieces, mut sprites, mut animations) =
        (0, 0, 0, 0, 0, 0);
    let (mut absent_pieces, mut terry_pieces, mut grid_placed) = (0, 0, 0);
    for (map, name) in [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ]
    .into_iter()
    .enumerate()
    {
        let bytes = fs::read(root.join("LEVELS").join(name).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let terrain =
            TerrainData::decode_bsm(archive.get(&format!("{name}.BSM")).unwrap()).unwrap();
        let source = Bundles {
            vehicle: vec![],
            shared: ingame_archive
                .entries()
                .iter()
                .filter(|e| e.name.ends_with(".SS"))
                .map(|e| ShapeArchive::parse(ingame_archive.get(e.name).unwrap()).unwrap())
                .collect(),
            level: archive
                .entries()
                .iter()
                .filter(|e| e.name.ends_with(".SS"))
                .map(|e| ShapeArchive::parse(archive.get(e.name).unwrap()).unwrap())
                .collect(),
        };
        for cup in 0..3 {
            let script = format!("{}{}.DAT", &name[..3], ["B", "S", "G"][cup]);
            let level = Level::decode(archive.get(&script).unwrap()).unwrap();
            if level.objects.is_empty() {
                continue;
            }
            let output = Command::new(&executable)
                .arg(&root)
                .arg(map.to_string())
                .arg(cup.to_string())
                .output()
                .unwrap();
            assert!(
                output.status.success(),
                "{name}: {:?} {}",
                output.status.code(),
                String::from_utf8_lossy(&output.stderr)
            );
            assert_eq!(&output.stdout[..4], b"OS02");
            let mut r = Reader(&output.stdout[4..]);
            let mut models = ModelBank::default();
            for name in [
                "cmine.sm",
                "chomer.sm",
                "cufo.sm",
                "cconc.sm",
                "cturbo.sm",
                "cshield.sm",
            ] {
                models.load(&source, Space::Shared, name).unwrap();
            }
            if cup > 0 {
                models.load(&source, Space::Shared, "cemine.sm").unwrap();
            }
            if cup == 2 {
                models.load(&source, Space::Shared, "cwarp.sm").unwrap();
            }
            let mut meshes = Meshes::default();
            let mut placements = Vec::new();
            let mut collision_world = World {
                colliders: vec![],
                objects: vec![],
                deformables: vec![],
                classes: vec![std::sync::Arc::new(Class {
                    maximum_slot: 0,
                    methods: vec![None],
                })],
                grid: Grid {
                    origin: terrain.origin.map(|v| v.wrapping_mul(8)),
                    shifts: terrain.shifts.map(|v| u32::from(v) + 3),
                    cells: std::array::from_fn(|_| vec![]),
                },
            };

            while r.word() != 0 {
                let entity = EntityId(r.word());
                let mut random = r.word();
                let class = r.text();
                let params = r.text();
                let position = r.ints::<3>();
                let matrix = r.ints::<9>();
                let initial = Initial {
                    weight: r.word() as i32,
                    threshold: r.word() as i32,
                    activity: r.word() as i32,
                    sort_position: r.word() as i16,
                };
                let source_vertices = r.shape();
                let record = level
                    .objects
                    .iter()
                    .find(|o| {
                        o.class.eq_ignore_ascii_case(class)
                            && o.position == position
                            && o.parameters == params
                    })
                    .unwrap_or_else(|| panic!("{name} {entity:?}: {class} {position:?} {params}"));
                let label = format!("{name} cup={cup} {entity:?} {class} shape={}", record.shape);
                r.check([0], "native status");
                let n = r.word();
                let mut requests = Requests {
                    expected: (0..n)
                        .map(|_| {
                            let id = r.word();
                            let mode = r.word();
                            (id, mode, r.text().into())
                        })
                        .collect(),
                    cursor: 0,
                };
                let built = Obstacle::build_components(
                    record,
                    entity,
                    initial,
                    &mut Resources {
                        source: &source,
                        models: &mut models,
                        meshes: &mut meshes,
                        terrain: &terrain,
                        random: &mut random,
                    },
                    &mut Environment {
                        catalog: &catalog,
                        materials: &mut requests,
                        matrix_tail: Some([0; 3]),
                    },
                )
                .unwrap_or_else(|e| panic!("{label}: {e:?} parameters={params}"));
                assert_eq!(requests.cursor, requests.expected.len());
                assert_eq!(built.collider.pose.matrix.0, matrix, "{label} pose");
                assert_eq!(
                    built.source.as_ref().map(|m| &m.geometry.vertices),
                    source_vertices.as_ref(),
                    "{label} source geometry"
                );
                r.check(
                    built
                        .collider
                        .bounds
                        .min
                        .into_iter()
                        .chain(built.collider.bounds.max),
                    &format!("{label} bounds"),
                );
                r.check(
                    [
                        built.weight,
                        built.threshold,
                        built.activity,
                        i32::from(built.visibility.distance),
                        i32::from(built.sort_position),
                        i32::from(built.visibility.add_lap),
                        i32::from(built.visibility.remove_lap),
                        i32::from(built.collision_margin),
                        built.minimum_activation,
                    ],
                    &format!("{label} scalar"),
                );
                let cell = r.word();
                r.present(built.auxiliary.is_some(), "auxiliary");
                if let Some(a) = &built.auxiliary {
                    assert_eq!(r.text(), a.shape_name);
                    r.check(
                        [
                            a.gravity,
                            a.bounce.packed() as i32,
                            a.friction.packed() as i32,
                            a.push.packed() as i32,
                            a.velocity.packed() as i32,
                            a.spin.packed() as i32,
                        ],
                        &format!("{label} auxiliary"),
                    );
                    auxiliaries += 1;
                }
                r.present(built.debris.is_some(), "debris");
                if let Some(f) = &built.debris {
                    r.check(f.offset, "offset");
                    r.check(
                        [
                            i32::from(f.retired),
                            f.flags as i32,
                            f.ground_height,
                            f.gravity,
                            f.friction,
                            f.direction,
                        ],
                        "debris settings",
                    );
                    r.check([f.pieces.len() as i32], "piece count");
                    for p in &f.pieces {
                        let vertices = r.shape();
                        assert_eq!(
                            vertices.as_ref(),
                            p.source.as_ref().map(|m| &m.geometry.vertices),
                            "{label} piece geometry"
                        );
                        r.check(p.position, "piece position");
                        r.check(p.velocity, "piece velocity");
                        r.check(p.matrix.0, "piece matrix");
                        r.check(p.translation, "piece translation");
                        r.check(p.angular, "piece angular");
                        r.check(
                            [
                                p.previous_tilt,
                                i32::from(p.velocity_gain),
                                i32::from(p.spin_gain),
                                p.flags as i32,
                                p.sprite_angle,
                            ],
                            "piece scalar",
                        );
                        absent_pieces += usize::from(p.source.is_none() && f.sprite.is_none());
                        terry_pieces += usize::from(f.flags & 256 != 0);
                        pieces += 1;
                    }
                    r.present(f.sprite.is_some(), "sprite");
                    if let Some(s) = &f.sprite {
                        r.material(s.material, "sprite material");
                        r.check(s.position, "sprite position");
                        r.check(
                            [s.flags as i32, s.timer, s.size[0], s.size[1]],
                            "sprite scalar",
                        );
                        r.check(
                            s.uv.into_iter().chain(s.color).map(i32::from),
                            "sprite uv/color",
                        );
                        sprites += 1;
                    }
                    r.present(f.animation.is_some(), "animation");
                    if let Some(a) = &f.animation {
                        r.material(a.material, "animation material");
                        r.check(a.position, "animation position");
                        r.check([a.blend, 0, a.size[0], a.size[1]], "animation scalar");
                        r.check(
                            [
                                i32::from(a.sheet_size),
                                i32::from(a.cell_size),
                                a.initial_size,
                                i32::from(a.cycles),
                                i32::from(a.growth),
                                i32::from(a.timer),
                                i32::from(a.cycle),
                            ],
                            "animation config",
                        );
                        r.check(
                            a.uv.into_iter().chain(a.color).map(i32::from),
                            "animation uv/color",
                        );
                        animations += 1;
                    }
                    debris += 1;
                }
                r.check([random as i32], &format!("{label} random"));
                objects += 1;
                if ["Obstacle", "Fall", "Bounce", "Spin"]
                    .iter()
                    .any(|c| class.eq_ignore_ascii_case(c))
                {
                    placements.push((built, cell));
                }
            }
            for (built, expected_cell) in placements {
                let minimum = built.minimum_activation;
                let placed = built
                    .install_in_grid(&mut collision_world, ClassId(0))
                    .unwrap();
                grid_placed += 1;
                assert_eq!(
                    placed.cell.map_or(u32::MAX, |c| c.packed()),
                    expected_cell,
                    "{name} installed cell"
                );
                assert_eq!(collision_world.objects[placed.id.0].threshold, minimum);
            }
            assert!(r.0.is_empty());
        }
    }
    assert!(objects > 100 && debris > 0 && auxiliaries > 0);
    assert!(
        sprites > 0 && animations > 0 && absent_pieces > 0 && terry_pieces > 0 && grid_placed > 0
    );
    eprintln!(
        "{objects} common Obstacle constructions; auxiliary={auxiliaries}, debris={debris}, pieces={pieces}, sprites={sprites}, animations={animations}, absent pieces={absent_pieces}, terry pieces={terry_pieces}, installed={grid_placed}; exact C outputs/RNG"
    );
}
