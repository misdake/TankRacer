use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
    sync::Arc,
};
use tankracer_assets::{
    config::Gain,
    mesh::{Geometry, Triangle},
    model::Model,
};
use tankracer_game::scene_entity::Handle;
use tankracer_game::{
    dispatch::{ClassId, MethodId},
    entity_factory::{self as factory, Methods},
    math::Matrix,
    mesh_break::{self, Face},
    object_query::{Collider, EntityId, ObstacleId},
    obstacle_collision::{self as collision, Grid, Invocation, Object},
    obstacle_startup::{Auxiliary, PlacedObstacle},
    pose::Pose,
    scene_loading::Registry,
    scene_objects::{
        self, Scene,
        collision::{Collision, Services, Sound},
    },
    scene_startup::Visibility,
    spatial::Cell,
};
#[derive(Clone)]
struct Fixture {
    prepared: bool,
    active: i16,
    position: [i32; 3],
    basis: Option<Matrix>,
    velocity: Option<[i32; 3]>,
    seed: u32,
    base_flags: u32,
    cell: Option<Cell>,
    slot: u32,
    lifetime: i32,
    config: Auxiliary,
    fail_completion: bool,
    lists: [Vec<u32>; 5],
    grid: [Vec<u32>; 64],
    model: Model,
    faces: Vec<Face>,
    retired: u32,
    height: i32,
    tail: Option<u16>,
    steps: Vec<[u32; 6]>,
}
fn triangle(indices: [u8; 3]) -> Triangle {
    Triangle {
        source_offset: 0,
        normal: [0; 3],
        color: 0,
        indices,
        sides: 0,
        uv: [[0; 2]; 3],
        flags: 0,
        material: None,
        material_source_offset: 0,
        billboard: None,
    }
}
impl Default for Fixture {
    fn default() -> Self {
        let faces = vec![[0, 1, 2], [2, 1, 3], [4, 5, 6]];
        Self {
            prepared: true,
            active: 0,
            position: [100, 200, -300],
            basis: Some(Matrix::IDENTITY),
            velocity: Some([250000, -150000, 500000]),
            seed: 0x181293fa,
            base_flags: 12,
            cell: Some(Cell { x: 0, z: 0 }),
            slot: 777,
            lifetime: 2000,
            config: Auxiliary {
                shape_name: "frag.sm".into(),
                gravity: 150,
                bounce: Gain {
                    multiplier: 500,
                    shift: 10,
                },
                friction: Gain {
                    multiplier: 800,
                    shift: 10,
                },
                push: Gain {
                    multiplier: 150,
                    shift: 10,
                },
                velocity: Gain {
                    multiplier: 1,
                    shift: 13,
                },
                spin: Gain {
                    multiplier: 1,
                    shift: 16,
                },
            },
            fail_completion: false,
            lists: [vec![1, 2], vec![], vec![1], vec![], vec![]],
            grid: std::array::from_fn(|i| if i < 16 { vec![1, 2] } else { vec![] }),
            model: Model {
                geometry: Geometry {
                    vertices: vec![
                        [-100, -100, -100],
                        [100, -100, -100],
                        [-100, 100, 100],
                        [100, 100, 100],
                        [1000, 100, 100],
                        [1300, 50, 50],
                        [1000, 350, 100],
                    ],
                    triangles: faces.into_iter().map(triangle).collect(),
                },
                face_centers: [
                    Some(vec![-33, 33, 1100]),
                    Some(vec![-33, 33, 167]),
                    Some(vec![-33, 33, 83]),
                ],
                materials: vec![],
                radius: 77,
                render_hint: 0,
                bounds: [[0; 3]; 2],
                vertex_normals: None,
            },
            faces: vec![Face::default(); 3],
            retired: 0,
            height: 0,
            tail: None,
            steps: vec![
                [1, 40, 0, 0, 0, 100000],
                [2, 16, 0, 0, 0, 100],
                [2, 33, 0, 0, 0, 100],
                [2, 4000, 0, 0, 0, 100],
                [1, 16, 0, 0, 0, 100],
            ],
        }
    }
}
fn fixtures() -> Vec<Fixture> {
    let mut result = vec![Fixture::default()];
    result.push(Fixture {
        prepared: false,
        ..Fixture::default()
    });
    for prepared in [false, true] {
        let f = Fixture {
            prepared,
            steps: vec![
                [3, 40, 0, 0, 0, 100000],
                [4, 16, 0, 0, 0, 100],
                [4, 4000, 0, 0, 0, 100],
            ],
            ..Fixture::default()
        };
        result.push(f);
    }
    for velocity in [None, Some([i32::MIN, i32::MAX, -1]), Some([-1, 0, 1])] {
        let f = Fixture {
            velocity,
            basis: None,
            ..Fixture::default()
        };
        result.push(f);
    }
    for mode in [0, 1, 2, 4] {
        let mut f = Fixture::default();
        f.steps[3][2] = mode;
        f.fail_completion = mode == 4;
        result.push(f);
    }
    for limit in [0, 1, 2, 3, 4, 5, 100] {
        let f = Fixture {
            steps: vec![
                [1, 40, 0, 0, 0, limit],
                [2, 16, 0, 0, 0, limit],
                [2, 4000, 2, 0, 0, limit],
            ],
            ..Fixture::default()
        };
        result.push(f);
    }
    for dt in [0, 1, 2048, i32::MAX as u32, i32::MIN as u32, (-1i32) as u32] {
        let f = Fixture {
            active: 1,
            faces: vec![
                Face {
                    velocity: [2000, -15000, 7000],
                    angular: [17, -29, 65],
                    life: 3000,
                    extra_vertex: 3,
                    mode: 2
                };
                3
            ],
            steps: vec![[2, dt, 0, 0, 0, 100], [2, 16, 0, 0, 0, 100]],
            ..Fixture::default()
        };
        result.push(f);
    }
    for shift in [0, 1, 31, 32, 255, 256, 65535] {
        let mut f = Fixture::default();
        f.config.velocity = Gain {
            multiplier: 65535,
            shift,
        };
        f.config.spin = Gain {
            multiplier: 65535,
            shift,
        };
        f.config.push = Gain {
            multiplier: 65535,
            shift,
        };
        result.push(f);
    }
    for status in [-13, 0, 1] {
        let mut f = Fixture::default();
        f.steps[0][3] = status as u32;
        f.steps[0][4] = (-1000i32) as u32;
        result.push(f);
    }
    for pairs in [
        vec![[0, 1, 2]],
        vec![[0, 1, 2], [2, 1, 0]],
        vec![[0, 1, 2], [2, 1, 0], [0, 1, 2], [4, 5, 6]],
        vec![],
    ] {
        for tail in [None, Some(3)] {
            let mut f = Fixture::default();
            f.model.geometry.triangles = pairs.iter().copied().map(triangle).collect();
            let n = pairs.len();
            f.faces = vec![Face::default(); n];
            f.model.face_centers = std::array::from_fn(|_| Some(vec![0; n]));
            f.tail = tail;
            f.steps = if tail.is_none() && n >= 2 {
                vec![[1, 40, 0, 0, 0, 100]]
            } else {
                vec![
                    [1, 40, 0, 0, 0, 100],
                    [2, 16, 0, 0, 0, 100],
                    [2, 4000, 0, 0, 0, 100],
                ]
            };
            result.push(f);
        }
    }
    let short = Fixture {
        lifetime: 1500,
        ..Fixture::default()
    };
    result.push(short);
    result
}
struct Inputs {
    name: String,
    model: Arc<Model>,
    height_status: i32,
    height: i32,
    queries: u32,
    tail: Option<u16>,
}
impl Services for Inputs {
    fn auxiliary_model(&mut self, name: &str) -> Result<Option<Arc<Model>>, i32> {
        assert_eq!(name, self.name);
        Ok(Some(Arc::clone(&self.model)))
    }
    fn auxiliary_height(&mut self, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<i32, i32> {
        self.queries += 1;
        if self.height_status < 0 {
            Err(self.height_status)
        } else {
            Ok(self.height)
        }
    }
    fn paired_vertex_tail(&mut self) -> Option<u16> {
        self.tail
    }
    fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        panic!("no sound");
    }
    fn feedback(&mut self, _: i32, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        panic!("no feedback");
    }
}
fn ints(out: &mut Vec<u32>, v: impl IntoIterator<Item = i32>) {
    out.extend(v.into_iter().map(|v| v as u32));
}
fn list(out: &mut Vec<u32>, v: impl IntoIterator<Item = u32>) {
    let at = out.len();
    out.push(0);
    out.extend(v);
    out[at] = (out.len() - at - 1) as u32;
}
fn face_words(out: &mut Vec<u32>, f: &Face) {
    ints(out, f.velocity);
    ints(out, f.angular);
    ints(out, [f.life, i32::from(f.extra_vertex)]);
    out.push(u32::from(f.mode));
}
fn encode(f: &Fixture, input: &mut Vec<u8>) {
    let mut w = vec![
        f.steps.len() as u32,
        u32::from(f.prepared),
        f.active as u16 as u32,
    ];
    ints(&mut w, f.position);
    w.push(u32::from(f.basis.is_some()));
    ints(&mut w, f.basis.unwrap_or(Matrix::IDENTITY).0);
    w.push(u32::from(f.velocity.is_some()));
    ints(&mut w, f.velocity.unwrap_or([0; 3]));
    w.extend([
        f.seed,
        f.base_flags,
        f.cell.map_or(u32::MAX, Cell::packed),
        f.slot,
        u32::from(f.lifetime == 1500),
        f.config.gravity as u32,
    ]);
    for g in [
        f.config.bounce,
        f.config.friction,
        f.config.push,
        f.config.velocity,
        f.config.spin,
    ] {
        w.extend([u32::from(g.multiplier), u32::from(g.shift)]);
    }
    w.push(u32::from(f.fail_completion));
    for l in &f.lists {
        list(&mut w, l.iter().copied());
    }
    for l in &f.grid {
        list(&mut w, l.iter().copied());
    }
    w.extend([
        f.model.geometry.vertices.len() as u32,
        f.model.geometry.triangles.len() as u32,
    ]);
    for v in &f.model.geometry.vertices {
        ints(&mut w, v.map(i32::from));
    }
    for tri in &f.model.geometry.triangles {
        w.extend(tri.indices.map(u32::from));
        w.push(u32::from(tri.flags));
    }
    for column in &f.model.face_centers {
        ints(
            &mut w,
            column.as_ref().unwrap().iter().copied().map(i32::from),
        );
    }
    if f.prepared {
        for face in &f.faces {
            face_words(&mut w, face);
        }
        w.extend([f.retired, f.height as u32]);
    }
    w.push(f.tail.map_or(u32::MAX, u32::from));
    w.extend(f.steps.iter().flatten().copied());
    input.extend(w.into_iter().flat_map(u32::to_le_bytes));
}
fn registry(f: &Fixture) -> Registry {
    let mut r = Registry::default();
    if f.fail_completion {
        Arc::make_mut(&mut r.classes[r.obstacle.0]).methods
            [tankracer_game::scene_loading::DEACTIVATE as usize] = Some(MethodId(u16::MAX));
    }
    r
}
fn scene(f: &Fixture, r: &Registry) -> (Scene, factory::State<Handle>) {
    let mut scene = r.scene(Grid {
        origin: [0; 2],
        shifts: [12; 2],
        cells: f
            .grid
            .each_ref()
            .map(|v| v.iter().map(|id| ObstacleId(*id as usize - 1)).collect()),
    });
    let shared = scene
        .components
        .surfaces
        .meshes
        .bind(&f.config.shape_name, &f.model);
    let private = f.prepared.then(|| {
        scene
            .components
            .surfaces
            .meshes
            .private_copy(shared)
            .unwrap()
    });
    let mut state = factory::State::new(4).unwrap();
    for i in 0..2 {
        let entity = EntityId(i as u32 + 1);
        state.entities.new_record(entity, 100).unwrap();
        let record = state.entities.record_mut(entity).unwrap();
        record.class = Some(r.obstacle);
        record.object = Some(Handle::Obstacle(ObstacleId(i)));
        scene.world.colliders.push(Collider {
            entity,
            pose: Pose {
                position: if i == 0 { f.position } else { [0; 3] },
                matrix: f.basis.unwrap_or(Matrix::IDENTITY),
            },
            external_basis: f.basis,
            radius: 77,
            bounds: tankracer_game::geometry::Bounds {
                min: [0; 3],
                max: [0; 3],
            },
        });
        scene
            .components
            .falls
            .push(Some(tankracer_game::fall::State::default()));
        scene.world.objects.push(Object {
            class: r.obstacle,
            weight: 0,
            threshold: 0,
            activity: 0,
            flags: 0,
            mesh_bounds: [[0; 3]; 2],
        });
        scene.components.objects.push(Some(scene_objects::Object {
            impact_sound: None,
            data: PlacedObstacle {
                id: ObstacleId(i),
                cell: f.cell,
                source: None,
                mesh: Some(shared),
                visibility: Visibility {
                    distance: 0,
                    add_lap: 0,
                    remove_lap: 0,
                    entered: false,
                },
                sort_position: 0,
                impact_threshold: 0,
                collision_margin: 0,
                auxiliary: Some(f.config.clone()),
                debris: None,
            },
            auxiliary_mesh: if i == 0 { private } else { None },
            auxiliary_active: if i == 0 { f.active } else { 0 },
            auxiliary_motion: if i == 0 && f.prepared {
                Some(mesh_break::State {
                    faces: f.faces.clone(),
                    retired: f.retired,
                    ground_height: f.height,
                    prepared: true,
                    lifetime: f.lifetime,
                })
            } else {
                None
            },
            ground_height: None,
        }));
    }
    scene.components.ordinary = f.lists[0]
        .iter()
        .map(|v| ObstacleId(*v as usize - 1))
        .collect();
    scene.components.special = f.lists[1]
        .iter()
        .map(|v| ObstacleId(*v as usize - 1))
        .collect();
    state.ticks.flags[1] = f.base_flags;
    state.current_slot = f.slot;
    state.ticks.order = f.lists[2].iter().map(|v| EntityId(*v)).collect();
    state.queues.notifications = f.lists[3].iter().map(|v| EntityId(*v)).collect();
    state.queues.destruction = f.lists[4].iter().map(|v| EntityId(*v)).collect();
    (scene, state)
}
fn snapshot(
    out: &mut Vec<u32>,
    scene: &mut Scene,
    state: &factory::State<Handle>,
    inputs: &Inputs,
    outcome: (i32, u32),
    seed: u32,
) {
    let object = scene.components.objects[0].as_ref().unwrap();
    let display = if object.data.mesh.is_none() {
        0
    } else if object.data.mesh == object.auxiliary_mesh {
        2
    } else {
        1
    };
    out.extend([
        outcome.0 as u32,
        outcome.1,
        seed,
        state.current_slot,
        inputs.queries,
        object.auxiliary_active as u16 as u32,
        object.auxiliary_motion.as_ref().map_or(0, |v| v.retired),
        object
            .auxiliary_motion
            .as_ref()
            .map_or(0, |v| v.ground_height as u32),
        state.ticks.flags[1],
        display,
        u32::from(object.auxiliary_mesh.is_some()),
    ]);
    let properties = scene.world.objects[0];
    let fall = scene.components.falls[0].as_ref().unwrap();
    ints(
        out,
        [
            properties.activity,
            properties.flags as i32,
            fall.angular_velocity,
            fall.rotation,
            fall.turn_rate,
            fall.last_vehicle.map_or(0, |v| v.0) as i32,
        ],
    );
    ints(out, scene.world.colliders[0].pose.matrix.0);
    out.push(u32::from(scene.world.colliders[0].radius));
    if let Some(mesh) = object.auxiliary_mesh {
        let m = scene.components.surfaces.meshes.fragments(mesh).unwrap();
        out.push(u32::from(*m.radius));
        for f in &object.auxiliary_motion.as_ref().unwrap().faces {
            face_words(out, f);
        }
        for v in &m.geometry.vertices {
            ints(out, v.map(i32::from));
        }
        out.extend(m.geometry.triangles.iter().map(|t| u32::from(t.flags)));
        for c in &*m.centers {
            ints(out, c.as_ref().unwrap().iter().copied().map(i32::from));
        }
    }
    for l in [&scene.components.ordinary, &scene.components.special] {
        list(out, l.iter().map(|v| v.0 as u32 + 1));
    }
    list(out, state.ticks.order.iter().map(|v| v.0));
    list(out, state.queues.notifications.iter().map(|v| v.0));
    list(out, state.queues.destruction.iter().map(|v| v.0));
    for l in &scene.world.grid.cells {
        list(out, l.iter().map(|v| v.0 as u32 + 1));
    }
}
fn run(
    f: &Fixture,
    out: &mut Vec<u32>,
    ranges: &mut Vec<(std::ops::Range<usize>, String)>,
    case: usize,
) {
    let registry = registry(f);
    let (mut scene, mut state) = scene(f, &registry);
    let mut inputs = Inputs {
        name: f.config.shape_name.clone(),
        model: Arc::new(f.model.clone()),
        height_status: 0,
        height: 0,
        queries: 0,
        tail: f.tail,
    };
    let shared = scene.components.objects[1]
        .as_ref()
        .unwrap()
        .data
        .mesh
        .unwrap();
    let mut seed = f.seed;
    let mut peer = collision::State {
        entity: EntityId(if f.velocity.is_some() { 2 } else { 0 }),
        class: ClassId(0),
        base_position: [0; 3],
        reference_basis: tankracer_game::math::Matrix::IDENTITY,
        external_basis: None,
        position: [0; 3],
        previous_position: [0; 3],
        velocity: f.velocity.unwrap_or([0; 3]),
        forward_velocity: 0,
        player: false,
        contacts: [[0; 3]; 4],
        impact_accumulator: 0,
        collision_size: 0,
        material_tag: 0,
        deformable: None,
    };
    for (step, [op, dt, mode, status, height, limit]) in f.steps.iter().copied().enumerate() {
        inputs.height_status = status as i32;
        inputs.height = height as i32;
        scene.components.dt = dt as i32;
        scene.components.mode = mode as i32;
        let mut result = 0x7182;
        let status = if op == 1 || op == 3 {
            let hit = registry.hit_environment(scene.components.fall_tuning, false, limit);
            let (lifecycle, current_slot) = state.split();
            Collision {
                components: &mut scene.components,
                lifecycle,
                environment: registry.environment(limit),
            }
            .method(
                if op == 1 {
                    scene_objects::AUXILIARY_HIT
                } else {
                    tankracer_game::breakable::BREAKABLE_HIT
                },
                Invocation {
                    dt: dt as i32,
                    target: ObstacleId(0),
                    vehicle: &mut peer,
                    world: &mut scene.world,
                    random: &mut seed,
                    current_slot,
                    result: &mut result,
                },
                hit,
                &mut inputs,
            )
        } else {
            scene.updates(&mut seed).invoke(
                if op == 4 {
                    scene_objects::FALL_TICK
                } else {
                    scene_objects::AUXILIARY_TICK
                },
                factory::Invocation {
                    context: state.context(),
                    entity: EntityId(1),
                    payload: None,
                    result: &mut result,
                },
                registry.environment(limit),
            )
        }
        .err()
        .unwrap_or(0);
        let original = scene.components.surfaces.meshes.fragments(shared).unwrap();
        assert_eq!(
            original.geometry.vertices, inputs.model.geometry.vertices,
            "shared vertices changed"
        );
        assert_eq!(
            original
                .geometry
                .triangles
                .iter()
                .map(|t| t.flags)
                .collect::<Vec<_>>(),
            inputs
                .model
                .geometry
                .triangles
                .iter()
                .map(|t| t.flags)
                .collect::<Vec<_>>(),
            "shared faces changed"
        );
        assert_eq!(
            *original.centers, inputs.model.face_centers,
            "shared centers changed"
        );
        let start = out.len();
        snapshot(out, &mut scene, &state, &inputs, (status, result), seed);
        ranges.push((
            start..out.len(),
            format!(
                "case={case} step={step} op={op} limit={limit} dt={dt} prepared={}",
                f.prepared
            ),
        ));
    }
}
#[test]
#[ignore = "requires native mesh-break oracle; set TANKRACER_MESH_BREAK_REFERENCE"]
fn private_resources_pairing_vertex_updates_lifetime_and_cleanup_match_c() {
    let cases = fixtures();
    let (mut input, mut expected, mut ranges) = (vec![], vec![], vec![]);
    for (case, f) in cases.iter().enumerate() {
        encode(f, &mut input);
        run(f, &mut expected, &mut ranges, case);
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/mesh_break");
    fs::create_dir_all(&folder).unwrap();
    let path = folder.join("input.bin");
    fs::write(&path, input).unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_MESH_BREAK_REFERENCE")
            .expect("set TANKRACER_MESH_BREAK_REFERENCE"),
    )
    .stdin(Stdio::from(fs::File::open(path).unwrap()))
    .output()
    .unwrap();
    assert!(
        output.status.success(),
        "{:?} {}",
        output.status.code(),
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"MB01");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
        .collect();
    if let Some(i) = actual.iter().zip(&expected).position(|(a, b)| a != b) {
        let (_, label) = ranges.iter().find(|(r, _)| r.contains(&i)).unwrap();
        panic!(
            "first difference {i} {label}; C={:?} Rust={:?}",
            &actual[i.saturating_sub(3)..(i + 8).min(actual.len())],
            &expected[i.saturating_sub(3)..(i + 8).min(expected.len())]
        );
    }
    assert_eq!(actual.len(), expected.len());
    let errors = ranges
        .iter()
        .filter(|(r, _)| expected[r.start] != 0)
        .count();
    assert!(errors > 0);
    eprintln!(
        "{} mesh-break scenarios / {} operations / {} words exact; {errors} error prefixes",
        cases.len(),
        ranges.len(),
        expected.len()
    );
}

