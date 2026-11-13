use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    math::Matrix,
    object_query::{
        self as query, Bounds, Code, Collider, EntityId, Grid, Hit, ObstacleId, SegmentHit, Tank,
        TankId,
    },
    pose::Pose,
};
struct Random(u32);
impl Random {
    fn word(&mut self) -> u32 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 17;
        self.0 ^= self.0 << 5;
        self.0
    }
    fn signed(&mut self) -> i32 {
        self.word() as i32
    }
    fn choose(&mut self, v: &[i32]) -> i32 {
        v[self.word() as usize % v.len()]
    }
}
fn append(out: &mut Vec<u32>, v: impl IntoIterator<Item = i32>) {
    out.extend(v.into_iter().map(|v| v as u32));
}
fn hit<Id>(out: &mut Vec<u32>, h: &Hit<Id>, index: impl Fn(&Id) -> usize) {
    out.push(h.entity.map_or(0, |id| id.0));
    out.push(h.object.as_ref().map_or(u32::MAX, |id| index(id) as u32));
    out.push(h.code as u32);
    append(out, h.point);
    append(out, h.normal);
}
fn status(result: Result<Code, query::Error>) -> i32 {
    match result {
        Ok(code) => code as i32,
        Err(query::Error::Arithmetic) => -2,
        Err(query::Error::Limit) => -3,
        Err(e) => panic!("unexpected fixture error {e:?}"),
    }
}
fn bool_status(result: Result<bool, query::Error>) -> i32 {
    match result {
        Ok(value) => i32::from(value),
        Err(query::Error::Arithmetic) => -2,
        Err(query::Error::Limit) => -3,
        Err(e) => panic!("unexpected fixture error {e:?}"),
    }
}
#[derive(Default)]
struct Oracle {
    input: Vec<u8>,
    expected: Vec<u32>,
    records: Vec<(Range<usize>, String)>,
    counts: [usize; 6],
    statuses: [[usize; 4]; 6],
}
impl Oracle {
    fn record(&mut self, op: u32, input: Vec<u32>, out: Vec<u32>, label: String) {
        self.input.extend(op.to_le_bytes());
        self.input
            .extend(input.into_iter().flat_map(u32::to_le_bytes));
        let start = self.expected.len();
        let status = out[0] as i32;
        self.statuses[op as usize][if status < 0 { 3 } else { status as usize }] += 1;
        self.expected.extend(out);
        self.records.push((start..self.expected.len(), label));
        self.counts[op as usize] += 1;
    }
    fn compare(self) {
        let folder =
            PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/object-query");
        fs::create_dir_all(&folder).unwrap();
        let path = folder.join("input.bin");
        fs::write(&path, self.input).unwrap();
        let result = Command::new(
            std::env::var_os("TANKRACER_OBJECT_QUERY_REFERENCE")
                .expect("set TANKRACER_OBJECT_QUERY_REFERENCE"),
        )
        .stdin(Stdio::from(fs::File::open(path).unwrap()))
        .output()
        .unwrap();
        assert!(
            result.status.success(),
            "status {:?}: {}",
            result.status.code(),
            String::from_utf8_lossy(&result.stderr)
        );
        assert_eq!(&result.stdout[..4], b"OQ01");
        let words: Vec<_> = result.stdout[4..]
            .chunks_exact(4)
            .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
            .collect();
        assert_eq!(words.len(), self.expected.len());
        for (range, label) in &self.records {
            assert_eq!(
                words[range.clone()],
                self.expected[range.clone()],
                "{label}"
            );
        }
        eprintln!(
            "{} ordered query records / {} words exact; counts {:?}; miss/cross/inside/error {:?}",
            self.records.len(),
            words.len(),
            self.counts,
            self.statuses
        );
    }
}
fn object_words(out: &mut Vec<u32>, tank: &Tank) {
    let c = tank.collider;
    out.push(c.entity.0);
    append(out, c.pose.position);
    append(out, c.pose.matrix.0);
    out.push(u32::from(c.external_basis.is_some()));
    append(out, c.external_basis.unwrap_or(Matrix::IDENTITY).0);
    out.push(u32::from(c.radius));
    append(out, c.bounds.min);
    append(out, c.bounds.max);
    out.extend([
        u32::from(tank.slot),
        u32::from(tank.active),
        u32::from(tank.disabled),
        u32::from(tank.ready),
    ]);
    append(out, tank.overlap_bounds.min);
    append(out, tank.overlap_bounds.max);
    for p in tank.contacts {
        append(out, p);
    }
}
struct World {
    tanks: Vec<Tank>,
    cells: [Vec<ObstacleId>; 64],
    deformables: Vec<ObstacleId>,
    slots: [Option<TankId>; 9],
    origin: [i32; 2],
    shifts: [u32; 2],
}
impl World {
    fn input(
        &self,
        self_index: usize,
        self_entity: EntityId,
        other: usize,
        a: [i32; 3],
        b: [i32; 3],
    ) -> Vec<u32> {
        let mut out = vec![self.tanks.len() as u32];
        for t in &self.tanks {
            object_words(&mut out, t);
        }
        append(&mut out, self.origin);
        out.extend(self.shifts);
        for list in self.cells.iter().chain([&self.deformables]) {
            out.push(list.len() as u32);
            out.extend(list.iter().map(|id| id.0 as u32));
        }
        out.extend(self.slots.map(|id| id.map_or(u32::MAX, |id| id.0 as u32)));
        out.extend([self_index as u32, self_entity.0, other as u32]);
        append(&mut out, a);
        append(&mut out, b);
        hit(&mut out, &initial_obstacle(), |id| id.0);
        out
    }
    fn grid(&self) -> Grid<'_> {
        Grid {
            origin: self.origin,
            shifts: self.shifts,
            cells: self.cells.each_ref().map(|v| v.as_slice()),
        }
    }
}
fn initial_obstacle() -> Hit<ObstacleId> {
    Hit {
        entity: Some(EntityId(63)),
        object: Some(ObstacleId(1)),
        code: 93,
        point: [17, 19, 23],
        normal: [31, 37, 41],
    }
}
fn initial_tank() -> Hit<TankId> {
    let h = initial_obstacle();
    Hit {
        entity: h.entity,
        object: h.object.map(|id| TankId(id.0)),
        code: h.code,
        point: h.point,
        normal: h.normal,
    }
}
#[test]
#[ignore = "requires C object-query oracle; set TANKRACER_OBJECT_QUERY_REFERENCE"]
fn ordered_queries_and_partial_outputs_match_native_integer_geometry() {
    let mut rng = Random(0x281e90cd);
    let mut oracle = Oracle::default();
    let extremes = [
        i32::MIN,
        i32::MAX,
        -524289,
        -524288,
        -262144,
        -4096,
        -2,
        -1,
        0,
        1,
        2,
        4096,
        262144,
        524288,
        524289,
    ];
    for case in 0..8192 {
        let boundary = case % 5 == 0;
        let a = std::array::from_fn(|_| {
            if boundary {
                rng.choose(&extremes)
            } else {
                rng.signed() % 10000
            }
        });
        let b = if case % 17 == 0 {
            a
        } else {
            std::array::from_fn(|_| {
                if boundary {
                    rng.choose(&extremes)
                } else {
                    rng.signed() % 10000
                }
            })
        };
        let bounds = if boundary {
            Bounds {
                min: std::array::from_fn(|_| rng.choose(&extremes)),
                max: std::array::from_fn(|_| rng.choose(&extremes)),
            }
        } else {
            Bounds {
                min: [-4000; 3],
                max: [4000; 3],
            }
        };
        let mut h = SegmentHit {
            point: [17, 19, 23],
            normal: [31, 37, 41],
        };
        let mut input = Vec::new();
        for v in [a, b, bounds.min, bounds.max, h.point, h.normal] {
            append(&mut input, v);
        }
        let code = status(query::segment_box(a, b, bounds, &mut h));
        let mut out = vec![code as u32];
        append(&mut out, h.point);
        append(&mut out, h.normal);
        oracle.record(
            1,
            input,
            out,
            format!("segment case={case} a={a:?} b={b:?} bounds={bounds:?}"),
        );
    }
    for case in 0..1024 {
        let boundary = case % 9 == 0;
        let tanks: Vec<_> = (0..9)
            .map(|slot| {
                let position = std::array::from_fn(|_| {
                    if boundary {
                        rng.choose(&extremes)
                    } else {
                        rng.signed() % 15000
                    }
                });
                let basis = if case % 4 == 0 {
                    Matrix::IDENTITY
                } else {
                    Matrix::from_angles(std::array::from_fn(|_| rng.word() & 65535))
                };
                let bounds = Bounds {
                    min: [-2000; 3],
                    max: [2000; 3],
                };
                let collider = Collider {
                    entity: EntityId(slot + 1),
                    pose: Pose {
                        position,
                        matrix: basis,
                    },
                    external_basis: if slot % 3 == 0 { None } else { Some(basis) },
                    radius: rng.choose(&[0, 64, 128, 1024, 65535]) as u16,
                    bounds,
                };
                let contacts = std::array::from_fn(|i| {
                    let v = basis.local_to_world([
                        if i & 1 == 0 { -3000 } else { 3000 },
                        -2000,
                        if i & 2 == 0 { -3000 } else { 3000 },
                    ]);
                    std::array::from_fn(|j| v[j].wrapping_add(position[j]))
                });
                Tank {
                    collider,
                    slot: slot as u8,
                    active: case % 11 != 0,
                    disabled: slot % 7 == 0,
                    ready: slot % 8 != 0,
                    overlap_bounds: bounds,
                    contacts,
                }
            })
            .collect();
        let cells = std::array::from_fn(|cell| {
            (0..9)
                .filter(|&i| (cell + i + case) % 4 != 0)
                .rev()
                .map(ObstacleId)
                .collect()
        });
        let mut world = World {
            tanks,
            cells,
            deformables: vec![ObstacleId(5), ObstacleId(2), ObstacleId(6), ObstacleId(1)],
            slots: std::array::from_fn(|i| (i % 5 != 0).then_some(TankId(i))),
            origin: [-10000, -10000],
            shifts: if boundary {
                [rng.word(), rng.word()]
            } else {
                [12, 12]
            },
        };
        let target = case % 9;
        let pos = world.tanks[target].collider.pose.position;
        let (a, b) = if boundary {
            (
                std::array::from_fn(|_| rng.choose(&extremes)),
                std::array::from_fn(|_| rng.choose(&extremes)),
            )
        } else if case % 3 == 0 {
            (pos, pos)
        } else {
            let start = world.tanks[target]
                .collider
                .pose
                .matrix
                .local_to_world([-4000, 0, 0]);
            let end = world.tanks[target]
                .collider
                .pose
                .matrix
                .local_to_world([4000, 0, 0]);
            (
                std::array::from_fn(|i| pos[i].wrapping_add(start[i])),
                std::array::from_fn(|i| pos[i].wrapping_add(end[i])),
            )
        };
        if case % 13 == 0 {
            world.tanks[1].collider.pose = world.tanks[2].collider.pose;
            world.tanks[1].contacts = world.tanks[2].contacts;
        }
        let objects: Vec<_> = world.tanks.iter().map(|t| t.collider).collect();
        let self_index = (target + 1) % 9;
        let self_entity = EntityId(if case % 2 == 0 { target as u32 + 1 } else { 63 });
        for op in 2..=5 {
            let input = world.input(self_index, self_entity, target, a, b);
            let mut out = Vec::new();
            match op {
                2 | 3 => {
                    let mut h = initial_obstacle();
                    let result = if op == 2 {
                        query::obstacles(&world.grid(), &objects, self_entity, a, b, &mut h)
                    } else {
                        query::deformables(&world.deformables, &objects, a, b, &mut h)
                    };
                    out.push(bool_status(result) as u32);
                    hit(&mut out, &h, |id| id.0);
                }
                4 => {
                    let mut h = initial_tank();
                    out.push(status(query::tanks(
                        &world.tanks[self_index],
                        &world.tanks,
                        &world.slots,
                        a,
                        b,
                        &mut h,
                    )) as u32);
                    hit(&mut out, &h, |id| id.0);
                }
                5 => out.push(u32::from(query::overlap(
                    &world.tanks[self_index],
                    &world.tanks[target],
                ))),
                _ => unreachable!(),
            }
            oracle.record(
                op,
                input,
                out,
                format!("object case={case} op={op} target={target} self={self_index}"),
            );
        }
    }
    for op in 1..=4 {
        assert!(oracle.statuses[op][0] > 0);
        assert!(oracle.statuses[op][1] + oracle.statuses[op][2] > 0);
    }
    oracle.compare();
}

