use std::{fs, path::PathBuf, process::Command};
use tankracer_assets::{archive::Archive, level::Level, model::ShapeArchive, terrain::TerrainData};
use tankracer_game::{
    crush,
    dispatch::{Class, ClassId},
    lifecycle::{Queues, Ticks},
    object_query::EntityId,
    obstacle_collision::{Grid, World},
    scene_collision::{DEFORMABLE_HIT, Deformable, Surfaces},
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
    fn runtime(
        &mut self,
        surface: &Deformable,
        entity: EntityId,
        ticks: &mut Ticks<EntityId>,
        queues: &mut Queues<EntityId>,
        initial: bool,
    ) {
        // Surrounding unported constructors supply only initial membership
        // boundaries. Surface/record state is always built from authored data.
        if initial {
            ticks.flags[entity.0 as usize] = self.word();
        } else {
            self.check(ticks.flags[entity.0 as usize], "live flags");
        }
        self.check(u32::from(surface.sampling.state.dirty), "live dirty");
        self.check(
            u32::from(surface.sampling.state.records_attached),
            "live record buffer",
        );
        self.ints(
            [
                surface.runtime.siren_timer,
                surface.runtime.sound_channel as i32,
            ],
            "live siren",
        );
        self.check(
            u32::from(surface.runtime.private_mesh),
            "private mesh ownership",
        );
        self.check(
            u32::from(!surface.vertex_flags.is_empty()),
            "live vertex flags",
        );
        for record in &surface.sampling.state.records {
            self.ints(
                record.position.into_iter().chain(record.world_position),
                "live record positions",
            );
            self.check(
                u32::from_le_bytes([
                    record.state,
                    record.color[0],
                    record.color[1],
                    record.color[2],
                ]),
                "live state/color",
            );
            self.ints([record.timer], "live record timer");
        }
        let count = self.word();
        if initial {
            queues.notifications = (0..count).map(|_| EntityId(self.word())).collect();
        } else {
            assert_eq!(
                count as usize,
                queues.notifications.len(),
                "live notification count"
            );
            for id in &queues.notifications {
                self.check(id.0, "live notification order");
            }
        }
    }
}

#[test]
#[ignore = "requires real map startup with C constructor capture; set TANKRACER_SCENE_STARTUP_REFERENCE"]
fn authored_crush_components_mesh_sharing_records_and_rng_match_real_map_construction() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let executable =
        std::env::var_os("TANKRACER_SCENE_STARTUP_REFERENCE").expect("scene constructor oracle");
    let (mut count, mut glows, mut shared, mut runtime_updates, mut detached) = (0, 0, 0, 0, 0);
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
            assert_eq!(&output.stdout[..4], b"SC02");
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
                let mut built = Crush::build(
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
                    [built.ground_height, built.surface.runtime.siren_timer],
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
                for value in [
                    s.state.dirty,
                    u8::from(s.runtime.highlighted),
                    s.sampling.state.dirty,
                ] {
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
                let saved_sampling = built.surface.sampling.state.clone();
                let saved_runtime = built.surface.runtime.clone();
                let mut ticks = Ticks::<EntityId> {
                    flags: vec![0; entity.0 as usize + 1],
                    ..Default::default()
                };
                let mut queues = Queues::default();
                r.runtime(&built.surface, entity, &mut ticks, &mut queues, true);
                let geometry = tankracer_game::deformation::Geometry {
                    sample_bounds: built.surface.sampling.bounds,
                    edges: [
                        built.collider.bounds.min[0],
                        built.collider.bounds.max[0],
                        built.collider.bounds.min[2],
                        built.collider.bounds.max[2],
                    ],
                    edge_cap: built.surface.sampling.edge_cap,
                    vertices: &meshes.get(built.surface.mesh).unwrap().vertices,
                };
                for i in 0..built.surface.sampling.state.records.len() {
                    let world = built.surface.sampling.state.records[i].world_position;
                    built.surface.sampling.state.sample(&geometry, world);
                }
                r.runtime(&built.surface, entity, &mut ticks, &mut queues, false);
                for _ in 0..2 {
                    let status = if built.surface.sampling.state.records.is_empty()
                        || built.surface.sampling.state.records_attached
                    {
                        runtime_updates += 1;
                        crush::tick(
                            &mut built.surface,
                            1500,
                            entity,
                            &mut ticks,
                            &mut queues,
                            100000,
                        )
                        .map_or_else(crush::code, |_| 0)
                    } else {
                        -1
                    };
                    r.ints([status], "authored record tick status");
                    r.runtime(&built.surface, entity, &mut ticks, &mut queues, false);
                }
                let activated = if let Some(record) = built.surface.sampling.state.records.first() {
                    let world = record.world_position;
                    built
                        .surface
                        .sampling
                        .state
                        .sample(&geometry, world)
                        .activated_records
                } else {
                    0
                };
                r.check(activated, "post-retirement activation");
                r.runtime(&built.surface, entity, &mut ticks, &mut queues, false);
                detached += usize::from(!built.surface.sampling.state.records_attached);
                // Preserve constructor-only placement coverage after the independent
                // short sequence; these small component clones exist only in tests.
                built.surface.sampling.state = saved_sampling;
                built.surface.runtime = saved_runtime;
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
        "{count} authored Crush constructions / {glows} glow records / {shared} shared-mesh pairs / {runtime_updates} runtime ticks / {detached} detached buffers exact against real C startup"
    );
}
