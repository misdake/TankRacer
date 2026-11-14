use std::{fs, path::PathBuf, process::Command};
use tankracer_assets::{archive::Archive, object_catalog::Catalog};

#[test]
#[ignore = "requires actual C OBPARAMS startup; set TANKRACER_OBJECT_CATALOG_REFERENCE"]
fn all_object_presets_match_native_widths_names_flags_and_defaults() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../assets/game");
    let bytes = fs::read(root.join("DATA/FASTFILE.DAT")).unwrap();
    let archive = Archive::parse(&bytes).unwrap();
    let catalog = Catalog::decode(archive.get("OBPARAMS.DAT").unwrap()).unwrap();
    let output = Command::new(
        std::env::var_os("TANKRACER_OBJECT_CATALOG_REFERENCE").expect("catalog oracle"),
    )
    .arg(&root)
    .output()
    .unwrap();
    assert!(
        output.status.success(),
        "{:?} {}",
        output.status.code(),
        String::from_utf8_lossy(&output.stderr)
    );
    assert_eq!(&output.stdout[..4], b"OC04");
    assert_eq!(
        u32::from_le_bytes(output.stdout[4..8].try_into().unwrap()) as usize,
        catalog.entries.len()
    );
    let mut expected = Vec::new();
    for o in &catalog.entries {
        // Packed layout belongs only to the oracle serializer. Production
        // resources remain semantic Rust fields with no address/offset access.
        let mut row = [0_u8; 76];
        row[..o.name.len()].copy_from_slice(o.name.as_bytes());
        row[25] = o.friction;
        row[26..28].copy_from_slice(&o.elasticity.to_le_bytes());
        row[28] = o.height_factor;
        row[29] = o.threshold;
        row[30..32].copy_from_slice(&o.y_spin_factor.to_le_bytes());
        row[32..34].copy_from_slice(&o.solidity.to_le_bytes());
        for (i, gain) in [
            o.hit_spin_x,
            o.hit_spin_z,
            o.hit_slow,
            o.spin_slow,
            o.flatten,
        ]
        .into_iter()
        .enumerate()
        {
            row[34 + i * 4..38 + i * 4].copy_from_slice(&gain.packed().to_le_bytes());
        }
        row[54..56].copy_from_slice(&o.stop_spin.to_le_bytes());
        row[56..58].copy_from_slice(&o.max_deflection.to_le_bytes());
        row[58..62].copy_from_slice(&o.flags.to_le_bytes());
        row[62..64].copy_from_slice(&o.collision_margin.to_le_bytes());
        row[64..73].copy_from_slice(&o.bash_sound);
        expected.extend(row);
    }
    for value in [
        catalog.fall.minimum_turn_rate as u32,
        catalog.fall.maximum_turn_rate as u32,
        catalog.fall.squash_angle as u32,
        catalog.fall.gravity as u32,
        catalog.fall.elasticity_multiplier as u32,
        catalog.fall.elasticity_shift,
        catalog.general.gravity as u32,
        catalog.general.circuit_power_mul as u32,
        u32::from(catalog.moon.player.multiplier),
        u32::from(catalog.moon.player.shift),
        u32::from(catalog.moon.other.multiplier),
        u32::from(catalog.moon.other.shift),
    ] {
        expected.extend(value.to_le_bytes());
    }
    assert_eq!(&output.stdout[8..], expected);
    eprintln!(
        "{} typed object presets + 6 Fall, 2 General, 4 Moon settings / {} native bytes exact",
        catalog.entries.len(),
        expected.len()
    );
}
