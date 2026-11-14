//! Transient file decoding shared by native and browser startup.
use crate::vehicle_model::{BuildContext, Bundles, ModelBank, VehicleModel};
use std::error::Error;
use tankracer_assets::{
    archive::Archive,
    config::{Surfaces, Tank},
    model::ShapeArchive,
    object_catalog::Catalog,
};

pub struct Prepared {
    pub model: VehicleModel,
    pub surfaces: Surfaces,
}
pub(crate) fn shapes(bytes: &[u8]) -> Result<ShapeArchive<'_>, Box<dyn Error>> {
    let archive = Archive::parse(bytes)?;
    let file = archive
        .entries()
        .iter()
        .find(|e| e.name.ends_with(".SS"))
        .ok_or("missing shape archive")?;
    Ok(ShapeArchive::parse(
        archive.get(file.name).ok_or("missing shapes")?,
    )?)
}
/// The returned model owns its decoded resources. File mounts and parse cursors
/// can be released immediately; no original RAM or guest pointers are retained.
pub fn prepare(
    data: &[u8],
    vehicles: &[&[u8]],
    shared: &[u8],
    vehicle: u32,
    mut context: BuildContext,
) -> Result<Prepared, Box<dyn Error>> {
    if !(1..=15).contains(&vehicle) {
        return Err("vehicle must be 1..15".into());
    }
    let archive = Archive::parse(data)?;
    let surfaces = Surfaces::decode(archive.get("SURFS.DAT").ok_or("missing surfaces")?)?;
    let tank = Tank::decode(
        archive
            .get(&format!("TANK{vehicle}.DAT"))
            .ok_or("missing tank")?,
    )?;
    let catalog = Catalog::decode(
        archive
            .get("OBPARAMS.DAT")
            .ok_or("missing object settings")?,
    )
    .map_err(|e| format!("object settings: {e:?}"))?;
    context.resolve.circuit_power_mul = catalog.general.circuit_power_mul;
    let source = Bundles {
        vehicle: vehicles
            .iter()
            .map(|b| shapes(b))
            .collect::<Result<_, _>>()?,
        shared: vec![shapes(shared)?],
        level: vec![],
    };
    let model = VehicleModel::build(
        &tank,
        &source,
        &mut ModelBank::default(),
        &surfaces,
        context,
    )
    .map_err(|e| format!("vehicle model: {e:?}"))?;
    Ok(Prepared { model, surfaces })
}
