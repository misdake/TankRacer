use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
    sync::Arc,
};
use tankracer_assets::{mesh::Geometry, model::Model};
use tankracer_game::{
    crush, deformable_hit, deformation,
    dispatch::{Class, ClassId, MethodId},
    entity_factory::{self as factory, BaseMethod, Environment, Invocation, Methods, Slots},
    object_query::{EntityId, ObstacleId},
    scene_collision::{Deformable, Sampling},
    scene_meshes::{MeshId, Meshes},
};
const UNCHANGED: u32 = 0x7182;
#[derive(Clone)]
struct Fixture {
    position: [i32; 3],
    camera: [i32; 3],
    runtime: crush::State,
    sampling: deformation::State,
    flags: [u32; 3],
    lists: [Vec<u32>; 5],
    vertex_flags: bool,
    activate_status: i32,
    parent_status: i32,
    steps: Vec<[u32; 5]>,
}
impl Default for Fixture {
    fn default() -> Self {
        Self {
            position: [80, 0, -80],
            camera: [0; 3],
            runtime: crush::State {
                sound_channel: 99,
                ..Default::default()
            },
            sampling: deformation::State {
                records_attached: true,
                records: vec![
                    deformation::Record {
                        state: 0,
                        timer: 0,
                        color: [255, 64, 0],
                        ..Default::default()
                    },
                    deformation::Record {
                        state: 1,
                        timer: 50,
                        color: [32, 0, 255],
                        ..Default::default()
                    },
                ],
                ..Default::default()
            },
            flags: [8; 3],
            lists: [vec![1, 2, 1], vec![3, 1], vec![], vec![], vec![]],
            vertex_flags: true,
            activate_status: 0,
            parent_status: 0,
            steps: vec![],
        }
    }
}
struct Scene {
    surface: Deformable,
    meshes: Meshes,
    shared: MeshId,
    collision: Vec<ObstacleId>,
    drawing: Vec<ObstacleId>,
    trace: Vec<[u32; 3]>,
    dt: i32,
    activate_status: i32,
    parent_status: i32,
    releases: u32,
    sample: deformation::Sample,
}
fn error_code(error: factory::Error) -> i32 {
    match error {
        factory::Error::Dispatch(tankracer_game::dispatch::Error::Method(v)) => v,
        factory::Error::Lifecycle(v) => crush::code(crush::Error::Lifecycle(v)),
        v => panic!("invalid fixture {v:?}"),
    }
}
impl Methods<ObstacleId> for Scene {
    fn invoke(
        &mut self,
        method: MethodId,
        call: Invocation<'_, ObstacleId>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        let kind = match method {
            crush::TICK => 0,
            MethodId(17) => 1,
            MethodId(18) => 2,
            _ => unreachable!(),
        };
        self.trace
            .push([kind, call.entity.0, *call.context.current_slot]);
        match kind {
            0 if call.entity == EntityId(1) => {
                crush::invoke_tick(&mut self.surface, call, env, self.dt)
            }
            0 => {
                *call.result = call.entity.0;
                Ok(())
            }
            1 => {
                BaseMethod::EnableTick.invoke(call, env)?;
                if self.activate_status != 0 {
                    Err(self.activate_status)
                } else {
                    Ok(())
                }
            }
            2 => BaseMethod::DisableTick.invoke(call, env),
            _ => unreachable!(),
        }
    }
}
fn make(f: &Fixture) -> (factory::State<ObstacleId>, Scene) {
    let model = Model {
        geometry: Geometry {
            vertices: vec![[0, 200, 0], [10, 150, 10], [20, 100, 20]],
            triangles: vec![],
        },
        materials: vec![],
        radius: 0,
        render_hint: 0,
        bounds: [[0; 3]; 2],
        vertex_normals: None,
    };
    let mut meshes = Meshes::default();
    let shared = meshes.bind("car.sm", &model);
    let mesh = if f.runtime.private_mesh {
        meshes.private_copy(shared).unwrap()
    } else {
        shared
    };
    let mut state = factory::State::new(4).unwrap();
    state.current_slot = 4;
    for id in 1..=3 {
        state.entities.new_record(EntityId(id), 1000).unwrap();
        let record = state.entities.record_mut(EntityId(id)).unwrap();
        record.class = Some(ClassId(0));
        record.object = Some(ObstacleId(id as usize - 1));
        state.ticks.flags[id as usize] = f.flags[id as usize - 1];
    }
    state.ticks.order = f.lists[2].iter().copied().map(EntityId).collect();
    state.queues.notifications = f.lists[3].iter().copied().map(EntityId).collect();
    state.queues.destruction = f.lists[4].iter().copied().map(EntityId).collect();
    let mut sampling = f.sampling.clone();
    sampling.pose.origin = f.position;
    (
        state,
        Scene {
            surface: Deformable {
                config: deformable_hit::Config {
                    floor: 50,
                    compression: 1,
                    drift_shift: 1,
                    material: 0,
                    has_sound: false,
                },
                state: Default::default(),
                mesh,
                vertex_flags: if f.vertex_flags {
                    vec![4, 1, 0]
                } else {
                    vec![]
                },
                sampling: Sampling {
                    bounds: [[-2000, 2000]; 2],
                    edge_cap: 45000,
                    state: sampling,
                },
                runtime: f.runtime.clone(),
            },
            meshes,
            shared,
            collision: f.lists[0]
                .iter()
                .map(|v| ObstacleId(*v as usize - 1))
                .collect(),
            drawing: f.lists[1]
                .iter()
                .map(|v| ObstacleId(*v as usize - 1))
                .collect(),
            trace: vec![],
            dt: 0,
            activate_status: f.activate_status,
            parent_status: f.parent_status,
            releases: 0,
            sample: deformation::Sample::default(),
        },
    )
}
fn operation(
    state: &mut factory::State<ObstacleId>,
    scene: &mut Scene,
    classes: &[Arc<Class>],
    step: [u32; 5],
) -> Result<u32, i32> {
    let [op, a, b, c, limit] = step;
    let env = Environment {
        classes,
        slots: Slots {
            allocate: 0,
            initialize: 1,
            destroy: 0,
            release: 0,
        },
        work_limit: limit,
    };
    scene.dt = a as i32;
    match op {
        1 => {
            crush::tick(
                &mut scene.surface,
                a as i32,
                EntityId(1),
                &mut state.ticks,
                &mut state.queues,
                limit,
            )
            .map_err(crush::code)?;
            Ok(1)
        }
        2 => {
            let activate = crush::enter(
                ObstacleId(0),
                &mut scene.collision,
                &mut scene.drawing,
                scene.surface.sampling.state.records_attached,
                limit,
            )
            .map_err(crush::code)?;
            if activate {
                state
                    .context()
                    .dispatch(EntityId(1), 1, None, env, scene)
                    .map_err(error_code)?;
            }
            Ok(u32::from(activate))
        }
        3 => crush::leave(
            ObstacleId(0),
            EntityId(1),
            crush::Membership {
                collision: &mut scene.collision,
                drawing: &mut scene.drawing,
            },
            scene.surface.sampling.state.records_attached,
            &mut state.ticks,
            &mut state.queues,
            limit,
        )
        .map(u32::from)
        .map_err(crush::code),
        4 => {
            crush::set_highlight(&mut scene.surface, a);
            Ok(1)
        }
        5 => {
            let private_live = scene.surface.runtime.private_mesh
                && scene.meshes.get(scene.surface.mesh).is_some();
            crush::destroy_owned(&mut scene.surface, &mut scene.meshes, limit)
                .map_err(crush::code)?;
            scene.releases += u32::from(private_live);
            scene.trace.push([3, 1, state.current_slot]);
            state.current_slot = state.current_slot.wrapping_add(7);
            if scene.parent_status != 0 {
                Err(scene.parent_status)
            } else {
                Ok(1)
            }
        }
        6 => {
            state.context().drain(2, env, scene).map_err(error_code)?;
            Ok(UNCHANGED)
        }
        7 => {
            state.context().tick(0, env, scene).map_err(error_code)?;
            Ok(UNCHANGED)
        }
        8 => {
            scene.sample = scene.surface.sampling.state.sample(
                &deformation::Geometry {
                    sample_bounds: scene.surface.sampling.bounds,
                    edges: [-16000, 16000, -16000, 16000],
                    edge_cap: 45000,
                    vertices: &scene.meshes.get(scene.surface.mesh).unwrap().vertices,
                },
                [a as i32, b as i32, c as i32],
            );
            Ok(UNCHANGED)
        }
        _ => unreachable!(),
    }
}
fn words_record(out: &mut Vec<u32>, record: &deformation::Record) {
    out.extend(
        record
            .position
            .into_iter()
            .chain(record.world_position)
            .map(|v| v as u32),
    );
    out.extend([
        u32::from_le_bytes([
            record.state,
            record.color[0],
            record.color[1],
            record.color[2],
        ]),
        record.timer as u32,
    ]);
}
fn words_list(out: &mut Vec<u32>, values: impl IntoIterator<Item = u32>) {
    let start = out.len();
    out.push(0);
    out.extend(values);
    out[start] = (out.len() - start - 1) as u32;
}
fn encode(out: &mut Vec<u32>, f: &Fixture) {
    out.push(f.steps.len() as u32);
    out.extend(f.position.map(|v| v as u32));
    out.extend(f.camera.map(|v| v as u32));
    out.extend([
        f.runtime.siren_timer as u32,
        f.runtime.sound_channel,
        u32::from(f.runtime.highlighted),
        u32::from(f.sampling.dirty),
        u32::from(f.sampling.records_attached),
        u32::from(f.runtime.private_mesh),
        u32::from(f.vertex_flags),
        f.activate_status as u32,
        f.parent_status as u32,
        12345,
    ]);
    out.extend(f.flags);
    out.push(f.sampling.records.len() as u32);
    for record in &f.sampling.records {
        words_record(out, record);
    }
    for list in &f.lists {
        words_list(out, list.iter().copied());
    }
    out.extend(f.steps.iter().flatten());
}
fn snapshot(
    out: &mut Vec<u32>,
    state: &factory::State<ObstacleId>,
    scene: &Scene,
    status: i32,
    result: u32,
) {
    let surface = &scene.surface;
    out.extend([
        status as u32,
        result,
        state.current_slot,
        surface.runtime.siren_timer as u32,
        surface.runtime.sound_channel,
        u32::from(surface.runtime.highlighted),
        u32::from(surface.sampling.state.dirty),
        u32::from(surface.sampling.state.records_attached),
        u32::from(!surface.vertex_flags.is_empty()),
        u32::from(scene.meshes.get(surface.mesh).is_some()),
        scene.releases,
        12345,
    ]);
    out.extend(&state.ticks.flags[1..4]);
    for record in &surface.sampling.state.records {
        words_record(out, record);
    }
    out.push(state.ticks.next.map_or(0, |v| v.0));
    words_list(out, scene.collision.iter().map(|v| v.0 as u32 + 1));
    words_list(out, scene.drawing.iter().map(|v| v.0 as u32 + 1));
    words_list(out, state.ticks.order.iter().map(|v| v.0));
    words_list(out, state.queues.notifications.iter().map(|v| v.0));
    words_list(out, state.queues.destruction.iter().map(|v| v.0));
    out.push(scene.trace.len() as u32);
    out.extend(scene.trace.iter().flatten());
    let sample = &scene.sample;
    out.extend(sample.local_raw.map(|v| v as u32));
    out.extend(sample.local_clamped.map(|v| v as u32));
    out.extend([
        sample.vertex_height as u32,
        sample.edge_weight as u32,
        sample.offset as u32,
        sample.activated_records,
    ]);
    assert!(
        scene.meshes.get(scene.shared).is_some(),
        "a private destructor retired shared geometry"
    );
}
fn fixtures() -> Vec<Fixture> {
    let query = [8, 80, 0, (-80_i32) as u32, 20];
    let mut result = vec![Fixture {
        steps: vec![
            [2, 0, 0, 0, 20],
            query,
            [7, 50, 0, 0, 20],
            [7, 50, 0, 0, 20],
            [7, 50, 0, 0, 20],
            [6, 0, 0, 0, 20],
            query,
            [7, 16, 0, 0, 20],
            [3, 0, 0, 0, 20],
        ],
        ..Fixture::default()
    }];
    let mut detached = Fixture {
        steps: vec![query],
        ..Fixture::default()
    };
    detached.sampling.records_attached = false;
    result.push(detached);
    result.push(Fixture {
        steps: vec![
            [1, 16, 0, 0, 20],
            [1, 500, 0, 0, 20],
            [1, 0, 0, 0, 20],
            [1, i32::MAX as u32, 0, 0, 20],
            [1, (-1_i32) as u32, 0, 0, 20],
        ],
        ..Fixture::default()
    });
    for red in [127, 128] {
        let mut f = Fixture {
            steps: vec![[1, (-1_i32) as u32, 0, 0, 20], [1, 16, 0, 0, 20]],
            ..Fixture::default()
        };
        for record in &mut f.sampling.records {
            record.state = 2;
            record.color = [red, 3, 9];
        }
        // A retired buffer must be stopped before another tick; use one step
        // for red=128, whose signed byte is already negative on the first tick.
        if red == 128 {
            f.steps.pop();
        }
        result.push(f);
    }
    let mut siren = Fixture {
        steps: vec![
            query,
            [1, 5000, 0, 0, 20],
            [1, 16, 0, 0, 20],
            [6, 0, 0, 0, 20],
            query,
        ],
        ..Fixture::default()
    };
    siren.runtime.siren_timer = 22050;
    result.push(siren);
    for private in [false, true] {
        for status in [0, 1, -13] {
            let mut f = Fixture {
                parent_status: status,
                steps: vec![
                    [4, 1, 0, 0, 0],
                    [4, 2, 0, 0, 0],
                    [5, 0, 0, 0, 20],
                    [5, 0, 0, 0, 20],
                ],
                ..Fixture::default()
            };
            f.runtime.private_mesh = private;
            if !private {
                f.steps.push(query);
            }
            result.push(f);
        }
    }
    for status in [1, -13] {
        result.push(Fixture {
            activate_status: status,
            steps: vec![[2, 0, 0, 0, 20], [3, 0, 0, 0, 20], [6, 0, 0, 0, 20]],
            ..Fixture::default()
        });
    }
    for limit in 0..=5 {
        for timer in [0, 11025, 11026, 22050, i32::MIN, i32::MAX] {
            let mut f = Fixture {
                steps: vec![
                    [1, 128, 0, 0, limit],
                    query,
                    [3, 0, 0, 0, limit],
                    [6, 0, 0, 0, 20],
                ],
                ..Fixture::default()
            };
            f.runtime.siren_timer = timer;
            f.sampling.dirty = 1;
            f.lists[3] = vec![2, 3];
            for record in &mut f.sampling.records {
                record.state = 2;
                record.color = [127, 13, 42];
            }
            result.push(f);
        }
    }
    let mut random = 0xa6148e73_u32;
    let mut word = || {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        random
    };
    for _ in 0..128 {
        let mut f = Fixture::default();
        f.runtime.private_mesh = word() & 1 != 0;
        f.runtime.siren_timer = [0, 11025, 11026, 22050, i32::MAX][word() as usize % 5];
        f.camera = [word() as i32, 0, word() as i32];
        f.sampling.dirty = (word() % 2) as u8;
        f.sampling.records = (0..word() % 7)
            .map(|_| deformation::Record {
                state: (word() % 6) as u8,
                color: [word() as u8, word() as u8, word() as u8],
                timer: [-1, 0, 16, 500, i32::MAX][word() as usize % 5],
                ..Default::default()
            })
            .collect();
        f.lists[0] = (0..word() % 5).map(|_| 1 + word() % 3).collect();
        f.lists[1] = (0..word() % 5).map(|_| 1 + word() % 3).collect();
        f.lists[3] = (0..word() % 3).map(|v| 2 + v).collect();
        let dt = [-1, 0, 1, 16, 64, 127, 128, 500][word() as usize % 8] as u32;
        f.steps = vec![
            [1, dt, 0, 0, word() % 9],
            query,
            [3, 0, 0, 0, word() % 6],
            [6, 0, 0, 0, 20],
            [5, 0, 0, 0, 20],
        ];
        result.push(f);
    }
    result
}
#[test]
#[ignore = "requires C Crush oracle; set TANKRACER_CRUSH_REFERENCE"]
fn concrete_record_ticks_sampling_membership_cleanup_and_dispatch_match_c() {
    let classes = vec![Arc::new(Class {
        maximum_slot: 2,
        methods: vec![Some(crush::TICK), Some(MethodId(17)), Some(MethodId(18))],
    })];
    let (mut input, mut expected, mut ranges) = (Vec::new(), Vec::new(), Vec::new());
    let (mut errors, mut retired, mut callbacks) = (0, 0, 0);
    let fixtures = fixtures();
    for (case, f) in fixtures.iter().enumerate() {
        encode(&mut input, f);
        let (mut state, mut scene) = make(f);
        for (step, &op) in f.steps.iter().enumerate() {
            let result = operation(&mut state, &mut scene, &classes, op);
            let (status, value) = match result {
                Ok(v) => (0, v),
                Err(v) => {
                    errors += 1;
                    (v, UNCHANGED)
                }
            };
            let start = expected.len();
            snapshot(&mut expected, &state, &scene, status, value);
            ranges.push((
                start..expected.len(),
                format!("case {case} step {step} op {op:?}"),
            ));
            retired += usize::from(!scene.surface.sampling.state.records_attached);
        }
        callbacks += scene.trace.len();
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/crush");
    fs::create_dir_all(&folder).unwrap();
    let path = folder.join("input.bin");
    fs::write(
        &path,
        input
            .iter()
            .flat_map(|v| v.to_le_bytes())
            .collect::<Vec<_>>(),
    )
    .unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_CRUSH_REFERENCE").expect("set TANKRACER_CRUSH_REFERENCE"),
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
    assert_eq!(&output.stdout[..4], b"CR01");
    let words: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
        .collect();
    if let Some(index) = words.iter().zip(&expected).position(|(a, b)| a != b) {
        let (range, label) = ranges
            .iter()
            .find(|(range, _)| range.contains(&index))
            .unwrap();
        panic!(
            "{label}, offset {}: C {}, Rust {}",
            index - range.start,
            words[index],
            expected[index]
        );
    }
    assert_eq!(words.len(), expected.len());
    assert!(errors > 10 && retired > 10 && callbacks > 10);
    eprintln!(
        "{} Crush scenarios / {} updates / {} words exact; {errors} error prefixes, {retired} detached observations, {callbacks} callbacks",
        fixtures.len(),
        ranges.len(),
        words.len()
    );
}