#[test]
fn authored_replacement_models_load_and_animate_without_mutating_shared_resources() {
    use tankracer_assets::{
        archive::Archive, level::Level, model::ShapeArchive, parameters::Parameters,
    };
    use tankracer_game::vehicle_model::{Bundles, ModelBank, Space};
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game/LEVELS");
    let shared_bytes = fs::read(root.parent().unwrap().join("INGAME/FASTFILE.DAT")).unwrap();
    let shared_archive = Archive::parse(&shared_bytes).unwrap();
    let mut count = 0;
    let mut out = vec![];
    let mut ranges = vec![];
    for map in [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ] {
        let bytes = fs::read(root.join(map).join("FASTFILE.DAT")).unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        let source = Bundles {
            vehicle: vec![],
            shared: shared_archive
                .entries()
                .iter()
                .filter(|e| e.name.ends_with(".SS"))
                .map(|e| ShapeArchive::parse(shared_archive.get(e.name).unwrap()).unwrap())
                .collect(),
            level: archive
                .entries()
                .iter()
                .filter(|e| e.name.ends_with(".SS"))
                .map(|e| ShapeArchive::parse(archive.get(e.name).unwrap()).unwrap())
                .collect(),
        };
        let mut names = std::collections::BTreeSet::new();
        for cup in ["B", "S", "G"] {
            let script = format!("{}{cup}.DAT", &map[..3]);
            let Some(script) = archive.get(&script) else {
                continue;
            };
            let level = Level::decode(script).unwrap();
            for record in level.objects {
                let p = Parameters::new(record.parameters).unwrap();
                if let Some(mut v) = p.find(":shatter").or_else(|| p.find(":Shatter")) {
                    names.insert(v.word().unwrap().to_ascii_lowercase());
                }
            }
        }
        let mut models = ModelBank::default();
        // Same INGAME preloads qualified by the map-startup oracle, spanning
        // bronze/silver/gold resources because this resource test covers all cups.
        for name in [
            "cmine.sm",
            "chomer.sm",
            "cufo.sm",
            "cconc.sm",
            "cturbo.sm",
            "cshield.sm",
            "cemine.sm",
            "cwarp.sm",
        ] {
            models.load(&source, Space::Shared, name).unwrap();
        }
        for name in names {
            let model = models
                .load_optional(&source, Space::Level, &name)
                .unwrap()
                .unwrap_or_else(|| panic!("{map} missing {name}"));
            let faces = model.geometry.triangles.len();
            assert!(
                model
                    .face_centers
                    .iter()
                    .all(|c| c.as_ref().is_some_and(|v| v.len() == faces)),
                "{map} {name} missing centers"
            );
            let mut f = Fixture {
                prepared: false,
                model: (*model).clone(),
                faces: vec![Face::default(); faces],
                steps: vec![
                    [1, 40, 0, 0, 0, 100000],
                    [2, 16, 0, 0, 0, 100000],
                    [2, 4000, 0, 0, 0, 100000],
                ],
                ..Fixture::default()
            };
            f.config.shape_name = name;
            run(&f, &mut out, &mut ranges, count);
            for (r, label) in &ranges {
                assert_eq!(out[r.start], 0, "{map} {} {label}", f.config.shape_name);
            }
            out.clear();
            ranges.clear();
            count += 1;
        }
    }
    assert!(count > 0);
    eprintln!(
        "{count} authored map/replacement resource bindings loaded, animated and retired; shared geometry/centers preserved"
    );
}
