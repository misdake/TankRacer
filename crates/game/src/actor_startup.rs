//! Authored actor construction. These are owned initial states, not RNG padding.
//! Runtime behavior and drawing are supplied by the actor's later integration.
use crate::{
    math::Matrix,
    object_query::ObstacleId,
    pose::Pose,
    vehicle_model::{ModelBank, ShapeSource, Space},
};
use std::{error::Error, sync::Arc};
use tankracer_assets::{
    level,
    model::Model,
    parameters::{Parameters, Values, fixed1024},
    terrain::TerrainData,
};
pub mod emitter;
pub mod flock;
pub mod lamps;
mod linkage;
pub mod pickup;
pub mod track_camera;
pub mod train;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Kind {
    Noisy,
    Smoker,
    Trackcam,
    Bomber,
    Train,
    Flock,
    Powerup,
    Background,
    Lamps,
}
impl Kind {
    pub const ALL: [Self; 9] = [
        Self::Noisy,
        Self::Smoker,
        Self::Trackcam,
        Self::Bomber,
        Self::Train,
        Self::Flock,
        Self::Powerup,
        Self::Background,
        Self::Lamps,
    ];
    pub fn name(self) -> &'static str {
        match self {
            Self::Noisy => "Noisy",
            Self::Smoker => "Smoker",
            Self::Trackcam => "Trackcam",
            Self::Bomber => "Bomber",
            Self::Train => "Train",
            Self::Flock => "Flock",
            Self::Powerup => "Powerup",
            Self::Background => "BckgrdOb",
            Self::Lamps => "Lamps",
        }
    }
    pub fn named(name: &str) -> Option<Self> {
        Self::ALL
            .into_iter()
            .find(|k| k.name().eq_ignore_ascii_case(name))
    }
}
pub struct Resources<'a, S> {
    pub source: &'a S,
    pub models: &'a mut ModelBank,
    pub terrain: &'a TerrainData,
    pub random: &'a mut u32,
    pub camera: tankracer_assets::object_catalog::Camera,
}
impl<S: ShapeSource> Resources<'_, S> {
    fn shape(&mut self, name: &str) -> Result<Arc<Model>, Box<dyn Error>> {
        let source = if self.source.shape(Space::Level, name).is_some() {
            Space::Level
        } else {
            Space::Shared
        };
        self.models
            .load(self.source, source, name)
            .map_err(|e| format!("actor model {name}: {e:?}"))?
            .ok_or_else(|| format!("Missing actor model {name}").into())
    }
}
#[derive(Debug)]
pub enum Actor {
    Flock(Box<flock::Flock>),
    Emitter(emitter::Emitter),
    Train(train::Train),
    Smoker {
        white: bool,
        billow: bool,
        particles: Box<[Smoke; 16]>,
    },
    Noisy {
        sounds: Vec<Sound>,
        selected: i32,
    },
    Trackcam(track_camera::TrackCamera),
    Background {
        range: i32,
        kind: u32,
        target: Option<String>,
        linked_target: Option<crate::object_query::EntityId>,
        position: [i32; 3],
        shake: [i16; 2],
    },
    Lamps(lamps::Lamps),
    Powerup(pickup::Pickup),
}
#[derive(Debug, Clone, Copy, Default)]
pub struct Smoke {
    pub position: [i32; 3],
    pub life: i16,
}
#[derive(Debug)]
pub struct Sound {
    pub name: String,
    pub values: [i32; 4],
}
#[derive(Default)]
pub struct Store {
    entries: Vec<(ObstacleId, Actor)>,
    pub(crate) pickup: pickup::Setup,
    pub track_cameras: Vec<ObstacleId>,
}
impl Store {
    pub fn entries(&self) -> &[(ObstacleId, Actor)] {
        &self.entries
    }
    pub fn get_mut(&mut self, id: ObstacleId) -> Option<&mut Actor> {
        let at = self
            .entries
            .binary_search_by_key(&id.0, |(id, _)| id.0)
            .ok()?;
        Some(&mut self.entries[at].1)
    }
    pub(crate) fn insert(&mut self, id: ObstacleId, actor: Actor) -> Result<(), i32> {
        match self.entries.binary_search_by_key(&id.0, |(id, _)| id.0) {
            Ok(_) => Err(-1),
            Err(at) => {
                self.entries.insert(at, (id, actor));
                Ok(())
            }
        }
    }
    pub(crate) fn remove(&mut self, id: ObstacleId) {
        if let Ok(at) = self.entries.binary_search_by_key(&id.0, |(id, _)| id.0) {
            self.entries.remove(at);
        }
        self.track_cameras.retain(|other| *other != id);
    }
    pub fn pickup_geometry(&self) -> &pickup::Geometry {
        &self.pickup.geometry
    }
}
pub(crate) fn integer3(p: Parameters<'_>, key: &str) -> Result<Option<[i32; 3]>, Box<dyn Error>> {
    p.find(key)
        .map(|mut v| Ok([int(&mut v)?, int(&mut v)?, int(&mut v)?]))
        .transpose()
}
fn int(v: &mut Values<'_>) -> Result<i32, Box<dyn Error>> {
    v.integer().ok_or_else(|| "Missing actor integer".into())
}
fn real(v: &mut Values<'_>) -> Result<i32, Box<dyn Error>> {
    v.real32()
        .map(fixed1024)
        .ok_or_else(|| "Missing actor real".into())
}
fn point(p: Parameters<'_>, key: &str) -> Result<Option<[i32; 3]>, Box<dyn Error>> {
    p.find(key)
        .map(|mut v| Ok([real(&mut v)?, real(&mut v)?, real(&mut v)?]))
        .transpose()
}
pub fn construct<S: ShapeSource>(
    record: level::Object<'_>,
    pose: &mut Pose,
    resources: &mut Resources<'_, S>,
    state: &mut Store,
) -> Result<Actor, Box<dyn Error>> {
    let p = Parameters::new(record.parameters).map_err(|_| "Non-ASCII actor parameters")?;
    Ok(match Kind::named(record.class).ok_or("Unknown actor")? {
        Kind::Flock => Actor::Flock(Box::new(flock::Flock::construct(p, pose, resources)?)),
        Kind::Bomber => Actor::Emitter(emitter::Emitter::construct(p, *pose, resources)?),
        Kind::Train => Actor::Train(train::Train::construct(p, pose, resources)?),
        Kind::Smoker => Actor::Smoker {
            white: p.find(":white").is_some(),
            billow: p.find(":billow").is_some(),
            particles: Box::new(
                [Smoke {
                    position: pose.position,
                    life: 0,
                }; 16],
            ),
        },
        Kind::Noisy => {
            let mut sounds = vec![];
            if let Some(mut v) = p.find(":sound_list") {
                let count = int(&mut v)?;
                if !(0..=100000).contains(&count) {
                    return Err("Invalid sound count".into());
                }
                for _ in 0..count {
                    sounds.push(Sound {
                        name: v.token_word().ok_or("Missing sound")?.into(),
                        values: [real(&mut v)?, real(&mut v)?, real(&mut v)?, real(&mut v)?],
                    });
                }
            }
            Actor::Noisy {
                sounds,
                selected: -1,
            }
        }
        Kind::Trackcam => Actor::Trackcam(track_camera::TrackCamera::construct(
            p,
            *pose,
            resources.camera.near,
        )?),
        Kind::Background => {
            let mut kind = 0;
            let mut target = None;
            let mut position = [0; 3];
            let mut shake = [0; 2];
            if p.find(":ufo").is_some() {
                kind = 1;
                target = p
                    .find(":target")
                    .or_else(|| p.find(":Target"))
                    .and_then(|mut v| v.token_word())
                    .map(str::to_owned);
                position = point(p, ":new_pos")?.unwrap_or_default();
            }
            if p.find(":screen_shake").is_some() {
                kind = 2;
                shake = [
                    p.integer(":time").unwrap_or(0).wrapping_mul(1000) as i16,
                    p.integer(":max_amount").unwrap_or(0) as i16,
                ];
            }
            Actor::Background {
                range: p.integer(":inrange").unwrap_or(0).wrapping_shl(10) >> 3,
                kind,
                target,
                linked_target: None,
                position,
                shake,
            }
        }
        Kind::Lamps => Actor::Lamps(lamps::Lamps::load(
            resources
                .source
                .file(Space::Level, "LAMPS.DAT")
                .ok_or("Missing LAMPS.DAT")?,
        )?),
        Kind::Powerup => Actor::Powerup(pickup::Pickup::construct(
            record,
            p,
            pose,
            resources,
            &mut state.pickup,
        )?),
    })
}