struct Input<'a>(&'a [u8]);
impl Input<'_> {
    fn word(&mut self) -> u32 {
        let (word, tail) = self.0.split_at(4);
        self.0 = tail;
        u32::from_le_bytes(word.try_into().unwrap())
    }
    fn ints<const N: usize>(&mut self) -> [i32; N] {
        std::array::from_fn(|_| self.word() as i32)
    }
    fn object(&mut self) -> Tank {
        let entity = EntityId(self.word());
        let position = self.ints();
        let matrix = Matrix(self.ints());
        let external = self.word() != 0;
        let basis = Matrix(self.ints());
        let radius = self.word() as u16;
        let bounds = Bounds {
            min: self.ints(),
            max: self.ints(),
        };
        let collider = Collider {
            entity,
            pose: Pose { position, matrix },
            external_basis: external.then_some(basis),
            radius,
            bounds,
        };
        Tank {
            collider,
            slot: self.word() as u8,
            active: self.word() != 0,
            disabled: self.word() != 0,
            ready: self.word() != 0,
            overlap_bounds: Bounds {
                min: self.ints(),
                max: self.ints(),
            },
            contacts: std::array::from_fn(|_| self.ints()),
        }
    }
    fn list(&mut self) -> Vec<ObstacleId> {
        let count = self.word();
        (0..count)
            .map(|_| ObstacleId(self.word() as usize))
            .collect()
    }
    fn world(&mut self) -> World {
        let count = self.word();
        let tanks = (0..count).map(|_| self.object()).collect();
        let origin = self.ints();
        let shifts = [self.word(), self.word()];
        let cells = std::array::from_fn(|_| self.list());
        let deformables = self.list();
        let slots = std::array::from_fn(|_| {
            let id = self.word();
            (id != u32::MAX).then_some(TankId(id as usize))
        });
        World {
            tanks,
            cells,
            deformables,
            slots,
            origin,
            shifts,
        }
    }
}

