use avian3d::{math::Vector, prelude::*};
use bevy::{
    MinimalPlugins,
    app::{App, PluginsState},
    ecs::{entity::Entity, schedule::ScheduleLabel},
    prelude::Transform,
    time::{Time, TimeUpdateStrategy},
    transform::TransformPlugin,
};
use core::time::Duration;

pub const ENGINE_ID: &str = "avian3d";
pub const CASE_ID: &str = "box_container_pile_10k";
pub const PILE_X_COUNT: usize = 25;
pub const PILE_Y_COUNT: usize = 16;
pub const PILE_Z_COUNT: usize = 25;
pub const DYNAMIC_BODY_COUNT: usize = PILE_X_COUNT * PILE_Y_COUNT * PILE_Z_COUNT;
pub const STATIC_BODY_COUNT: usize = 5;
pub const BODY_COUNT: usize = DYNAMIC_BODY_COUNT + STATIC_BODY_COUNT;
pub const HALF_EXTENT: f32 = 0.5;
pub const CUBE_SIZE: f32 = HALF_EXTENT * 2.0;
pub const SPACING: f32 = 1.02;
pub const INITIAL_Y: f32 = 24.51;
pub const TIMESTEP: f32 = 1.0 / 60.0;
pub const PHYSICS_SUBSTEP_COUNT: u32 = 4;
pub const INNER_HALF_WIDTH: f32 = 15.5;
pub const CEILING_Y: f32 = 96.0;
pub const FIXTURE_SEMANTIC: &str = "open_container_falling_pile";
pub const FIXTURE_VERSION: &str = "avian3d_open_container_v1";
pub const TOOLCHAIN_ID: &str = "rust_1_95_msvc_cargo_release";
pub const ENGINE_REF: &str = "fc99fdcdbff804fbbe6dc1eb7fc4137e677853d2";

#[derive(ScheduleLabel, Clone, Debug, PartialEq, Eq, Hash)]
pub struct AvianBenchmarkSchedule;

pub struct AvianWorld {
    pub app: App,
    pub dynamic_entities: Vec<Entity>,
    pub body_count: usize,
    pub shape_count: usize,
    pub completed_step_count: usize,
}

#[derive(Clone, Copy)]
pub struct AvianTransform {
    pub position_x: f32,
    pub position_y: f32,
    pub position_z: f32,
    pub rotation_x: f32,
    pub rotation_y: f32,
    pub rotation_z: f32,
    pub rotation_w: f32,
}

#[derive(Clone, Copy)]
pub struct AvianStaticBox {
    pub position_x: f32,
    pub position_y: f32,
    pub position_z: f32,
    pub half_extent_x: f32,
    pub half_extent_y: f32,
    pub half_extent_z: f32,
}

#[derive(Clone, Copy)]
pub struct StabilityCounters {
    pub invalid_transform_count: usize,
    pub below_floor_count: usize,
    pub out_of_bounds_count: usize,
}

pub fn create_world() -> Result<AvianWorld, i32> {
    let mut app = App::new();
    app.add_plugins((
        MinimalPlugins,
        TransformPlugin,
        PhysicsPlugins::new(AvianBenchmarkSchedule),
    ))
    .insert_resource(Gravity(Vector::new(0.0, -10.0, 0.0)))
    .insert_resource(SubstepCount(PHYSICS_SUBSTEP_COUNT))
    .insert_resource(Time::from_hz(60.0))
    .insert_resource(TimeUpdateStrategy::ManualDuration(Duration::from_secs_f64(
        TIMESTEP as f64,
    )));

    while app.plugins_state() != PluginsState::Ready {
        bevy::tasks::tick_global_task_pools_on_main_thread();
    }
    app.finish();
    app.cleanup();

    let mut dynamic_entities = Vec::with_capacity(DYNAMIC_BODY_COUNT);

    add_static_box(&mut app, 0.0, -0.5, 0.0, 14.5, 0.5, 14.5);
    add_static_box(&mut app, -14.5, 11.75, 0.0, 0.5, 12.25, 14.5);
    add_static_box(&mut app, 14.5, 11.75, 0.0, 0.5, 12.25, 14.5);
    add_static_box(&mut app, 0.0, 11.75, -14.5, 14.5, 12.25, 0.5);
    add_static_box(&mut app, 0.0, 11.75, 14.5, 14.5, 12.25, 0.5);

    let origin_x = -0.5 * (PILE_X_COUNT as f32 - 1.0) * SPACING;
    let origin_z = -0.5 * (PILE_Z_COUNT as f32 - 1.0) * SPACING;
    for y in 0..PILE_Y_COUNT {
        for z in 0..PILE_Z_COUNT {
            for x in 0..PILE_X_COUNT {
                let position_x = origin_x + x as f32 * SPACING;
                let position_y = INITIAL_Y + y as f32 * SPACING;
                let position_z = origin_z + z as f32 * SPACING;
                let entity = app
                    .world_mut()
                    .spawn((
                        RigidBody::Dynamic,
                        Collider::cuboid(CUBE_SIZE, CUBE_SIZE, CUBE_SIZE),
                        Position::from_xyz(position_x, position_y, position_z),
                        Rotation::IDENTITY,
                        Transform::from_xyz(position_x, position_y, position_z),
                        SleepingDisabled,
                    ))
                    .id();
                dynamic_entities.push(entity);
            }
        }
    }

    if dynamic_entities.len() != DYNAMIC_BODY_COUNT {
        eprintln!(
            "invalid_result body_count={} shape_count={} dynamic_body_count={}",
            BODY_COUNT,
            BODY_COUNT,
            dynamic_entities.len()
        );
        return Err(2);
    }

    Ok(AvianWorld {
        app,
        dynamic_entities,
        body_count: BODY_COUNT,
        shape_count: BODY_COUNT,
        completed_step_count: 0,
    })
}

