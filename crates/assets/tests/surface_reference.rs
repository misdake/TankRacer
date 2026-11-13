//! Exact image/mip comparison with the existing C decoder/device conversion.
use std::{path::PathBuf, process::Command};
use tankracer_assets::{archive::Archive, surface::Surface};

fn word(bytes: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap())
}

#[test]
#[ignore = "requires the C surface oracle; set TANKRACER_SURFACE_REFERENCE"]
fn all_shipped_images_and_mips_match_c() {
    let executable =
        std::env::var_os("TANKRACER_SURFACE_REFERENCE").expect("Set TANKRACER_SURFACE_REFERENCE");
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let names = [
        "DATA",
        "COMMON",
        "GUI",
        "INGAME",
        "TANKS/BRONZE",
        "TANKS/SILVER",
        "TANKS/GOLD",
        "LEVELS/ENGLAND",
        "LEVELS/MEXICO",
        "LEVELS/SWAMP",
        "LEVELS/EAST",
        "LEVELS/THEME",
        "LEVELS/BIO",
        "LEVELS/MONACO",
        "LEVELS/CIRCUIT",
    ];
    let paths: Vec<_> = names
        .iter()
        .map(|n| root.join(n).join("FASTFILE.DAT"))
        .collect();
    let bytes: Vec<_> = paths.iter().map(|p| std::fs::read(p).unwrap()).collect();
    let archives: Vec<_> = bytes.iter().map(|b| Archive::parse(b).unwrap()).collect();
    let output = Command::new(executable).args(&paths).output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
    let (mut cursor, mut cases, mut pixels, mut rejected) = (0, 0, 0, 0);
    while cursor < output.stdout.len() {
        let header = &output.stdout[cursor..cursor + 52];
        cursor += 52;
        let pack = word(header, 0) as usize;
        let length = word(header, 4) as usize;
        let mip = word(header, 8);
        let status = word(header, 12) as i32;
        let name = std::str::from_utf8(&output.stdout[cursor..cursor + length]).unwrap();
        cursor += length;
        let count = word(header, 48) as usize;
        let expected = &output.stdout[cursor..cursor + count];
        cursor += count;
        let source = archives[pack].get(name).unwrap();
        let surface = Surface::open(source);
        let decoded = surface
            .as_ref()
            .map_err(|e| e.clone())
            .and_then(|s| s.decode(mip));
        if status != 0 {
            assert!(
                decoded.is_err(),
                "{} {name} mip{mip}: C rejected but Rust accepted",
                names[pack]
            );
            rejected += 1;
            continue;
        }
        let info = surface.unwrap().info;
        let image = decoded.unwrap_or_else(|e| panic!("{} {name} mip{mip}: {e}", names[pack]));
        assert_eq!(image.width, word(header, 16));
        assert_eq!(image.height, word(header, 20));
        assert_eq!(image.flags, word(header, 24));
        assert_eq!(u32::from(info.format), word(header, 28));
        assert_eq!(u32::from(info.kind), word(header, 32));
        assert_eq!(u32::from(info.bits), word(header, 36));
        assert_eq!(u32::from(info.compression), word(header, 40));
        assert_eq!(image.consumed, word(header, 44) as usize);
        assert_eq!(image.rgba.len(), count);
        assert!(
            image.rgba == expected,
            "{} {name} mip{mip}: RGBA bytes differ",
            names[pack]
        );
        cases += 1;
        pixels += count / 4;
    }
    let resources = archives
        .iter()
        .map(|a| {
            a.entries()
                .iter()
                .filter(|e| e.name.ends_with(".SUR"))
                .count()
        })
        .sum::<usize>();
    assert!(cases + rejected >= resources);
    assert!(pixels > 1_000_000);
    eprintln!(
        "C surface reference: {resources} resources, {cases} image/mips, {pixels} pixels exact, {rejected} explicit rejections"
    );
}
