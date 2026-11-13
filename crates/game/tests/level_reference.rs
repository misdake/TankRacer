use std::{fs, path::PathBuf, process::Command};
use tankracer_assets::{archive::Archive, level::Level};
use tankracer_game::pose::Pose;

struct Reader<'a>(&'a [u8]);
impl<'a> Reader<'a> {
    fn word(&mut self) -> u32 {
        let (head, tail) = self.0.split_at(4);
        self.0 = tail;
        u32::from_le_bytes(head.try_into().unwrap())
    }
    fn string(&mut self) -> &'a str {
        let n = self.word() as usize;
        let (head, tail) = self.0.split_at(n);
        self.0 = tail;
        std::str::from_utf8(head).unwrap()
    }
}

#[test]
#[ignore = "requires common C level-record oracle; set TANKRACER_LEVEL_REFERENCE"]
fn authored_records_and_sequential_initial_poses_match_c_on_all_cup_maps() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let output_dir = root.join("build/tests/rust-level-reference");
    fs::create_dir_all(&output_dir).unwrap();
    let executable = std::env::var_os("TANKRACER_LEVEL_REFERENCE").expect("C level oracle");
    let (mut files, mut records) = (0, 0);
    for map in [
        "ENGLAND", "MEXICO", "SWAMP", "EAST", "THEME", "BIO", "MONACO", "CIRCUIT",
    ] {
        let bytes = fs::read(
            root.join("assets/game/LEVELS")
                .join(map)
                .join("FASTFILE.DAT"),
        )
        .unwrap();
        let archive = Archive::parse(&bytes).unwrap();
        for entry in archive.entries() {
            let source = archive.get(entry.name).unwrap();
            if !source.starts_with(b"FSTPC 5") {
                continue;
            }
            let label = format!("{map}/{}", entry.name);
            let level = Level::decode(source).unwrap_or_else(|e| panic!("{label}: {e}"));
            let path = output_dir.join(format!("{map}-{}", entry.name));
            fs::write(&path, source).unwrap();
            let output = Command::new(&executable).arg(&path).output().unwrap();
            assert!(
                output.status.success(),
                "{label}: {:?} {}",
                output.status.code(),
                String::from_utf8_lossy(&output.stderr)
            );
            assert_eq!(&output.stdout[..4], b"LV01");
            let mut input = Reader(&output.stdout[4..]);
            for (index, object) in level.objects.iter().enumerate() {
                assert_eq!(input.word(), 1, "{label} #{index}");
                assert_eq!(input.string(), object.class, "{label} #{index} class");
                assert_eq!(input.string(), object.name, "{label} #{index} name");
                assert_eq!(input.string(), object.shape, "{label} #{index} shape");
                let pose = Pose::from_level(object);
                for value in pose.position.into_iter().chain(pose.matrix.0) {
                    assert_eq!(input.word() as i32, value, "{label} #{index} pose");
                }
                assert_eq!(
                    input.string(),
                    object.parameters,
                    "{label} #{index} parameters"
                );
                records += 1;
            }
            assert_eq!(input.word(), 0, "{label} remaining objects");
            assert!(input.0.is_empty(), "{label} unread output");
            files += 1;
        }
    }
    assert!(files >= 24 && records > 1000);
    eprintln!(
        "{files} authored object files / {records} records: common C names, poses and nested parameters exact"
    );
}
