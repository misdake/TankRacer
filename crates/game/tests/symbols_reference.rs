use std::{
    fs,
    path::PathBuf,
    process::{Command, Stdio},
};
use tankracer_game::{
    dispatch::ClassId,
    object_query::EntityId,
    symbols::{SymbolId, Symbols},
};

#[test]
#[ignore = "requires C symbols oracle; set TANKRACER_SYMBOLS_REFERENCE"]
fn original_casefold_nil_forward_binding_and_long_name_identity_match_c() {
    let mut names = vec![
        "NIL".to_owned(),
        String::new(),
        "A".into(),
        "a".into(),
        "Car".into(),
        "cAR".into(),
        "é".into(),
        "Car\0ignoredé".into(),
    ];
    for length in [1, 2, 31, 32, 33, 63, 127, 1023] {
        let name = "Q".repeat(length);
        names.extend([name.clone(), name.to_ascii_lowercase(), name]);
    }
    for index in 0..64 {
        names.extend([
            format!("object{index}"),
            format!("OBJECT{index}"),
            format!("object{}", index / 2),
        ]);
    }
    let (mut input, mut expected, mut records) = (Vec::new(), Vec::new(), Vec::new());
    let mut symbols = Symbols::default();
    let mut known: Vec<SymbolId> = vec![symbols.intern("nil").unwrap()];
    for (case, name) in names.iter().enumerate() {
        let entity = if case % 3 == 0 { 1 + case as u32 } else { 0 };
        let class = if case % 4 == 0 { 7 + case as u32 } else { 0 };
        input.push(name.len() as u32);
        input.extend(name.bytes().map(u32::from));
        input.extend([entity, class]);
        let result = symbols.intern(name);
        let (status, id) = match result {
            Ok(id) => {
                if !known.contains(&id) {
                    known.push(id);
                }
                if entity != 0 {
                    symbols.bind_entity(id, EntityId(entity)).unwrap();
                }
                if class != 0 {
                    symbols.bind_class(id, ClassId(class as usize)).unwrap();
                }
                (0, known.iter().position(|&v| v == id).unwrap() as u32)
            }
            Err(_) => (-5_i32 as u32, u32::MAX),
        };
        let start = expected.len();
        expected.extend([status, id, known.len() as u32]);
        for &id in &known {
            expected.extend([
                symbols.entity(id).map_or(0, |v| v.0),
                symbols.class(id).map_or(0, |v| v.0 as u32),
            ]);
            let name = symbols.name(id).unwrap();
            expected.push(name.len() as u32);
            expected.extend(name.bytes().map(u32::from));
        }
        records.push((start..expected.len(), name.clone()));
    }
    let folder = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/tests/symbols");
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
        std::env::var_os("TANKRACER_SYMBOLS_REFERENCE").expect("set TANKRACER_SYMBOLS_REFERENCE"),
    )
    .stdin(Stdio::from(fs::File::open(path).unwrap()))
    .output()
    .unwrap();
    assert!(
        output.status.success(),
        "{:?}: {}",
        output.status.code(),
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"SY01");
    let words: Vec<_> = output.stdout[4..]
        .chunks_exact(4)
        .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
        .collect();
    assert_eq!(words.len(), expected.len());
    for (range, label) in records {
        assert_eq!(words[range.clone()], expected[range], "name {label:?}");
    }
    eprintln!(
        "{} symbol operations / {} words exact; {} stable symbols",
        names.len(),
        words.len(),
        known.len()
    );
}
