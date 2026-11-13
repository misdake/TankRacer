use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
    sync::Arc,
};
use tankracer_assets::{config::Gain, mesh::Geometry, model::Model};
use tankracer_game::{
    breakable, crush, deformable_hit, deformation,
    dispatch::{self, Class, ClassId},
    entity_factory::{self as factory, Environment, Payload, Slots},
    geometry::Bounds,
    lifecycle,
    math::Matrix,
    object_query::{Collider, EntityId, ObstacleId},
    obstacle_collision::{self, Grid, World},
    obstacle_startup::{Auxiliary, Debris, PlacedObstacle},
    pose::Pose,
    scene_collision::{Deformable, Sampling, Surfaces},
    scene_meshes::Meshes,
    scene_objects::{self as methods, Interfaces, Object, Parents, Scene},
    scene_startup::Visibility,
    spatial::{self, Cell},
};

const UNCHANGED: u32 = 0x7182;
const ENTER: u32 = 6;
const LEAVE: u32 = 7;
const DEACTIVATE: u32 = 8;
const MODE: u32 = 9;
const MOVE: u32 = 10;
const COLLIDABLE: u32 = 11;
const TICK: u32 = 12;

#[derive(Clone)]
struct Fixture {
    crush: bool,
    parent_max: i32,
    pose: Pose,
    external: bool,
    cell: u32,
    sort: i16,
    weight: i32,
    flags: [u32; 3],
    shape: bool,
    auxiliary: bool,
    debris: bool,
    config: deformable_hit::Config,
    cap: i32,
    highlighted: bool,
    dirty: u8,
    siren: i32,
    records_attached: bool,
    private_mesh: bool,
    records: Vec<deformation::Record>,
    lists: [Vec<u32>; 6],
    grid: [Vec<u32>; 64],
    steps: Vec<[u32; 5]>,
}
impl Default for Fixture {
    fn default() -> Self {
        Self {
            crush: false,
            parent_max: 15,
            pose: Pose {
                position: [80, -400, -80],
                matrix: Matrix([3, 4, 5, 7, 8, 9, 13, 17, 19]),
            },
            external: false,
            cell: 0,
            sort: 23,
            weight: 123,
            flags: [8; 3],
            shape: true,
            auxiliary: true,
            debris: true,
            config: deformable_hit::Config {
                floor: 17,
                compression: 9,
                drift_shift: 3,
                material: 0,
                has_sound: false,
            },
            cap: 77123,
            highlighted: false,
            dirty: 0,
            siren: 0,
            records_attached: true,
            private_mesh: false,
            records: vec![deformation::Record {
                state: 0,
                timer: 7,
                color: [255, 64, 0],
                ..Default::default()
            }],
            lists: std::array::from_fn(|_| vec![]),
            grid: std::array::from_fn(|_| vec![]),
            steps: vec![],
        }
    }
}
fn classes(parent_max: i32) -> Vec<Arc<Class>> {
    let mut world = Class {
        maximum_slot: parent_max,
        methods: vec![
            None,
            Some(methods::RESET_POSE),
            Some(methods::RELEASE_OBJECT),
            Some(methods::DETACH),
            Some(breakable::ENABLE_TICK),
            Some(methods::DISABLE_TICK),
            None,
            None,
            Some(methods::QUEUE_DESTROY),
            None,
            None,
            None,
            None,
            None,
            Some(methods::ACTIVATE),
            Some(methods::NOTIFY),
        ],
    };
    let mut generic = world.clone();
    generic.maximum_slot = 15;
    generic.methods[1] = Some(methods::WORLD_INITIALIZE);
    let mut obstacle = generic.clone();
    for (slot, method) in [
        (1, methods::OBSTACLE_INITIALIZE),
        (2, methods::DESTROY),
        (ENTER, methods::ENTER),
        (LEAVE, methods::LEAVE),
        (DEACTIVATE, methods::DEACTIVATE),
        (MODE, methods::SET_MODE),
        (MOVE, methods::MOVE),
        (COLLIDABLE, methods::COLLIDABLE),
        (13, methods::DESTROY_REQUEST),
    ] {
        obstacle.methods[slot as usize] = Some(method);
    }
    let mut crush = obstacle.clone();
    for (slot, method) in [
        (1, methods::CRUSH_INITIALIZE),
        (2, methods::CRUSH_DESTROY),
        (ENTER, methods::CRUSH_ENTER),
        (LEAVE, methods::CRUSH_LEAVE),
        (MODE, methods::CRUSH_HIGHLIGHT),
        (TICK, crush::TICK),
    ] {
        crush.methods[slot as usize] = Some(method);
    }
    // Only World has the out-of-range branch; its descendants keep all slots.
    world.maximum_slot = parent_max;
    vec![
        Arc::new(world),
        Arc::new(generic),
        Arc::new(obstacle),
        Arc::new(crush),
    ]
}
fn make(f: &Fixture, classes: &[Arc<Class>]) -> (factory::State<ObstacleId>, Scene) {
    let model = Model {
        geometry: Geometry {
            vertices: vec![[0; 3]],
            triangles: vec![],
        },
        materials: vec![],
        radius: 64,
        render_hint: 0,
        bounds: [[0; 3]; 2],
        vertex_normals: None,
    };
    let mut meshes = Meshes::default();
    let shared = meshes.bind("fixture.sm", &model);
    let mesh = if f.private_mesh {
        meshes.private_copy(shared).unwrap()
    } else {
        shared
    };
    let auxiliary_mesh = f.auxiliary.then(|| meshes.private_copy(mesh).unwrap());
    let mut state = factory::State::new(16).unwrap();
    state.current_slot = 19;
    let mut scene = Scene::new(
        World {
            colliders: vec![],
            objects: vec![],
            classes: classes.to_vec(),
            grid: Grid {
                origin: [-5000; 2],
                shifts: [12; 2],
                cells: f
                    .grid
                    .each_ref()
                    .map(|v| v.iter().map(|id| ObstacleId(*id as usize - 1)).collect()),
            },
            deformables: f.lists[2]
                .iter()
                .map(|v| ObstacleId(*v as usize - 1))
                .collect(),
        },
        Surfaces {
            entries: vec![],
            meshes,
        },
        Parents {
            world: ClassId(0),
            generic: ClassId(1),
            obstacle: ClassId(2),
        },
        Interfaces {
            activate: 4,
            deactivate: DEACTIVATE,
            auxiliary_tick: 16,
            debris_tick: 17,
        },
    );
    let gain = Gain {
        multiplier: 1,
        shift: 0,
    };
    for id in 1..=3 {
        let target = ObstacleId(id as usize - 1);
        let first = id == 1;
        let class = ClassId(if first && f.crush { 3 } else { 2 });
        state.entities.new_record(EntityId(id), 1000).unwrap();
        let record = state.entities.record_mut(EntityId(id)).unwrap();
        record.class = Some(class);
        record.object = Some(target);
        state.ticks.flags[id as usize] = f.flags[id as usize - 1];
        let pose = if first {
            f.pose
        } else {
            Pose {
                position: [0; 3],
                matrix: Matrix([0; 9]),
            }
        };
        scene.world.colliders.push(Collider {
            entity: EntityId(id),
            pose,
            external_basis: (first && f.external).then_some(pose.matrix),
            radius: if first && f.shape { 64 } else { 0 },
            bounds: Bounds {
                min: [0; 3],
                max: [0; 3],
            },
        });
        scene.world.objects.push(obstacle_collision::Object {
            class,
            weight: if first { f.weight } else { 0 },
            threshold: 0,
            activity: 0,
            flags: 0,
            mesh_bounds: [[0; 3]; 2],
        });
        scene.objects.push(Some(Object {
            data: PlacedObstacle {
                id: target,
                cell: Cell::unpack(if first { f.cell } else { 0 }),
                source: None,
                mesh: (first && f.shape).then_some(mesh),
                visibility: Visibility {
                    distance: 0,
                    add_lap: 0,
                    remove_lap: 0,
                    entered: false,
                },
                sort_position: if first { f.sort } else { 0 },
                impact_threshold: 0,
                collision_margin: 0,
                auxiliary: (first && f.auxiliary).then(|| Auxiliary {
                    shape_name: "fixture.sm".into(),
                    gravity: 0,
                    bounce: gain,
                    friction: gain,
                    push: gain,
                    velocity: gain,
                    spin: gain,
                }),
                debris: (first && f.debris).then_some(Debris {
                    pieces: vec![],
                    retired: 0,
                    flags: 128,
                    sprite: None,
                    animation: None,
                    offset: [0; 3],
                    ground_height: 0,
                    gravity: 0,
                    friction: 0,
                    direction: 0,
                }),
            },
            auxiliary_mesh: if first { auxiliary_mesh } else { None },
            auxiliary_active: 0,
            ground_height: None,
        }));
    }
    scene.ordinary = f.lists[0]
        .iter()
        .map(|v| ObstacleId(*v as usize - 1))
        .collect();
    scene.special = f.lists[1]
        .iter()
        .map(|v| ObstacleId(*v as usize - 1))
        .collect();
    state.ticks.order = f.lists[3].iter().copied().map(EntityId).collect();
    state.queues.notifications = f.lists[4].iter().copied().map(EntityId).collect();
    state.queues.destruction = f.lists[5].iter().copied().map(EntityId).collect();
    if f.crush {
        scene.surfaces.entries.push(Some(Deformable {
            config: f.config,
            state: Default::default(),
            mesh,
            vertex_flags: vec![0],
            sampling: Sampling {
                bounds: [[0; 2]; 2],
                edge_cap: f.cap,
                state: deformation::State {
                    dirty: f.dirty,
                    records_attached: f.records_attached,
                    records: f.records.clone(),
                    ..Default::default()
                },
            },
            runtime: crush::State {
                siren_timer: f.siren,
                highlighted: f.highlighted,
                private_mesh: f.private_mesh,
                ..Default::default()
            },
        }));
    }
    (state, scene)
}
fn code(error: factory::Error) -> i32 {
    match error {
        factory::Error::Dispatch(dispatch::Error::Method(v)) => v,
        factory::Error::Lifecycle(lifecycle::Error::Spatial(spatial::Error::InvalidLimit)) => -1,
        factory::Error::Lifecycle(lifecycle::Error::Spatial(spatial::Error::Limit)) => -3,
        v => panic!("invalid oracle fixture: {v:?}"),
    }
}
fn operation(
    state: &mut factory::State<ObstacleId>,
    scene: &mut Scene,
    classes: &[Arc<Class>],
    op: [u32; 5],
) -> Result<u32, i32> {
    let [op, a, b, c, limit] = op;
    scene.dt = a as i32;
    let env = Environment {
        classes,
        slots: Slots {
            allocate: 0,
            initialize: 1,
            destroy: 2,
            release: 3,
        },
        work_limit: limit,
    };
    match op {
        7 => {
            state
                .context()
                .destroy(EntityId(1), env, scene)
                .map_err(code)?;
            Ok(UNCHANGED)
        }
        9 => {
            state.context().tick(TICK, env, scene).map_err(code)?;
            Ok(UNCHANGED)
        }
        10 => {
            state.context().drain(5, env, scene).map_err(code)?;
            Ok(UNCHANGED)
        }
        _ => {
            let slots = [
                0, ENTER, LEAVE, DEACTIVATE, MODE, MOVE, COLLIDABLE, 0, 1, 0, 0, 15, 13, 14,
            ];
            let payload = match op {
                4 | 6 => Some(Payload::Value(a)),
                5 => Some(Payload::Position([a as i32, b as i32, c as i32])),
                _ => None,
            };
            state
                .context()
                .dispatch(EntityId(1), slots[op as usize], payload, env, scene)
                .map_err(code)
        }
    }
}
fn words_record(out: &mut Vec<u32>, r: &deformation::Record) {
    out.extend(
        r.position
            .into_iter()
            .chain(r.world_position)
            .map(|v| v as u32),
    );
    out.extend([
        u32::from_le_bytes([r.state, r.color[0], r.color[1], r.color[2]]),
        r.timer as u32,
    ]);
}
fn words_list(out: &mut Vec<u32>, values: impl IntoIterator<Item = u32>) {
    let start = out.len();
    out.push(0);
    out.extend(values);
    out[start] = (out.len() - start - 1) as u32;
}
fn encode(out: &mut Vec<u32>, f: &Fixture) {
    out.extend([
        f.steps.len() as u32,
        u32::from(f.crush),
        f.parent_max as u32,
    ]);
    out.extend(f.pose.position.map(|v| v as u32));
    out.extend(f.pose.matrix.0.map(|v| v as u32));
    out.extend([
        u32::from(f.external),
        f.cell,
        f.sort as u32,
        f.weight as u32,
        f.flags[0],
        u32::from(f.shape),
        u32::from(f.auxiliary),
        u32::from(f.debris),
    ]);
    if f.crush {
        out.extend([
            f.config.floor as u32,
            f.config.compression as u32,
            f.cap as u32,
            f.config.drift_shift,
            u32::from(f.highlighted),
            u32::from(f.dirty),
            f.siren as u32,
            u32::from(f.records_attached),
            u32::from(f.private_mesh),
            f.records.len() as u32,
        ]);
        for r in &f.records {
            words_record(out, r);
        }
    }
    out.extend(&f.flags[1..]);
    for list in f.lists.iter().chain(f.grid.iter()) {
        words_list(out, list.iter().copied());
    }
    out.extend(f.steps.iter().flatten());
}
fn snapshot(
    out: &mut Vec<u32>,
    state: &factory::State<ObstacleId>,
    scene: &Scene,
    status: i32,
    value: u32,
    is_crush: bool,
) {
    out.extend([
        status as u32,
        value,
        state.current_slot,
        state.entities.record_count(),
    ]);
    for id in 1..=3 {
        let record = state.entities.record(EntityId(id));
        out.push(u32::from(record.is_some()));
        let Some(record) = record else {
            continue;
        };
        let target = record.object.unwrap();
        let object = scene.objects[target.0].as_ref().unwrap();
        let collider = &scene.world.colliders[target.0];
        out.extend(collider.pose.position.map(|v| v as u32));
        out.extend(collider.pose.matrix.0.map(|v| v as u32));
        out.extend([
            u32::from(collider.external_basis.is_some()),
            scene.world.objects[target.0].weight as u32,
            object.data.cell.map_or(u32::MAX, Cell::packed),
            object.data.sort_position as u32,
            state.ticks.flags[id as usize],
            u32::from(object.data.auxiliary.is_some()),
            u32::from(object.data.debris.is_some()),
            u32::from(object.data.mesh.is_some()),
        ]);
        if id == 1 && is_crush {
            let surface = scene.surfaces.entries[0].as_ref().unwrap();
            out.extend([
                surface.config.floor as u32,
                surface.config.compression as u32,
                surface.sampling.edge_cap as u32,
                surface.config.drift_shift,
                u32::from(surface.runtime.highlighted),
                u32::from(surface.sampling.state.dirty),
                u32::from(surface.sampling.state.records_attached),
                surface.runtime.siren_timer as u32,
                u32::from(!surface.vertex_flags.is_empty()),
            ]);
            for r in &surface.sampling.state.records {
                words_record(out, r);
            }
        }
    }
    out.push(state.ticks.next.map_or(0, |v| v.0));
    for list in [&scene.ordinary, &scene.special, &scene.world.deformables] {
        words_list(out, list.iter().map(|v| v.0 as u32 + 1));
    }
    words_list(out, state.ticks.order.iter().map(|v| v.0));
    words_list(out, state.queues.notifications.iter().map(|v| v.0));
    words_list(out, state.queues.destruction.iter().map(|v| v.0));
    for cell in &scene.world.grid.cells {
        words_list(out, cell.iter().map(|v| v.0 as u32 + 1));
    }
}
fn step(op: u32, a: u32, limit: u32) -> [u32; 5] {
    [op, a, 0, 0, limit]
}
fn fixtures() -> Vec<Fixture> {
    let mut result = vec![];
    for is_crush in [false, true] {
        for parent_max in [0, 15] {
            for shape in [false, true] {
                let f = Fixture {
                    crush: is_crush,
                    parent_max,
                    shape,
                    ..Default::default()
                };
                for op in 1..=13 {
                    for limit in 0..=4 {
                        let mut f = f.clone();
                        f.steps = vec![step(op, 1, limit)];
                        // Tick the target and a null-method peer through the real scheduler.
                        if op == 9 {
                            f.lists[3] = vec![1, 2];
                            f.flags[0] |= 16;
                            f.flags[1] |= 16;
                        }
                        for list in &mut f.lists[..3] {
                            *list = vec![2, 1, 3, 1];
                        }
                        for i in [0, 1, 8, 9] {
                            f.grid[i] = vec![1, 3, 2, 1];
                        }
                        result.push(f);
                    }
                }
                // Normal entry/mode/move/exit/deactivate/full destruction.
                let mut sequence = f.clone();
                sequence.steps = vec![
                    step(8, 0, 20),
                    step(1, 0, 20),
                    step(4, 1, 20),
                    [5, 10000, (-300_i32) as u32, 7000, 20],
                    step(6, 0, 20),
                    step(6, 1, 20),
                    step(4, 2, 20),
                    step(2, 0, 20),
                    step(10, 0, 20),
                    step(13, 0, 20),
                    step(12, 0, 20),
                    step(10, 0, 20),
                ];
                result.push(sequence);
                let mut shutdown = f;
                shutdown.steps = vec![
                    step(1, 0, 20),
                    step(9, 16, 20),
                    step(3, 0, 20),
                    step(10, 0, 20),
                ];
                result.push(shutdown);
            }
        }
    }
    // Boundary-rich short sequences, no synthetic errors injected into methods.
    let mut random = 0xc6b01e93_u32;
    let mut word = || {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        random
    };
    for _ in 0..128 {
        let mut f = Fixture {
            crush: word() & 1 != 0,
            external: word() & 1 != 0,
            shape: word() & 1 != 0,
            auxiliary: word() & 1 != 0,
            debris: word() & 1 != 0,
            cell: [u32::MAX, 0, 0x10001, 0x70007, 0xffff0000][word() as usize % 5],
            weight: word() as i32,
            sort: word() as i16,
            ..Default::default()
        };
        f.private_mesh = f.crush && f.shape && word() & 1 != 0;
        f.pose.position = std::array::from_fn(|_| word() as i32);
        f.flags[0] = if word() & 1 != 0 { 8 } else { 0 };
        f.records = (0..word() % 6)
            .map(|_| deformation::Record {
                state: (word() % 5) as u8,
                timer: word() as i32,
                color: [word() as u8, word() as u8, word() as u8],
                ..Default::default()
            })
            .collect();
        f.records_attached = true;
        f.dirty = (word() % 2) as u8;
        f.siren = [0, 11025, 11026, 22050, i32::MAX][word() as usize % 5];
        for list in &mut f.lists[..3] {
            *list = (0..word() % 5).map(|_| 1 + word() % 3).collect();
        }
        for cell in &mut f.grid {
            *cell = (0..word() % 4).map(|_| 1 + word() % 3).collect();
        }
        f.steps = vec![
            step(1, 0, 20),
            step(9, word() % 1501, 20),
            step(4, word() % 3, 20),
            [5, word(), word(), word(), 20],
            step(2, 0, 20),
            step(10, 0, 20),
            step(3, 0, 20),
            step(10, 0, 20),
        ];
        result.push(f);
    }
    result
}
#[test]
#[ignore = "requires production C dispatcher oracle; set TANKRACER_SCENE_OBJECTS_REFERENCE"]
fn common_parent_and_crush_bindings_match_production_dispatcher() {
    let fixtures = fixtures();
    let (mut input, mut expected, mut ranges) = (vec![], vec![], vec![]);
    let (mut errors, mut retired) = (0, 0);
    for (case, fixture) in fixtures.iter().enumerate() {
        encode(&mut input, fixture);
        let classes = classes(fixture.parent_max);
        let (mut state, mut scene) = make(fixture, &classes);
        for (index, &op) in fixture.steps.iter().enumerate() {
            let (status, value) = match operation(&mut state, &mut scene, &classes, op) {
                Ok(v) => (0, v),
                Err(v) => {
                    errors += 1;
                    (v, UNCHANGED)
                }
            };
            retired += usize::from(scene.objects[0].is_none());
            let start = expected.len();
            snapshot(&mut expected, &state, &scene, status, value, fixture.crush);
            ranges.push((
                start..expected.len(),
                format!("case {case} step {index} op {op:?}"),
            ));
        }
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/scene_objects");
    fs::create_dir_all(&folder).unwrap();
    let input_path = folder.join("input.bin");
    fs::write(
        &input_path,
        input
            .iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_SCENE_OBJECTS_REFERENCE")
            .expect("set TANKRACER_SCENE_OBJECTS_REFERENCE"),
    )
    .stdin(Stdio::from(fs::File::open(input_path).unwrap()))
    .output()
    .unwrap();
    fs::write(folder.join("output.bin"), &output.stdout).unwrap();
    assert!(
        output.status.success(),
        "{:?} {}",
        output.status.code(),
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"SO01");
    let words: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
        .collect();
    if let Some(index) = words.iter().zip(&expected).position(|(a, b)| a != b) {
        let (range, label) = ranges.iter().find(|(r, _)| r.contains(&index)).unwrap();
        panic!(
            "{label}, offset {}: C {}, Rust {}",
            index - range.start,
            words[index],
            expected[index]
        );
    }
    assert_eq!(words.len(), expected.len());
    assert!(errors > 10 && retired > 10);
    eprintln!(
        "{} scene scenarios / {} operations / {} semantic words exact; {errors} error prefixes, {retired} retired observations",
        fixtures.len(),
        ranges.len(),
        words.len()
    );
}
