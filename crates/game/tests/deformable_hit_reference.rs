use std::{
    fs,
    ops::Range,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    deformable_hit::{Config, Mesh, State, Vehicle},
    geometry::Bounds,
    math::Matrix,
    object_query::{Collider, EntityId},
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
#[test]
#[ignore = "requires C deformable-hit oracle; set TANKRACER_DEFORMABLE_HIT_REFERENCE"]
fn compression_drift_material_timers_shared_random_and_failed_prefixes_match_c() {
    let mut rng = Random(0x61a28091);
    let (mut input, mut expected, mut records) =
        (Vec::new(), Vec::new(), Vec::<(Range<usize>, String)>::new());
    let (mut errors, mut triggers) = (0, 0);
    let extremes = [i32::MIN, i32::MAX, -32769, -32768, -1, 0, 1, 32767, 32768];
    for case in 0..1024 {
        let config = Config {
            floor: rng.choose(&[-32769, -100, 0, 50, 32767, 32768]),
            compression: rng.choose(&[i32::MIN, i32::MAX, -64, 0, 1, 32, 100]),
            drift_shift: rng.word(),
            material: rng.word() as i16,
            has_sound: case % 3 != 0,
        };
        let position = if case % 4 == 0 {
            std::array::from_fn(|_| rng.choose(&extremes))
        } else {
            [0; 3]
        };
        let matrix = Matrix::from_angles(std::array::from_fn(|_| rng.word() & 65535));
        let collider = Collider {
            entity: EntityId(1),
            pose: Pose { position, matrix },
            external_basis: (case % 2 == 0).then_some(matrix),
            radius: 1,
            bounds: Bounds {
                min: [-1; 3],
                max: [1; 3],
            },
        };
        let base = if case % 4 == 0 {
            std::array::from_fn(|_| rng.choose(&extremes))
        } else {
            [0; 3]
        };
        let dt = rng.choose(&[i32::MIN, i32::MAX, -1, 0, 1, 16, 40, 100]);
        let mut state = State {
            dirty: rng.word() as u8,
            last_vehicle: Some(EntityId(3)),
            sound_timer: rng.choose(&[-1, 0, 1, 16, 40, 400, 900]),
        };
        let mut vertices: Vec<_> = (0..16)
            .map(|i| {
                if i < 8 {
                    [
                        rng.choose(&[-256, -255, -1, 0, 1, 255, 256]) as i16,
                        rng.choose(&[0, 50, 100, 1000, 32767, -32768]) as i16,
                        rng.choose(&[-256, -255, -1, 0, 1, 255, 256]) as i16,
                    ]
                } else {
                    std::array::from_fn(|_| rng.choose(&extremes) as i16)
                }
            })
            .collect();
        let flags: Vec<_> = (0..if case % 11 == 0 { 3 } else { 16 })
            .map(|i| (i % 8) as u8)
            .collect();
        let (mut material, mut seed) = (rng.signed(), rng.word());
        for tick in 0..4 {
            let mut words = vec![
                1,
                u32::from(state.dirty),
                state.last_vehicle.map_or(0, |id| id.0),
                state.sound_timer as u32,
                config.floor as u32,
                config.compression as u32,
                config.drift_shift,
                config.material as u32,
                u32::from(config.has_sound),
            ];
            append(&mut words, position);
            words.push(u32::from(collider.external_basis.is_some()));
            append(&mut words, matrix.0);
            append(&mut words, base);
            words.extend([
                dt as u32,
                material as u32,
                seed,
                vertices.len() as u32,
                flags.len() as u32,
            ]);
            for v in &vertices {
                append(&mut words, v.map(i32::from));
            }
            words.extend(flags.iter().copied().map(u32::from));
            input.extend(words.into_iter().flat_map(u32::to_le_bytes));
            let status = match state.hit(
                config,
                &collider,
                Mesh {
                    vertices: &mut vertices,
                    flags: &flags,
                },
                Vehicle {
                    entity: EntityId(2),
                    position: base,
                    material: &mut material,
                    random: &mut seed,
                    dt,
                },
            ) {
                Ok(triggered) => {
                    triggers += usize::from(triggered);
                    0
                }
                Err(_) => {
                    errors += 1;
                    -1
                }
            };
            let start = expected.len();
            expected.extend([
                status as u32,
                if status == 0 { 1 } else { 0x7182 },
                u32::from(state.dirty),
                state.last_vehicle.map_or(0, |id| id.0),
                state.sound_timer as u32,
                material as u32,
                seed,
            ]);
            for v in &vertices {
                append(&mut expected, v.map(i32::from));
            }
            records.push((
                start..expected.len(),
                format!("case={case} tick={tick} config={config:?} dt={dt}"),
            ));
        }
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/deformable-hit");
    fs::create_dir_all(&folder).unwrap();
    let path = folder.join("input.bin");
    fs::write(&path, input).unwrap();
    let result = Command::new(
        std::env::var_os("TANKRACER_DEFORMABLE_HIT_REFERENCE")
            .expect("set TANKRACER_DEFORMABLE_HIT_REFERENCE"),
    )
    .stdin(Stdio::from(fs::File::open(path).unwrap()))
    .output()
    .unwrap();
    assert!(
        result.status.success(),
        "{:?} {}",
        result.status.code(),
        String::from_utf8_lossy(&result.stderr)
    );
    assert_eq!(&result.stdout[..4], b"DH01");
    let words: Vec<_> = result.stdout[4..]
        .chunks_exact(4)
        .map(|b| u32::from_le_bytes(b.try_into().unwrap()))
        .collect();
    assert_eq!(words.len(), expected.len());
    for (range, label) in &records {
        assert_eq!(words[range.clone()], expected[range.clone()], "{label}");
    }
    assert!(errors > 0 && triggers > 0);
    eprintln!(
        "{} deformable-hit records / {} words exact; errors={errors}, sound triggers={triggers}",
        records.len(),
        words.len()
    );
}
