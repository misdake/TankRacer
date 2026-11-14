use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
    sync::Arc,
};
use tankracer_assets::{
    archive::Archive, level::Level, mesh::Geometry, model::Model, object_catalog::Catalog,
    terrain::TerrainData,
};
use tankracer_game::scene_entity::Handle;
use tankracer_game::{
    entity_factory::{self as factory, Methods},
    math::Matrix,
    object_query::{EntityId, ObstacleId},
    obstacle_collision::{self as obstacle, Grid, Invocation},
    obstacle_startup::{Animation, Debris, MaterialId, Materials, Piece, Sprite},
    scene_loading::{self, Registry},
    scene_objects::{
        self, Scene,
        collision::{Collision, Services, Sound},
    },
    shatter::BlastTarget,
    vehicle_model::{ModelBank, ShapeSource, Space},
};
struct Resources;
impl ShapeSource for Resources {
    fn shape(&self, _: Space, name: &str) -> Option<&[u8]> {
        panic!("unexpected shape {name}")
    }
}
impl Materials for Resources {
    fn resolve(&mut self, name: &str, _: u32) -> Result<Option<MaterialId>, i32> {
        panic!("unexpected material {name}")
    }
}
#[derive(Default)]
struct Devices {
    blast: bool,
    status: i32,
    actions: u32,
    calls: u32,
}
impl Services for Devices {
    fn blast_target(
        &mut self,
        slot: usize,
        scene: &mut Collision<'_>,
        call: Invocation<'_>,
    ) -> Result<Option<BlastTarget>, i32> {
        let _ = scene;
        Ok(
            (self.blast && (slot == 0 || slot == 3)).then(|| BlastTarget {
                entity: EntityId(2),
                position: call.world.colliders[1].pose.position,
                protection_timer: 0,
            }),
        )
    }
    fn blast_impulse(
        &mut self,
        target: EntityId,
        _: &mut Collision<'_>,
        call: Invocation<'_>,
    ) -> Result<(), i32> {
        assert_eq!(target, EntityId(2));
        let mut methods = vec![None; 28];
        methods[27] = Some(tankracer_game::dispatch::MethodId(72));
        let class = tankracer_game::dispatch::Class {
            maximum_slot: 27,
            methods,
        };
        tankracer_game::dispatch::call_entity(
            &class,
            27,
            call.current_slot,
            &mut 0,
            |_, slot, result| {
                self.calls += 1;
                tankracer_game::random::next(call.random);
                call.vehicle.velocity[0] = call.vehicle.velocity[0].wrapping_neg();
                if self.actions & 1 != 0 {
                    call.world.colliders[1].pose.position[0] =
                        call.world.colliders[0].pose.position[0].wrapping_add(20480);
                }
                *slot = slot.wrapping_add(7);
                *result = 1234;
                if self.status == 0 {
                    Ok(())
                } else {
                    Err(self.status)
                }
            },
        )
        .map_err(|error| match error {
            tankracer_game::dispatch::Error::Method(code) => code,
            _ => -1,
        })
    }
    fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        panic!("no sound resource")
    }
    fn feedback(&mut self, _: i32, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        panic!("feedback disabled")
    }
}
#[derive(Clone)]
struct Fixture {
    debris: Debris,
    position: [i32; 3],
    velocity: [i32; 3],
    seed: u32,
    slot: u32,
    flags: u32,
    ordinary: Vec<u32>,
    special: Vec<u32>,
    steps: Vec<[u32; 4]>,
    blast: bool,
    blast_status: i32,
    blast_actions: u32,
}
fn model(radius: u16) -> Arc<Model> {
    Arc::new(Model {
        geometry: Geometry {
            vertices: vec![],
            triangles: vec![],
        },
        materials: vec![],
        radius,
        render_hint: 0,
        bounds: [[0; 3]; 2],
        vertex_normals: None,
        face_centers: [None, None, None],
    })
}
fn piece() -> Piece {
    Piece {
        source: Some(model(15)),
        position: [200, 800, -300],
        velocity: [2000, -5000, 3000],
        matrix: Matrix::IDENTITY,
        translation: [71, 72, 73],
        angular: [13, -29, 65],
        previous_tilt: -1,
        velocity_gain: 128,
        spin_gain: 17,
        flags: 0,
        sprite_angle: 100,
    }
}
fn animation() -> Animation {
    Animation {
        material: None,
        position: [1, 2, 3],
        blend: 1,
        sheet_size: 64,
        cell_size: 32,
        initial_size: 150,
        cycles: 2,
        growth: 3,
        timer: 59,
        rotation: 33,
        cycle: 0,
        size: [100, 100],
        uv: [0, 0, 31, 31],
        color: [255, 127, 13],
    }
}
impl Default for Fixture {
    fn default() -> Self {
        Self {
            blast: false,
            blast_status: 0,
            blast_actions: 0,
            debris: Debris {
                pieces: vec![piece(), piece(), piece()],
                retired: 0,
                flags: 0,
                sprite: None,
                animation: None,
                offset: [11, 51, -23],
                ground_height: 0,
                gravity: 4096,
                friction: 2048,
                direction: 8192,
            },
            position: [100, 200, -300],
            velocity: [250000, -150000, 500000],
            seed: 0x1823afec,
            slot: 777,
            flags: 12,
            ordinary: vec![1, 2],
            special: vec![],
            steps: vec![
                [1, 40, 1, 100],
                [2, 16, 1, 100],
                [2, 33, 2, 100],
                [2, 1000, 0, 100],
                [1, 16, 1, 100],
            ],
        }
    }
}
fn fixtures() -> Vec<Fixture> {
    let mut result = vec![];
    for flags in [
        0,
        2,
        4,
        16,
        64,
        128,
        256,
        2 | 2048,
        2 | 128,
        64 | 128,
        4 | 2 | 2048,
        1 | 1024,
    ] {
        let mut f = Fixture::default();
        f.debris.flags = flags;
        if flags & 128 != 0 {
            f.debris.sprite = Some(Sprite {
                material: None,
                position: [0; 3],
                flags: 0,
                timer: 0,
                size: [73, 73],
                uv: [0, 0, 31, 31],
                color: [127; 3],
            });
        }
        result.push(f.clone());
        f.debris.animation = Some(animation());
        result.push(f);
    }
    for flags in [0, 2, 4, 16, 2 | 2048] {
        for limit in [0, 2, 100] {
            let mut f = Fixture::default();
            f.debris.flags = flags;
            f.steps = vec![
                [4, 40, 1, limit],
                [5, 16, 1, 100],
                [2, 33, 0, 100],
                [4, 40, 1, 100],
            ];
            result.push(f);
        }
    }
    for status in [-13, 0, 1] {
        for actions in [0, 1] {
            let mut f = Fixture::default();
            f.debris.flags = 4;
            f.blast = true;
            f.blast_status = status;
            f.blast_actions = actions;
            result.push(f);
        }
    }
    for flags in [0, 1, 2, 4, 8, 9, 10, 12] {
        for velocity in [
            [0; 3],
            [1024, 0, 0],
            [1025, 0, 0],
            [20, -2049, 10],
            [20, -2050, 10],
            [i32::MIN, 0, 0],
            [i32::MAX, i32::MIN, -1],
        ] {
            let mut f = Fixture::default();
            f.debris.flags = 1;
            for p in &mut f.debris.pieces {
                p.flags = flags;
                p.velocity = velocity;
                p.position[1] = -200;
            }
            f.steps = vec![
                [3, 16, 1, 100],
                [3, 33, 1, 100],
                [2, 1000, 2, 100],
                [3, 0, 0, 100],
            ];
            result.push(f);
        }
    }
    for limit in [0, 1, 2, 3, 4, 5, 100] {
        let f = Fixture {
            steps: vec![
                [1, 40, 0, limit],
                [2, 40, 1, limit],
                [3, 16, 1, limit],
                [1, 40, 1, 100],
            ],
            ordinary: vec![2, 2, 1, 1],
            special: vec![2, 2, 2],
            ..Fixture::default()
        };
        result.push(f);
    }
    for dt in [0, 1, 60, 61, 65535, 65536, i32::MIN as u32, i32::MAX as u32] {
        for growth in [i16::MIN, -3, 0, 3, i16::MAX] {
            let mut f = Fixture::default();
            f.debris.pieces.clear();
            f.debris.flags = 9;
            let mut a = animation();
            a.growth = growth;
            f.debris.animation = Some(a);
            f.steps = vec![
                [3, dt, 1, 100],
                [2, dt, 1, 100],
                [3, 61, 1, 100],
                [1, 40, 1, 100],
            ];
            result.push(f);
        }
    }
    for tilt in [-1, 0, 1000, i32::MAX] {
        let mut f = Fixture::default();
        f.debris.flags = 1;
        for p in &mut f.debris.pieces {
            p.flags = 4;
            p.previous_tilt = tilt;
            p.angular = [0; 3];
            p.matrix.0[6] = i32::MIN;
        }
        f.steps = vec![[3, 16, 1, 100], [2, 0, 0, 100]];
        result.push(f);
    }
    for count in [0, 1, 3, 6] {
        let mut f = Fixture::default();
        f.debris.pieces = (0..count).map(|_| piece()).collect();
        for p in &mut f.debris.pieces {
            p.flags = 0;
            p.velocity = [0; 3];
            p.source = None;
        }
        f.debris.flags = 1;
        f.steps = vec![[2, 16, 0, 100], [2, 16, 1, 100], [3, 16, 0, 1]];
        result.push(f);
    }
    result
}
fn ints(out: &mut Vec<u32>, values: impl IntoIterator<Item = i32>) {
    out.extend(values.into_iter().map(|v| v as u32));
}
fn animation_words(a: &Animation) -> Vec<u32> {
    let mut out = vec![];
    ints(&mut out, a.position);
    ints(&mut out, [a.blend, a.size[0], a.size[1]]);
    out.extend([
        u32::from(a.rotation),
        u32::from(a.sheet_size),
        u32::from(a.cell_size),
        a.initial_size as u32,
        u32::from(a.cycles),
        a.growth as u32,
        a.timer as u32,
        u32::from(a.cycle),
    ]);
    out.extend(a.uv.map(u32::from));
    out.extend(a.color.map(u32::from));
    out
}
fn piece_words(p: &Piece) -> Vec<u32> {
    let mut out = vec![];
    ints(&mut out, p.position);
    ints(&mut out, p.velocity);
    ints(&mut out, p.matrix.0);
    ints(&mut out, p.translation);
    ints(&mut out, p.angular);
    ints(
        &mut out,
        [
            p.previous_tilt,
            i32::from(p.velocity_gain),
            i32::from(p.spin_gain),
        ],
    );
    out.extend([p.flags, p.sprite_angle as u32]);
    out
}
fn list(out: &mut Vec<u32>, values: impl IntoIterator<Item = u32>) {
    let start = out.len();
    out.push(0);
    out.extend(values);
    out[start] = (out.len() - start - 1) as u32;
}
fn encode(f: &Fixture, input: &mut Vec<u8>) {
    let d = &f.debris;
    let mut w = vec![f.steps.len() as u32, f.seed, d.flags, u32::from(d.retired)];
    ints(&mut w, d.offset);
    ints(
        &mut w,
        [d.ground_height, d.gravity, d.friction, d.direction],
    );
    ints(&mut w, f.position);
    ints(&mut w, f.velocity);
    w.extend([f.flags, f.slot]);
    list(&mut w, f.ordinary.iter().copied());
    list(&mut w, f.special.iter().copied());
    w.extend([u32::from(f.blast), f.blast_status as u32, f.blast_actions]);
    w.push(d.pieces.len() as u32);
    for p in &d.pieces {
        w.extend(piece_words(p));
        w.push(p.source.as_ref().map_or(u32::MAX, |s| u32::from(s.radius)));
    }
    w.push(u32::from(d.animation.is_some()));
    if let Some(a) = &d.animation {
        w.extend(animation_words(a));
    }
    w.push(u32::from(d.sprite.is_some()));
    if let Some(a) = &d.sprite {
        ints(&mut w, a.size);
    }
    w.extend(f.steps.iter().flatten().copied());
    input.extend(w.into_iter().flat_map(u32::to_le_bytes));
}
fn snapshot(
    out: &mut Vec<u32>,
    scene: &Scene,
    state: &factory::State<Handle>,
    random: u32,
    outcome: (i32, u32),
    devices: &Devices,
    vehicle: &obstacle::State,
) {
    let (status, result) = outcome;
    let d = scene.components.objects[0]
        .as_ref()
        .unwrap()
        .data
        .debris
        .as_ref()
        .unwrap();
    out.extend([
        status as u32,
        result,
        random,
        state.current_slot,
        devices.calls,
        scene.world.colliders[1].pose.position[0] as u32,
        vehicle.velocity[0] as u32,
        d.flags,
        u32::from(d.retired),
        state.ticks.flags[1],
        state.ticks.next.map_or(0, |id| id.0),
    ]);
    let fall = scene.components.falls[0].as_ref().unwrap();
    let properties = scene.world.objects[0];
    ints(
        out,
        [
            properties.activity,
            properties.flags as i32,
            fall.angular_velocity,
            fall.rotation,
            fall.turn_rate,
            fall.last_vehicle.map_or(0, |id| id.0) as i32,
        ],
    );
    ints(out, scene.world.colliders[0].pose.matrix.0);
    for p in &d.pieces {
        out.extend(piece_words(p));
    }
    if let Some(a) = &d.animation {
        out.extend(animation_words(a));
    }
    for l in [&scene.components.ordinary, &scene.components.special] {
        list(out, l.iter().map(|id| scene.world.colliders[id.0].entity.0));
    }
    list(out, state.ticks.order.iter().map(|id| id.0));
    list(out, state.queues.notifications.iter().map(|id| id.0));
    list(out, state.queues.destruction.iter().map(|id| id.0));
    for cell in &scene.world.grid.cells {
        list(
            out,
            cell.iter().map(|id| scene.world.colliders[id.0].entity.0),
        );
    }
}
fn terrain() -> Arc<TerrainData> {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../assets/game/LEVELS/ENGLAND/FASTFILE.DAT");
    let bytes = fs::read(path).unwrap();
    let archive = Archive::parse(&bytes).unwrap();
    Arc::new(TerrainData::decode_bsm(archive.get("ENGLAND.BSM").unwrap()).unwrap())
}
fn build(
    f: &Fixture,
    terrain: &Arc<TerrainData>,
    registry: &Registry,
) -> (Scene, factory::State<Handle>) {
    let level = Level::decode(
        b"FSTPC 5 Fall First 0 0 0 0 0 0 nil () Obstacle Peer 0 0 0 0 0 0 nil () Paths ()",
    )
    .unwrap();
    let mut scene = registry.scene(Grid {
        origin: [-5000; 2],
        shifts: [12; 2],
        cells: std::array::from_fn(|_| vec![]),
    });
    let mut state = factory::State::new(4).unwrap();
    let mut models = ModelBank::default();
    let mut random = 0;
    let mut materials = Resources;
    let catalog = Catalog::default();
    {
        let mut loader = scene_loading::Loader::new(
            &mut scene,
            scene_loading::Resources {
                source: &Resources,
                models: &mut models,
                terrain,
                random: &mut random,
                catalog: &catalog,
                materials: &mut materials,
                matrix_tail: Some([0; 3]),
            },
            registry,
        );
        for record in level.objects {
            loader.load(&mut state, record).unwrap();
        }
    }
    state
        .context()
        .drain(
            scene_loading::STOP,
            registry.environment(100000),
            &mut scene,
        )
        .unwrap();
    scene.components.objects[0].as_mut().unwrap().data.debris = Some(f.debris.clone());
    scene.world.colliders[0].pose.position = f.position;
    state.ticks.flags[1] = f.flags;
    state.ticks.order = if f.flags & 4 != 0 {
        vec![EntityId(1)]
    } else {
        vec![]
    };
    state.current_slot = f.slot;
    scene.components.ordinary = f
        .ordinary
        .iter()
        .map(|id| ObstacleId(*id as usize - 1))
        .collect();
    scene.components.special = f
        .special
        .iter()
        .map(|id| ObstacleId(*id as usize - 1))
        .collect();
    (scene, state)
}
fn vehicle(f: &Fixture, registry: &Registry) -> obstacle::State {
    obstacle::State {
        entity: EntityId(2),
        class: registry.obstacle,
        base_position: [0; 3],
        reference_basis: tankracer_game::math::Matrix::IDENTITY,
        external_basis: None,
        position: [0; 3],
        previous_position: [0; 3],
        velocity: f.velocity,
        forward_velocity: 0,
        player: false,
        contacts: [[0; 3]; 4],
        impact_accumulator: 0,
        collision_size: 0,
        material_tag: 0,
        deformable: None,
    }
}
fn run(
    f: &Fixture,
    terrain: &Arc<TerrainData>,
    registry: &Registry,
    out: &mut Vec<u32>,
    ranges: &mut Vec<(std::ops::Range<usize>, String)>,
    case: usize,
) {
    let (mut scene, mut state) = build(f, terrain, registry);
    let mut seed = f.seed;
    let mut devices = Devices {
        blast: f.blast,
        status: f.blast_status,
        actions: f.blast_actions,
        calls: 0,
    };
    let mut vehicle = vehicle(f, registry);
    for (step, [op, dt, group, limit]) in f.steps.iter().copied().enumerate() {
        scene.components.dt = dt as i32;
        scene.components.frame_group = group;
        let mut result = 0x7182;
        let status = if op == 1 || op == 4 {
            let env = registry.hit_environment(scene.components.fall_tuning, false, limit);
            let (lifecycle, current_slot) = state.split();
            Collision {
                components: &mut scene.components,
                lifecycle,
                environment: registry.environment(limit),
            }
            .method(
                if op == 1 {
                    scene_objects::DEBRIS_HIT
                } else {
                    tankracer_game::breakable::BREAKABLE_HIT
                },
                Invocation {
                    dt: dt as i32,
                    target: ObstacleId(0),
                    vehicle: &mut vehicle,
                    world: &mut scene.world,
                    random: &mut seed,
                    current_slot,
                    result: &mut result,
                },
                env,
                &mut devices,
            )
        } else if op == 5 {
            state
                .context()
                .tick(
                    scene_loading::TICK,
                    registry.environment(limit),
                    &mut scene.updates(&mut seed),
                )
                .map_err(|error| match error {
                    factory::Error::Dispatch(tankracer_game::dispatch::Error::Method(code)) => code,
                    factory::Error::Lifecycle(tankracer_game::lifecycle::Error::Spatial(
                        tankracer_game::spatial::Error::Limit,
                    )) => -3,
                    _ => -1,
                })
        } else {
            scene.updates(&mut seed).invoke(
                if op == 2 {
                    scene_objects::FALL_TICK
                } else {
                    scene_objects::DEBRIS_TICK
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
        let start = out.len();
        snapshot(
            out,
            &scene,
            &state,
            seed,
            (status, result),
            &devices,
            &vehicle,
        );
        ranges.push((
            start..out.len(),
            format!(
                "case={case} step={step} op={op} dt={dt} limit={limit} flags={}",
                f.debris.flags
            ),
        ));
    }
}
#[test]
#[ignore = "requires native debris oracle; set TANKRACER_SHATTER_REFERENCE"]
fn real_activation_motion_animation_and_lifecycle_match_c() {
    let fixtures = fixtures();
    let terrain = terrain();
    let registry = Registry::default();
    let (mut input, mut expected, mut ranges) = (vec![], vec![], vec![]);
    for (i, f) in fixtures.iter().enumerate() {
        encode(f, &mut input);
        run(f, &terrain, &registry, &mut expected, &mut ranges, i);
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/shatter");
    fs::create_dir_all(&folder).unwrap();
    let input_path = folder.join("input.bin");
    fs::write(&input_path, input).unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_SHATTER_REFERENCE").expect("set TANKRACER_SHATTER_REFERENCE"),
    )
    .stdin(Stdio::from(fs::File::open(input_path).unwrap()))
    .output()
    .unwrap();
    assert!(
        output.status.success(),
        "{:?} {}",
        output.status.code(),
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"SH01");
    let actual: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
        .collect();
    if let Some(i) = actual.iter().zip(&expected).position(|(a, b)| a != b) {
        let (_, label) = ranges.iter().find(|(r, _)| r.contains(&i)).unwrap();
        panic!(
            "first difference {i} {label}; C={:?} Rust={:?}",
            &actual[i.saturating_sub(6)..(i + 10).min(actual.len())],
            &expected[i.saturating_sub(6)..(i + 10).min(expected.len())]
        );
    }
    assert_eq!(actual.len(), expected.len());
    for (range, label) in &ranges {
        assert_eq!(actual[range.clone()], expected[range.clone()], "{label}");
    }
    let errors = ranges
        .iter()
        .filter(|(r, _)| expected[r.start] != 0)
        .count();
    let retired = ranges
        .iter()
        .filter(|(r, _)| expected[r.start + 8] != 0)
        .count();
    let blast = ranges
        .iter()
        .filter(|(r, _)| expected[r.start + 4] != 0)
        .count();
    assert!(errors > 0 && retired > 0 && blast > 0);
    eprintln!(
        "{} debris scenarios / {} operations / {} semantic words exact through shared scene/factory; errors={errors}, retired observations={retired}, blast callback observations={blast}",
        fixtures.len(),
        ranges.len(),
        expected.len()
    );
}
#[test]
fn active_fragments_use_the_factory_stream_and_do_not_silently_skip_missing_blast_bindings() {
    let registry = Registry::default();
    let terrain = terrain();
    let f = Fixture::default();
    let (mut scene, mut state) = build(&f, &terrain, &registry);
    let mut seed = f.seed;
    scene.components.dt = 16;
    scene.components.objects[0]
        .as_mut()
        .unwrap()
        .data
        .debris
        .as_mut()
        .unwrap()
        .flags = 1;
    scene
        .updates(&mut seed)
        .invoke(
            scene_objects::DEBRIS_TICK,
            factory::Invocation {
                context: state.context(),
                entity: EntityId(1),
                payload: None,
                result: &mut 0,
            },
            registry.environment(100),
        )
        .unwrap();
    assert_ne!(
        scene.components.objects[0]
            .as_ref()
            .unwrap()
            .data
            .debris
            .as_ref()
            .unwrap()
            .pieces[0]
            .position,
        f.debris.pieces[0].position
    );
    struct MissingBindings;
    impl Services for MissingBindings {
        fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
            panic!("unexpected sound");
        }
        fn feedback(
            &mut self,
            _: i32,
            _: &mut Collision<'_>,
            _: Invocation<'_>,
        ) -> Result<(), i32> {
            panic!("unexpected feedback");
        }
    }
    let mut missing = Fixture::default();
    missing.debris.flags = 4;
    let (mut missing_scene, mut missing_state) = build(&missing, &terrain, &registry);
    let mut missing_seed = missing.seed;
    let mut result = 0x7182;
    let mut peer = vehicle(&missing, &registry);
    let env = registry.hit_environment(missing_scene.components.fall_tuning, false, 100);
    let (lifecycle, current_slot) = missing_state.split();
    assert_eq!(
        Collision {
            components: &mut missing_scene.components,
            lifecycle,
            environment: registry.environment(100)
        }
        .method(
            scene_objects::DEBRIS_HIT,
            Invocation {
                dt: 40,
                target: ObstacleId(0),
                vehicle: &mut peer,
                world: &mut missing_scene.world,
                random: &mut missing_seed,
                current_slot,
                result: &mut result
            },
            env,
            &mut MissingBindings
        ),
        Err(-5)
    );
    assert_eq!(
        (result, missing_seed, missing_state.current_slot),
        (0x7182, missing.seed, missing.slot)
    );
    let debris = missing_scene.components.objects[0]
        .as_ref()
        .unwrap()
        .data
        .debris
        .as_ref()
        .unwrap();
    assert_eq!(debris.flags, 0x405);
    assert_eq!(debris.pieces[0].velocity, missing.debris.pieces[0].velocity);
    // Lifecycle-only invocation does not manufacture a private RNG for a tick.
    assert_eq!(
        scene.invoke(
            scene_objects::DEBRIS_TICK,
            factory::Invocation {
                context: state.context(),
                entity: EntityId(1),
                payload: None,
                result: &mut 0
            },
            registry.environment(100)
        ),
        Err(-1)
    );
}
