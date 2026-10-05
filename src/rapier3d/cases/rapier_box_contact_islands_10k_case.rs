use crate::case_execution_wire::{CaseExecutionSpec, CaseExecutionToggle};
use crate::{case_registry, result_writer, runner_args};
use rapier3d::prelude::*;
use std::time::{Duration, Instant};

pub const DESCRIPTOR: case_registry::RapierCaseDescriptor = case_registry::RapierCaseDescriptor
{
    engine_id: case_registry::ENGINE_ID,
};

pub const REGISTRATION: case_registry::CaseRegistration = case_registry::CaseRegistration
{
    descriptor: DESCRIPTOR,
    run_headless,
    build_visual_scene,
    sample_visual_transforms,
    build_visual_debug_primitives:
        crate::rapier_box_container_pile_10k_case::build_no_visual_debug_primitives,
};

pub fn sample_visual_transforms(
    state: &case_registry::CaseView,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    let case_registry::CaseView::ContactIslands(world) = state else
    {
        return Err(2);
    };
    sample_transforms(world, transforms)
}

pub struct RapierContactIslandsWorld
{
    pub execution: CaseExecutionSpec,
    pub rigid_body_set: RigidBodySet,
    pub collider_set: ColliderSet,
    pub physics_pipeline: PhysicsPipeline,
    pub island_manager: IslandManager,
    pub broad_phase: DefaultBroadPhase,
    pub narrow_phase: NarrowPhase,
    pub impulse_joint_set: ImpulseJointSet,
    pub multibody_joint_set: MultibodyJointSet,
    pub soft_body_set: SoftBodySet,
    pub ccd_solver: CCDSolver,
    pub dynamic_handles: Vec<RigidBodyHandle>,
}

pub fn island_origin(spacing: f32, coordinate: u32, count: u32) -> f32
{
    spacing * (coordinate as f32 - 0.5 * (count as f32 - 1.0))
}

