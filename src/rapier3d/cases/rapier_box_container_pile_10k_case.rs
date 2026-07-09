use rapier3d::prelude::*;

pub const ENGINE_ID: &str = "rapier3d";
pub const CASE_ID: &str = "box_container_pile_10k";
pub const PILE_X_COUNT: usize = 25;
pub const PILE_Y_COUNT: usize = 16;
pub const PILE_Z_COUNT: usize = 25;
pub const DYNAMIC_BODY_COUNT: usize = PILE_X_COUNT * PILE_Y_COUNT * PILE_Z_COUNT;
pub const STATIC_BODY_COUNT: usize = 5;
pub const BODY_COUNT: usize = DYNAMIC_BODY_COUNT + STATIC_BODY_COUNT;
pub const HALF_EXTENT: f32 = 0.5;
pub const SPACING: f32 = 1.02;
pub const INITIAL_Y: f32 = 24.51;
pub const TIMESTEP: f32 = 1.0 / 60.0;
pub const INNER_HALF_WIDTH: f32 = 15.5;
pub const CEILING_Y: f32 = 96.0;
pub const FIXTURE_SEMANTIC: &str = "open_container_falling_pile";
pub const FIXTURE_VERSION: &str = "rapier3d_open_container_v1";
pub const TOOLCHAIN_ID: &str = "rust_1_89_msvc_cargo_release";
pub const ENGINE_REF: &str = "a1ef31035613154dfb97a9e1d480c6a5eb9d0010";

pub struct RapierWorld {
    pub rigid_body_set: RigidBodySet,
    pub collider_set: ColliderSet,
    pub physics_pipeline: PhysicsPipeline,
    pub island_manager: IslandManager,
    pub broad_phase: DefaultBroadPhase,
    pub narrow_phase: NarrowPhase,
    pub impulse_joint_set: ImpulseJointSet,
    pub multibody_joint_set: MultibodyJointSet,
    pub ccd_solver: CCDSolver,
    pub dynamic_handles: Vec<RigidBodyHandle>,
}

#[derive(Clone, Copy)]
pub struct RapierTransform {
    pub position_x: f32,
    pub position_y: f32,
    pub position_z: f32,
    pub rotation_x: f32,
    pub rotation_y: f32,
    pub rotation_z: f32,
    pub rotation_w: f32,
}

