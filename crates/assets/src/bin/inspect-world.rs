//! Resource inspection is a native Rust tool, independent of browser/game startup.
use std::{collections::BTreeMap, error::Error};
use tankracer_assets::{archive::Archive, world::World};

fn main() -> Result<(), Box<dyn Error>> {
    let file = std::env::args()
        .nth(1)
        .ok_or("Usage: inspect-world /path/to/FASTFILE.DAT")?;
    let bytes = std::fs::read(&file)?;
    let archive = Archive::parse(&bytes)?;
    let name = archive
        .entries()
        .iter()
        .find(|e| e.name.ends_with(".BSM"))
        .ok_or("Archive has no BSM world")?
        .name;
    let world = World::decode(archive.get(name).ok_or("World entry missing")?)?;
    let mut materials = BTreeMap::<String, usize>::new();
    let mut flags = BTreeMap::<u16, usize>::new();
    let (mut vertices, mut triangles, mut billboard, mut disabled) = (0, 0, 0, 0);
    let mut minimum = [i64::MAX; 3];
    let mut maximum = [i64::MIN; 3];
    for mesh in &world.meshes {
        vertices += mesh.vertices.len();
        for vertex in &mesh.vertices {
            for axis in 0..3 {
                let p = i64::from(mesh.position[axis]) + i64::from(vertex[axis]);
                minimum[axis] = minimum[axis].min(p);
                maximum[axis] = maximum[axis].max(p);
            }
        }
        for triangle in &mesh.triangles {
            triangles += 1;
            billboard += usize::from(triangle.flags & 30 != 0);
            disabled += usize::from(triangle.flags & 64 != 0);
            // The decoder interns case-insensitive identities once per world.
            *materials
                .entry(triangle.material.map_or_else(
                    || "<untextured>".into(),
                    |id| world.materials[id.index()].name.clone(),
                ))
                .or_default() += 1;
            *flags.entry(triangle.flags).or_default() += 1;
        }
    }
    println!("{file}: {name}");
    println!(
        "{} meshes, {vertices} source vertices, {triangles} triangles",
        world.meshes.len()
    );
    println!("{billboard} camera-facing triangles, {disabled} disabled triangles");
    println!("Raw world bounds: {minimum:?} .. {maximum:?}");
    println!("{} material identities:", materials.len());
    for (name, count) in materials {
        println!("  {name}: {count} triangles");
    }
    println!("Triangle flag combinations:");
    for (flag, count) in flags {
        println!("  0x{flag:04x}: {count} triangles");
    }
    Ok(())
}