pub fn create_world(execution: &CaseExecutionSpec) -> Result<RapierContactIslandsWorld, i32>
{
    let shape: SharedShape = crate::case_registry::create_resolved_shape(execution.selected_geometry, execution)?;
    let rotation: Rotation = crate::case_registry::shape_rotation(execution.selected_geometry.axis);
    let fixture: crate::case_execution_wire::CaseExecutionContactIslands = execution.contact_islands;
    let mut rigid_body_set: RigidBodySet = RigidBodySet::with_capacity(execution.body_count as usize);
    let mut collider_set: ColliderSet = ColliderSet::with_capacity(execution.shape_count as usize);
    let mut dynamic_handles: Vec<RigidBodyHandle> = Vec::with_capacity(execution.dynamic_body_count as usize);

    for group_z in 0..fixture.island_grid[1]
    {
        for group_x in 0..fixture.island_grid[0]
        {
            add_static_box(
                &mut rigid_body_set,
                &mut collider_set,
                island_origin(fixture.island_spacing[0], group_x, fixture.island_grid[0]),
                -fixture.floor_half_extents.y,
                island_origin(fixture.island_spacing[1], group_z, fixture.island_grid[1]),
                fixture.floor_half_extents.x,
                fixture.floor_half_extents.y,
                fixture.floor_half_extents.z,
                execution,
            );
        }
    }
    for group_z in 0..fixture.island_grid[1]
    {
        for group_x in 0..fixture.island_grid[0]
        {
            let origin_x: f32 = island_origin(fixture.island_spacing[0], group_x, fixture.island_grid[0]);
            let origin_z: f32 = island_origin(fixture.island_spacing[1], group_z, fixture.island_grid[1]);
            for y in 0..fixture.body_grid[1]
            {
                for z in 0..fixture.body_grid[2]
                {
                    for x in 0..fixture.body_grid[0]
                    {
                        let body: RigidBody = RigidBodyBuilder::dynamic()
                            .pose(Pose::from_parts(Vector::new(
                                origin_x + (x as f32 - 0.5 * (fixture.body_grid[0] as f32 - 1.0)) * fixture.body_spacing.x,
                                fixture.body_initial_y + y as f32 * fixture.body_spacing.y,
                                origin_z + (z as f32 - 0.5 * (fixture.body_grid[2] as f32 - 1.0)) * fixture.body_spacing.z,
                            ), rotation))
                            .can_sleep(execution.sleep_mode == CaseExecutionToggle::Enabled)
                            .ccd_enabled(execution.continuous_collision_mode == CaseExecutionToggle::Enabled)
                            .build();
                        let body_handle: RigidBodyHandle = rigid_body_set.insert(body);
                        let collider: Collider = ColliderBuilder::new(shape.clone())
                            .friction(execution.friction)
                            .restitution(execution.restitution)
                            .density(fixture.density)
                            .build();
                        collider_set.insert_with_parent(collider, body_handle, &mut rigid_body_set);
                        dynamic_handles.push(body_handle);
                    }
                }
            }
        }
    }
    if rigid_body_set.len() != execution.body_count as usize
        || collider_set.len() != execution.shape_count as usize
        || dynamic_handles.len() != execution.dynamic_body_count as usize
    {
        eprintln!(
            "invalid_result body_count={} shape_count={} dynamic_body_count={}",
            rigid_body_set.len(),
            collider_set.len(),
            dynamic_handles.len()
        );
        return Err(2);
    }
    Ok(RapierContactIslandsWorld
    {
        execution: *execution,
        rigid_body_set,
        collider_set,
        physics_pipeline: PhysicsPipeline::new(),
        island_manager: IslandManager::new(),
        broad_phase: DefaultBroadPhase::new(),
        narrow_phase: NarrowPhase::new(),
        impulse_joint_set: ImpulseJointSet::new(),
        multibody_joint_set: MultibodyJointSet::new(),
        soft_body_set: SoftBodySet::new(),
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
    half_extent_x: f32,
    half_extent_y: f32,
    half_extent_z: f32,
    execution: &CaseExecutionSpec,
)
{
    let body_handle: RigidBodyHandle = rigid_body_set.insert(
        RigidBodyBuilder::fixed()
            .translation(Vector::new(x, y, z))
            .build(),
    );
    let collider: Collider = ColliderBuilder::cuboid(half_extent_x, half_extent_y, half_extent_z)
        .friction(execution.friction)
        .restitution(execution.restitution)
        .build();
    collider_set.insert_with_parent(collider, body_handle, rigid_body_set);
}

pub fn step_once(world: &mut RapierContactIslandsWorld)
{
    let execution: CaseExecutionSpec = world.execution;
    let gravity: Vector = Vector::new(execution.gravity.x, execution.gravity.y, execution.gravity.z);
    let mut integration_parameters: IntegrationParameters = IntegrationParameters::default();
    integration_parameters.dt = 1.0 / execution.timestep_hz as f32;
    integration_parameters.num_solver_iterations = execution.solver_values[crate::case_execution_wire::CaseSolverField::SolverIterations as usize] as usize;
    integration_parameters.max_ccd_substeps = match execution.continuous_collision_mode
    {
        CaseExecutionToggle::Disabled => 0,
        CaseExecutionToggle::Enabled => 1,
    };
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
        &mut world.soft_body_set,
        &mut world.ccd_solver,
        &(),
        &(),
    );
}

pub fn step_world(world: &mut RapierContactIslandsWorld, work_unit_count: usize)
{
    for _ in 0..work_unit_count
    {
        step_once(world);
    }
}

pub fn step_world_timed(world: &mut RapierContactIslandsWorld, durations: &mut [Duration])
{
    for duration in durations
    {
        let start: Instant = Instant::now();
        step_once(world);
        *duration = start.elapsed();
    }
}

pub fn sample_transforms(
    world: &RapierContactIslandsWorld,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    if transforms.len() < world.dynamic_handles.len()
    {
        return Err(2);
    }
    for (slot, handle) in world.dynamic_handles.iter().enumerate()
    {
        let body: &RigidBody = &world.rigid_body_set[*handle];
        let position: Vector = body.translation();
        let rotation: &Rotation = body.rotation();
        transforms[slot] = case_registry::VisualStableTransform
        {
            stable_slot: slot as u32,
            transform: case_registry::VisualTransform
            {
                position_x: position.x,
                position_y: position.y,
                position_z: position.z,
                rotation_x: rotation.x,
                rotation_y: rotation.y,
                rotation_z: rotation.z,
                rotation_w: rotation.w,
            },
        };
    }
    Ok(())
}

pub fn build_visual_scene(
    state: &case_registry::CaseView,
    geometries: &mut [case_registry::VisualGeometry],
    meshes: &mut case_registry::VisualMeshStorage,
    instances: &mut [case_registry::VisualInstance],
) -> Result<(usize, usize), i32>
{
    let case_registry::CaseView::ContactIslands(world) = state else
    {
        return Err(2);
    };
    let execution: CaseExecutionSpec = world.execution;
    let fixture: crate::case_execution_wire::CaseExecutionContactIslands = execution.contact_islands;
    if geometries.len() < 2 || instances.len() < execution.body_count as usize
    {
        return Err(2);
    }
    geometries[0] = case_registry::build_resolved_visual_geometry(&execution,
        &execution.selected_geometry, meshes)?;
    geometries[1] = case_registry::VisualGeometry
    {
        kind: 2, parameter_x: fixture.floor_half_extents.x,
        parameter_y: fixture.floor_half_extents.y,
        parameter_z: fixture.floor_half_extents.z,
        ..case_registry::VisualGeometry::default()
    };
    for (slot, handle) in world.dynamic_handles.iter().enumerate()
    {
        let body: &RigidBody = &world.rigid_body_set[*handle];
        let position: Vector = body.translation();
        let rotation: &Rotation = body.rotation();
        instances[slot] = case_registry::VisualInstance
        {
            geometry_index: 0,
            stable_slot: slot as u32,
            transform_slot: slot as u32,
            initial_transform: case_registry::VisualTransform
            {
                position_x: position.x, position_y: position.y, position_z: position.z,
                rotation_x: rotation.x, rotation_y: rotation.y,
                rotation_z: rotation.z, rotation_w: rotation.w,
            },
        };
    }
    let mut index: usize = 0;
    for group_z in 0..fixture.island_grid[1]
    {
        for group_x in 0..fixture.island_grid[0]
        {
            let stable_slot: usize = world.dynamic_handles.len() + index;
            instances[stable_slot] = case_registry::VisualInstance
            {
                geometry_index: 1,
                stable_slot: stable_slot as u32,
                transform_slot: u32::MAX,
                initial_transform: case_registry::VisualTransform
                {
                    position_x: island_origin(fixture.island_spacing[0], group_x, fixture.island_grid[0]),
                    position_y: -fixture.floor_half_extents.y,
                    position_z: island_origin(fixture.island_spacing[1], group_z, fixture.island_grid[1]),
                    rotation_x: 0.0, rotation_y: 0.0, rotation_z: 0.0, rotation_w: 1.0,
                },
            };
            index += 1;
        }
    }
    Ok((2, execution.body_count as usize))
}

pub fn invalid_transform_count(world: &RapierContactIslandsWorld) -> usize
{
    let mut invalid_transform_count: usize = 0;
    for handle in &world.dynamic_handles
    {
        let body: &RigidBody = &world.rigid_body_set[*handle];
        let position: Vector = body.translation();
        let rotation: &Rotation = body.rotation();
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
    }
    invalid_transform_count
}

pub fn visual_physics_settings(execution: &CaseExecutionSpec, thread_count: usize) -> String
{
    let native_count = execution.solver_values[crate::case_execution_wire::CaseSolverField::SolverIterations as usize];
    let sleep = if execution.sleep_mode == CaseExecutionToggle::Enabled
    {
        "enabled"
    }
    else
    {
        "disabled"
    };
    let ccd = if execution.continuous_collision_mode == CaseExecutionToggle::Enabled
    {
        "enabled"
    }
    else
    {
        "disabled"
    };
    let fixture: crate::case_execution_wire::CaseExecutionContactIslands = execution.contact_islands;
    let island_count: u32 = fixture.island_grid[0] * fixture.island_grid[1];
    format!(
        "solver_iterations={native_count}; sleep={sleep}; ccd={ccd}; worker_count={thread_count}; islands={island_count}"
    )
}

pub fn run_headless(
    args: &runner_args::RunnerArgs,
    effective_thread_count: usize,
) -> Result<(), i32>
{
    let mut world: RapierContactIslandsWorld = create_world(&args.case_execution)?;
    let mut capture: Option<crate::stack_state_capture::Capture> = match args.verification_mode
    {
        runner_args::VerificationMode::On => Some(crate::stack_state_capture::open(args)?),
        runner_args::VerificationMode::Off => None,
    };
    for ordinal in 0..=args.warmup_steps
    {
        if ordinal != 0
        {
            step_world(&mut world, 1);
        }
        if let Some(capture) = capture.as_mut()
        {
            capture.frame_start = Instant::now();
            sample_visual_transforms(&case_registry::CaseView::ContactIslands(&mut world), &mut capture.transforms)?;
            crate::stack_state_capture::append(capture, if ordinal == 0
            {
                crate::stack_state_capture::Phase::Construction
            }
            else
            {
                crate::stack_state_capture::Phase::Warmup
            }, 0, ordinal as u32)?;
        }
    }
    let mut work_unit_durations: Vec<Duration> = vec![Duration::ZERO; args.step_count];
    let mut recording: Option<crate::replay_recording::RecordingWriter> = match args.recording_mode
    {
        crate::runner_args::RecordingMode::On => Some(crate::replay_recording::begin_recording(args, &case_registry::CaseView::ContactIslands(&mut world))?),
        crate::runner_args::RecordingMode::Off => None,
    };
    for (index, duration) in work_unit_durations.iter_mut().enumerate()
    {
        step_world_timed(&mut world, std::slice::from_mut(duration));
        if let Some(capture) = capture.as_mut()
        {
            capture.frame_start = Instant::now();
            sample_visual_transforms(&case_registry::CaseView::ContactIslands(&mut world), &mut capture.transforms)?;
            crate::stack_state_capture::append(capture, crate::stack_state_capture::Phase::Measured, 0, index as u32 + 1)?;
        }
        if let Some(writer) = recording.as_mut()
        {
            crate::replay_recording::append_frame(writer, args,
                &case_registry::CaseView::ContactIslands(&mut world), index as u64 + 1)?;
        }
    }
    if let Some(capture) = capture
    {
        crate::stack_state_capture::close(capture)?;
    }
    if let Some(writer) = recording
    {
        crate::replay_recording::complete_recording(writer)?;
    }
    let workload_elapsed_ms: f64 = work_unit_durations
        .iter()
        .map(Duration::as_secs_f64)
        .sum::<f64>()
        * 1000.0;
    let invalid_transform_count: usize = invalid_transform_count(&world);
    let case_validity: result_writer::ResultValidity = if world.dynamic_handles.len() == args.case_execution.dynamic_body_count as usize
        && world.rigid_body_set.len() == args.case_execution.body_count as usize
        && world.collider_set.len() == args.case_execution.shape_count as usize
        && invalid_transform_count == 0
    {
        result_writer::ResultValidity::Valid
    }
    else
    {
        result_writer::ResultValidity::Invalid
    };
    let metric_validity: result_writer::ResultValidity = if args.step_count == args.case_execution.measured_work_unit_count as usize
        && workload_elapsed_ms > 0.0
        && workload_elapsed_ms.is_finite()
    {
        result_writer::ResultValidity::Valid
    }
    else
    {
        result_writer::ResultValidity::Invalid
    };
    let result: result_writer::BenchmarkResult = result_writer::BenchmarkResult
    {
        physics_settings: visual_physics_settings(&args.case_execution, effective_thread_count),
        body_count: world.rigid_body_set.len(),
        shape_count: world.collider_set.len(),
        query_count: args.case_execution.query_count as usize,
        constraint_count: args.case_execution.constraint_count as usize,
        invalid_transform_count,
        effective_thread_count,
        effective_worker_count: effective_thread_count,
        completed_work_unit_count: args.step_count,
        workload_elapsed_ms,
        case_validity,
        metric_validity,
        observations: Vec::new(),
    };
    result_writer::write_result(args, &result)?;
    result_writer::write_step_timing(args, &work_unit_durations)
}