#[derive(Clone, Copy)]
pub struct RapierStaticBox {
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

pub fn create_world() -> Result<RapierWorld, i32> {
    let mut rigid_body_set = RigidBodySet::with_capacity(BODY_COUNT);
    let mut collider_set = ColliderSet::with_capacity(BODY_COUNT);
    let mut dynamic_handles = Vec::with_capacity(DYNAMIC_BODY_COUNT);

    add_static_box(&mut rigid_body_set, &mut collider_set, 0.0, -0.5, 0.0, 14.5, 0.5, 14.5);
    add_static_box(&mut rigid_body_set, &mut collider_set, -14.5, 11.75, 0.0, 0.5, 12.25, 14.5);
    add_static_box(&mut rigid_body_set, &mut collider_set, 14.5, 11.75, 0.0, 0.5, 12.25, 14.5);
    add_static_box(&mut rigid_body_set, &mut collider_set, 0.0, 11.75, -14.5, 14.5, 12.25, 0.5);
    add_static_box(&mut rigid_body_set, &mut collider_set, 0.0, 11.75, 14.5, 14.5, 12.25, 0.5);

    let origin_x = -0.5 * (PILE_X_COUNT as f32 - 1.0) * SPACING;
    let origin_z = -0.5 * (PILE_Z_COUNT as f32 - 1.0) * SPACING;
    for y in 0..PILE_Y_COUNT {
        for z in 0..PILE_Z_COUNT {
            for x in 0..PILE_X_COUNT {
                let translation = Vector::new(
                    origin_x + x as f32 * SPACING,
                    INITIAL_Y + y as f32 * SPACING,
                    origin_z + z as f32 * SPACING,
                );
                let body = RigidBodyBuilder::dynamic()
                    .translation(translation)
                    .can_sleep(false)
                    .ccd_enabled(false)
                    .build();
                let body_handle = rigid_body_set.insert(body);
                let collider = ColliderBuilder::cuboid(HALF_EXTENT, HALF_EXTENT, HALF_EXTENT)
                    .friction(0.5)
                    .restitution(0.0)
                    .density(1.0)
                    .build();
                collider_set.insert_with_parent(collider, body_handle, &mut rigid_body_set);
                dynamic_handles.push(body_handle);
            }
        }
    }

    if rigid_body_set.len() != BODY_COUNT || collider_set.len() != BODY_COUNT || dynamic_handles.len() != DYNAMIC_BODY_COUNT {
        eprintln!(
            "invalid_result body_count={} shape_count={} dynamic_body_count={}",
            rigid_body_set.len(),
            collider_set.len(),
            dynamic_handles.len()
        );
        return Err(2);
    }

    Ok(RapierWorld {
        rigid_body_set,
        collider_set,
        physics_pipeline: PhysicsPipeline::new(),
        island_manager: IslandManager::new(),
        broad_phase: DefaultBroadPhase::new(),
        narrow_phase: NarrowPhase::new(),
        impulse_joint_set: ImpulseJointSet::new(),
        multibody_joint_set: MultibodyJointSet::new(),
        ccd_solver: CCDSolver::new(),
        dynamic_handles,
    })
}

pub fn add_static_box(
    rigid_body_set: &mut RigidBodySet,
    collider_set: &mut ColliderSet,
    x: f32,
    y: f32,
    z: f32,
    hx: f32,
    hy: f32,
    hz: f32,
) {
    let body = RigidBodyBuilder::fixed()
        .translation(Vector::new(x, y, z))
        .build();
    let body_handle = rigid_body_set.insert(body);
    let collider = ColliderBuilder::cuboid(hx, hy, hz)
        .friction(0.5)
        .restitution(0.0)
        .build();
    collider_set.insert_with_parent(collider, body_handle, rigid_body_set);
}

pub fn step_world(world: &mut RapierWorld, step_count: usize) {
    let gravity = Vector::new(0.0, -10.0, 0.0);
    let mut integration_parameters = IntegrationParameters::default();
    integration_parameters.dt = TIMESTEP;
    integration_parameters.num_solver_iterations = 4;
    let physics_hooks = ();
    let event_handler = ();
    for _ in 0..step_count {
        world.physics_pipeline.step(
            gravity,
            &integration_parameters,
            &mut world.island_manager,
            &mut world.broad_phase,
            &mut world.narrow_phase,
            &mut world.rigid_body_set,
            &mut world.collider_set,
            &mut world.impulse_joint_set,
            &mut world.multibody_joint_set,
            &mut world.ccd_solver,
            &physics_hooks,
            &event_handler,
        );
    }
}

pub fn sample_transforms(world: &RapierWorld, transforms: &mut [RapierTransform]) -> Result<(), i32> {
    if transforms.len() < DYNAMIC_BODY_COUNT {
        return Err(2);
    }
    for index in 0..DYNAMIC_BODY_COUNT {
        let body = &world.rigid_body_set[world.dynamic_handles[index]];
        let position = body.translation();
        let rotation = body.rotation();
        transforms[index] = RapierTransform {
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

pub fn copy_static_boxes(boxes: &mut [RapierStaticBox]) -> Result<(), i32> {
    if boxes.len() < STATIC_BODY_COUNT {
        return Err(2);
    }
    boxes[0] = RapierStaticBox { position_x: 0.0, position_y: -0.5, position_z: 0.0, half_extent_x: 14.5, half_extent_y: 0.5, half_extent_z: 14.5 };
    boxes[1] = RapierStaticBox { position_x: -14.5, position_y: 11.75, position_z: 0.0, half_extent_x: 0.5, half_extent_y: 12.25, half_extent_z: 14.5 };
    boxes[2] = RapierStaticBox { position_x: 14.5, position_y: 11.75, position_z: 0.0, half_extent_x: 0.5, half_extent_y: 12.25, half_extent_z: 14.5 };
    boxes[3] = RapierStaticBox { position_x: 0.0, position_y: 11.75, position_z: -14.5, half_extent_x: 14.5, half_extent_y: 12.25, half_extent_z: 0.5 };
    boxes[4] = RapierStaticBox { position_x: 0.0, position_y: 11.75, position_z: 14.5, half_extent_x: 14.5, half_extent_y: 12.25, half_extent_z: 0.5 };
    Ok(())
}

pub fn stability_counters(world: &RapierWorld) -> StabilityCounters {
    let mut invalid_transform_count = 0;
    let mut below_floor_count = 0;
    let mut out_of_bounds_count = 0;

    for handle in &world.dynamic_handles {
        let body = &world.rigid_body_set[*handle];
        let position = body.translation();
        let rotation = body.rotation();
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