#[test]
#[ignore = "requires full C world oracle; set TANKRACER_OBJECT_WORLD_REFERENCE"]
fn all_eight_initialized_worlds_match_real_object_lists_and_tank_queries() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let executable = std::env::var_os("TANKRACER_OBJECT_WORLD_REFERENCE")
        .expect("set TANKRACER_OBJECT_WORLD_REFERENCE");
    let (mut queries, mut colliders) = (0, 0);
    let mut statuses = [[0_usize; 4]; 3];
    for map in 0..8 {
        let output = Command::new(&executable)
            .arg(&root)
            .arg(map.to_string())
            .output()
            .unwrap();
        assert!(
            output.status.success(),
            "map={map} {:?} {}",
            output.status.code(),
            String::from_utf8_lossy(&output.stderr)
        );
        assert_eq!(&output.stdout[..4], b"OW01");
        let mut input = Input(&output.stdout[4..]);
        let world = input.world();
        let self_index = input.word() as usize;
        let self_entity = EntityId(input.word());
        let count = input.word();
        colliders += world.tanks.len();
        let objects: Vec<_> = world.tanks.iter().map(|t| t.collider).collect();
        let grid = world.grid();
        for case in 0..count {
            let a = input.ints();
            let b = input.ints();
            for op in 2..=4 {
                let mut out = Vec::new();
                if op == 4 {
                    let mut h = initial_tank();
                    h.object = Some(TankId(0));
                    out.push(status(query::tanks(
                        &world.tanks[self_index],
                        &world.tanks,
                        &world.slots,
                        a,
                        b,
                        &mut h,
                    )) as u32);
                    hit(&mut out, &h, |id| id.0);
                } else {
                    let mut h = initial_obstacle();
                    h.object = Some(ObstacleId(0));
                    let result = if op == 2 {
                        query::obstacles(&grid, &objects, self_entity, a, b, &mut h)
                    } else {
                        query::deformables(&world.deformables, &objects, a, b, &mut h)
                    };
                    out.push(bool_status(result) as u32);
                    hit(&mut out, &h, |id| id.0);
                }
                let expected: Vec<_> = (0..10).map(|_| input.word()).collect();
                assert_eq!(
                    out, expected,
                    "map={map} case={case} op={op} a={a:?} b={b:?}"
                );
                let code = out[0] as i32;
                statuses[(op - 2) as usize][if code < 0 { 3 } else { code as usize }] += 1;
                queries += 1;
            }
        }
        assert!(input.0.is_empty());
        eprintln!(
            "map {map}: {} real colliders, {} query triplets",
            world.tanks.len(),
            count
        );
    }
    eprintln!(
        "{queries} full-native object queries match across {colliders} real colliders; miss/cross/inside/error {statuses:?}"
    );
    for (kind, stats) in statuses.into_iter().enumerate() {
        assert!(
            stats[1] + stats[2] > 0,
            "no successful native queries for kind {kind}"
        );
    }
}
