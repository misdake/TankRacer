//! M3 terrain/vehicle session. The renderer receives a read-only proxy view.
use glam::{Mat4, Vec3};
use std::sync::Arc;
use tankracer_assets::{config::ResolveContext, terrain::TerrainData};
use tankracer_game::{
    drive,
    obstacle_collision::Invocation,
    pose,
    scene_objects::collision::{Collision, Services, Sound},
    vehicle_loading::Request,
    vehicle_model::BuildContext,
    vehicle_resources,
    vehicle_runtime::{Devices, Settings, SinglePlayer},
    vehicle_scene::spray,
    vehicle_startup::Environment,
};
use tankracer_render::{
    geometry::MeshData,
    gpu::{Frame, GpuMesh, Renderer},
    material::Material,
};
use tankracer_scene::{
    camera::Camera,
    transform::{physics_pose, physics_position},
};
use wasm_bindgen::JsValue;

struct SilentDevices;
impl Services for SilentDevices {
    fn sound(&mut self, _: Sound, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
    fn feedback(&mut self, _: i32, _: &mut Collision<'_>, _: Invocation<'_>) -> Result<(), i32> {
        Ok(())
    }
}
impl Devices for SilentDevices {
    fn surface_feedback(&mut self) -> Result<(), i32> {
        Ok(())
    }
    fn impact_feedback(&mut self, _: i32) -> Result<(), i32> {
        Ok(())
    }
}
pub struct VehicleView {
    mesh: GpuMesh,
    bounds: Mat4,
    transform: Mat4,
    pub position: Vec3,
    forward: Vec3,
}
impl VehicleView {
    pub fn draw(&self, frame: &mut Frame<'_>, view_projection: Mat4) {
        frame.draw(
            &self.mesh,
            &Material::opaque([0.9, 0.6, 0.16, 1.]),
            None,
            view_projection * self.transform * self.bounds,
        );
    }
    pub fn camera(&self) -> Camera {
        Camera::looking_at(
            self.position - self.forward * 22. + Vec3::Y * 10.,
            self.position + Vec3::Y * 2.,
        )
    }
}
pub struct Session {
    pub game: SinglePlayer,
    pub view: VehicleView,
}
impl Session {
    pub fn load(
        renderer: &Renderer,
        terrain: Arc<TerrainData>,
        data: &[u8],
        bronze: &[u8],
        shared: &[u8],
    ) -> Result<Self, JsValue> {
        let resources = vehicle_resources::prepare(
            data,
            &[bronze],
            shared,
            1,
            BuildContext {
                resolve: ResolveContext {
                    player: true,
                    ..ResolveContext::default()
                },
                ambient_wobble: false,
                session_active: false,
            },
        )
        .map_err(|e| JsValue::from_str(&e.to_string()))?;
        let bounds = resources.model.geometry_bounds;
        let min = physics_position([bounds.min[0], bounds.min[1], bounds.max[2]]);
        let max = physics_position([bounds.max[0], bounds.max[1], bounds.min[2]]);
        let bounds =
            Mat4::from_translation((min + max) * 0.5) * Mat4::from_scale((max - min) * 0.5);
        let game = SinglePlayer::new(
            terrain,
            resources.surfaces,
            Request {
                model: resources.model,
                spawn: pose::Pose::default(),
                static_allocation: true,
                environment: Environment {
                    ambient_wobble: false,
                    session_active: false,
                    ambient_phase: 0,
                    gun_pivot: [0; 3],
                },
            },
            Settings {
                map: 0,
                mode: 0,
                special: false,
                environment_flag: 0,
                seed: 0,
                spray_materials: spray::Materials {
                    filth: 1,
                    crap: 2,
                    splash: 3,
                },
                light_material: 4,
                hardware: true,
            },
        )
        .map_err(|e| JsValue::from_str(&format!("Vehicle startup: {e:?}")))?;
        let view = VehicleView {
            mesh: renderer.upload_mesh(&MeshData::cube())?,
            bounds,
            transform: Mat4::IDENTITY,
            position: Vec3::ZERO,
            forward: Vec3::NEG_Z,
        };
        let mut session = Self { game, view };
        session.refresh_view();
        Ok(session)
    }
    fn refresh_view(&mut self) {
        let body = self.game.vehicles.entries[0]
            .as_ref()
            .unwrap()
            .physics
            .motion
            .poses
            .body;
        self.view.transform = physics_pose(body.position, body.matrix.0);
        self.view.position = physics_position(body.position);
        self.view.forward = -self.view.transform.z_axis.truncate().normalize_or_zero();
    }
    pub fn step(&mut self, input: drive::Input, dt: i32, camera: &Camera) -> Result<(), JsValue> {
        let p = camera.position * 128.;
        let f = camera.forward() * 4096.;
        self.game.camera_position = [p.x as i32, p.y as i32, -p.z as i32];
        self.game.camera_forward = [f.x as i32, f.y as i32, -f.z as i32];
        self.game
            .step(input, dt, true, &mut SilentDevices)
            .map_err(|e| JsValue::from_str(&format!("Vehicle update: {e:?}")))?;
        self.refresh_view();
        Ok(())
    }
}
