//! Complete construction boundary. This owns constructed actors without
//! claiming that their later runtime behaviors have already been integrated.
use crate::{
    dispatch::{Class, ClassId, MethodId},
    entity_factory::{self as factory, Environment, Invocation, Methods},
    object_query::EntityId,
    pose::Pose,
    scene_entity::{Handle, SessionObject},
    scene_loading, scenery,
    session::loading::{self, Files, Selection},
    vehicle_runtime::SinglePlayer,
};
use std::{error::Error, sync::Arc};
use tankracer_assets::{archive::Archive, object_catalog::Camera, terrain::TerrainData};

#[derive(Debug)]
pub struct StartingCamera {
    pub entity: EntityId,
    pub pose: Pose,
    pub angles: [u32; 3],
    pub displacement: [i32; 3],
    pub target_angles: [i32; 3],
    pub variant: u32,
    pub resize: [Resize; 2],
}
#[derive(Debug)]
pub struct Resize {
    pub rate: i16,
    pub multiplier: u8,
    pub shift: u8,
}
#[derive(Debug)]
pub struct FinishOwner {
    pub entity: EntityId,
    pub source: EntityId,
    /// Finish surrogate suppresses player-only HUD/effects and shape release.
    /// The later takeover transfers the real Player state/resources.
    pub surrogate: bool,
    pub flags: u32,
    pub bounds: crate::geometry::Bounds,
    pub configuration_cup: usize,
    pub player: bool,
}
pub struct Constructed {
    world: SinglePlayer,
    scenery: scenery::Loaded,
    pub camera: StartingCamera,
    pub finish: FinishOwner,
}
impl Constructed {
    pub fn world(&self) -> &SinglePlayer {
        &self.world
    }
    pub fn scenery(&self) -> &scenery::Loaded {
        &self.scenery
    }
    pub fn load(files: Files<'_>, selection: Selection) -> Result<Self, Box<dyn Error>> {
        let archive = Archive::parse(files.level)?;
        let bsm = archive
            .entries()
            .iter()
            .find(|e| e.name.ends_with(".BSM"))
            .ok_or("Missing terrain")?;
        let terrain = Arc::new(TerrainData::decode_bsm(
            archive.get(bsm.name).ok_or("Missing BSM")?,
        )?);
        let loaded =
            loading::Prepared::load_complete(terrain, files, selection)?.finish_unrouted()?;
        let mut world = loaded.world;
        let scenery = loaded.scenery.ok_or("Missing complete scenery")?;
        let player = world.vehicles.entries[world.player.0]
            .as_mut()
            .ok_or("Missing Player")?;
        let source = player.physics.entity;
        let pose = player.physics.motion.poses.body;
        let bounds = player.model.collision_bounds;
        let config = loaded.camera;
        let Owner::Camera(camera) = create(&mut world, Initializer::Camera { pose, config })?
        else {
            return Err("Camera factory returned a different owner".into());
        };
        world
            .initialize_player_route(100000)
            .map_err(|e| format!("Player route startup: {e:?}"))?;
        let Owner::Finish(finish) = create(
            &mut world,
            Initializer::Finish {
                source,
                bounds,
                cup: selection.cup,
            },
        )?
        else {
            return Err("Finish factory returned a different owner".into());
        };
        let player = world.vehicles.entries[world.player.0]
            .as_mut()
            .ok_or("Missing Player")?;
        player.brake.saved_turn = player.model.config.tank.drive.max_turn as i16;
        Ok(Self {
            world,
            scenery,
            camera,
            finish,
        })
    }
}
impl StartingCamera {
    fn construct(
        entity: EntityId,
        mut pose: Pose,
        config: Camera,
        random: &mut u32,
    ) -> Result<Self, Box<dyn Error>> {
        let (angles, _) = pose
            .matrix
            .angles()
            .map_err(|e| format!("Camera basis: {e:?}"))?;
        let variant = ((crate::random::next(random) >> 4) & 65535) % 3;
        let mut displacement = config.near;
        displacement[1] = displacement[1].wrapping_add(if variant != 0 { 3000 } else { -5000 });
        displacement[2] = displacement[2].wrapping_sub(if variant != 0 { 10000 } else { 25000 });
        pose.position[2] = pose.position[2].wrapping_add(displacement[2]);
        let target_angles = [
            angles[0] as i32,
            (angles[1] as i32).wrapping_add(match variant {
                1 => 65520,
                2 => -65520,
                _ => 0,
            }),
            angles[2] as i32,
        ];
        Ok(Self {
            entity,
            pose,
            angles,
            displacement,
            target_angles,
            variant,
            resize: [
                Resize {
                    rate: if variant != 0 { 2 } else { 1 },
                    multiplier: 1,
                    shift: if variant != 0 { 9 } else { 10 },
                },
                Resize {
                    rate: 1,
                    multiplier: 1,
                    shift: if variant != 0 { 10 } else { 9 },
                },
            ],
        })
    }
}
const ALLOCATE: MethodId = MethodId(110);
const INITIALIZE: MethodId = MethodId(111);
const UNSUPPORTED_TICK: MethodId = MethodId(109);
enum Initializer {
    Camera {
        pose: Pose,
        config: Camera,
    },
    Finish {
        source: EntityId,
        bounds: crate::geometry::Bounds,
        cup: usize,
    },
}
impl Initializer {
    fn object(&self) -> SessionObject {
        match self {
            Self::Camera { .. } => SessionObject::Camera,
            Self::Finish { .. } => SessionObject::FinishOwner,
        }
    }
}
enum Owner {
    Camera(StartingCamera),
    Finish(FinishOwner),
}
struct SessionFactory<'a> {
    initializer: Initializer,
    random: &'a mut u32,
    output: Option<Owner>,
    failure: Option<String>,
}
impl Methods<Handle> for SessionFactory<'_> {
    fn invoke(
        &mut self,
        method: MethodId,
        call: Invocation<'_, Handle>,
        env: Environment<'_>,
    ) -> Result<(), i32> {
        match method {
            ALLOCATE => {
                call.context
                    .entities
                    .record_mut(call.entity)
                    .ok_or(-1)?
                    .object = Some(Handle::Session(self.initializer.object()));
                call.context
                    .ticks
                    .flags
                    .resize(call.context.entities.capacity() as usize, 0);
                call.context.ticks.flags[call.entity.0 as usize] = 8;
                *call.result = 1;
                Ok(())
            }
            INITIALIZE => {
                self.output = Some(match self.initializer {
                    Initializer::Camera { pose, config } => {
                        match StartingCamera::construct(call.entity, pose, config, self.random) {
                            Ok(camera) => Owner::Camera(camera),
                            Err(e) => {
                                self.failure = Some(e.to_string());
                                return Err(-5);
                            }
                        }
                    }
                    Initializer::Finish {
                        source,
                        bounds,
                        cup,
                    } => {
                        call.context
                            .queues
                            .queue(call.context.ticks, call.entity, false, env.work_limit)
                            .map_err(|_| -5)?;
                        Owner::Finish(FinishOwner {
                            entity: call.entity,
                            source,
                            surrogate: true,
                            flags: 32,
                            bounds,
                            configuration_cup: cup,
                            player: false,
                        })
                    }
                });
                *call.result = call.entity.0;
                Ok(())
            }
            _ => Err(-5),
        }
    }
}
fn create(world: &mut SinglePlayer, initializer: Initializer) -> Result<Owner, Box<dyn Error>> {
    let class = ClassId(world.registry.classes.len());
    let mut methods = vec![None; scene_loading::TICK as usize + 1];
    methods[scene_loading::ALLOCATE as usize] = Some(ALLOCATE);
    methods[scene_loading::INITIALIZE as usize] = Some(INITIALIZE);
    methods[scene_loading::TICK as usize] = Some(UNSUPPORTED_TICK);
    world.registry.classes.push(Arc::new(Class {
        maximum_slot: scene_loading::TICK as i32,
        methods,
    }));
    let mut factory = SessionFactory {
        initializer,
        random: &mut world.shared.random,
        output: None,
        failure: None,
    };
    let result =
        world
            .identity
            .context()
            .create(class, world.registry.environment(100000), &mut factory);
    result.map_err(|e: factory::Error| format!("Session owner: {e:?}: {:?}", factory.failure))?;
    factory
        .output
        .ok_or_else(|| "Session constructor produced no owner".into())
}