pub fn add_static_box(app: &mut App, x: f32, y: f32, z: f32, hx: f32, hy: f32, hz: f32) {
    app.world_mut().spawn((
        RigidBody::Static,
        Collider::cuboid(hx * 2.0, hy * 2.0, hz * 2.0),
        Position::from_xyz(x, y, z),
        Rotation::IDENTITY,
        Transform::from_xyz(x, y, z),
    ));
}

pub fn step_world(world: &mut AvianWorld, step_count: usize) {
    for _ in 0..step_count {
        world
            .app
            .world_mut()
            .resource_mut::<Time>()
            .advance_by(Duration::from_secs_f64(TIMESTEP as f64));
        world
            .app
            .world_mut()
            .run_schedule(AvianBenchmarkSchedule);
        world.completed_step_count += 1;
    }
}

pub fn sample_transforms(world: &AvianWorld, transforms: &mut [AvianTransform]) -> Result<(), i32> {
    if transforms.len() < DYNAMIC_BODY_COUNT {
        return Err(2);
    }
    for index in 0..DYNAMIC_BODY_COUNT {
        let entity = world.app.world().entity(world.dynamic_entities[index]);
        let position = match entity.get::<Position>() {
            Some(value) => value.0,
            None => return Err(2),
        };
        let rotation = match entity.get::<Rotation>() {
            Some(value) => value.0,
            None => return Err(2),
        };
        transforms[index] = AvianTransform {
            position_x: position.x,
            position_y: position.y,
            position_z: position.z,
            rotation_x: rotation.x,
            rotation_y: rotation.y,
            rotation_z: rotation.z,
            rotation_w: rotation.w,
        };
    }
    Ok(())
}

pub fn copy_static_boxes(boxes: &mut [AvianStaticBox]) -> Result<(), i32> {
    if boxes.len() < STATIC_BODY_COUNT {
        return Err(2);
    }
    boxes[0] = AvianStaticBox { position_x: 0.0, position_y: -0.5, position_z: 0.0, half_extent_x: 14.5, half_extent_y: 0.5, half_extent_z: 14.5 };
    boxes[1] = AvianStaticBox { position_x: -14.5, position_y: 11.75, position_z: 0.0, half_extent_x: 0.5, half_extent_y: 12.25, half_extent_z: 14.5 };
    boxes[2] = AvianStaticBox { position_x: 14.5, position_y: 11.75, position_z: 0.0, half_extent_x: 0.5, half_extent_y: 12.25, half_extent_z: 14.5 };
    boxes[3] = AvianStaticBox { position_x: 0.0, position_y: 11.75, position_z: -14.5, half_extent_x: 14.5, half_extent_y: 12.25, half_extent_z: 0.5 };
    boxes[4] = AvianStaticBox { position_x: 0.0, position_y: 11.75, position_z: 14.5, half_extent_x: 14.5, half_extent_y: 12.25, half_extent_z: 0.5 };
    Ok(())
}

pub fn stability_counters(world: &AvianWorld) -> StabilityCounters {
    let mut invalid_transform_count = 0;
    let mut below_floor_count = 0;
    let mut out_of_bounds_count = 0;

    for entity in &world.dynamic_entities {
        let entity_ref = world.app.world().entity(*entity);
        let position = match entity_ref.get::<Position>() {
            Some(value) => value.0,
            None => {
                invalid_transform_count += 1;
                continue;
            }
        };
        let rotation = match entity_ref.get::<Rotation>() {
            Some(value) => value.0,
            None => {
                invalid_transform_count += 1;
                continue;
            }
        };
        if !position.x.is_finite()
            || !position.y.is_finite()
            || !position.z.is_finite()
            || !rotation.x.is_finite()
            || !rotation.y.is_finite()
            || !rotation.z.is_finite()
            || !rotation.w.is_finite()
        {
            invalid_transform_count += 1;
        }
        if position.y < 0.0 {
            below_floor_count += 1;
        }
        if position.x < -INNER_HALF_WIDTH
            || position.x > INNER_HALF_WIDTH
            || position.z < -INNER_HALF_WIDTH
            || position.z > INNER_HALF_WIDTH
            || position.y < 0.0
            || position.y > CEILING_Y
        {
            out_of_bounds_count += 1;
        }
    }

    StabilityCounters {
        invalid_transform_count,
        below_floor_count,
        out_of_bounds_count,
    }
}

pub fn case_status(counters: StabilityCounters) -> &'static str {
    if counters.invalid_transform_count == 0 && counters.below_floor_count == 0 {
        "ok"
    } else {
        "invalid_stability_counters"
    }
}

pub fn metric_status(case_status: &str) -> &'static str {
    if case_status == "ok" {
        "ok"
    } else {
        "invalid_result"
    }
}

pub fn host_route() -> &'static str {
    if cfg!(target_os = "windows") {
        "windows"
    } else {
        "linux"
    }
}
