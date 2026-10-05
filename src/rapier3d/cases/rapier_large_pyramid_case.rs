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
    build_visual_debug_primitives,
};

pub struct RapierLargePyramidWorld
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
    pub completed_step_count: usize,
}

pub fn create_world(execution: &CaseExecutionSpec) -> Result<RapierLargePyramidWorld, i32>
{
    let shape: SharedShape = crate::case_registry::create_resolved_shape(execution.selected_geometry, execution)?;
    let rotation: Rotation = crate::case_registry::shape_rotation(execution.selected_geometry.axis);
    let fixture: crate::case_execution_wire::CaseExecutionLargePyramid = execution.large_pyramid;
    let mut rigid_body_set: RigidBodySet = RigidBodySet::with_capacity(execution.body_count as usize);
    let mut collider_set: ColliderSet = ColliderSet::with_capacity(execution.shape_count as usize);
    let mut dynamic_handles: Vec<RigidBodyHandle> = Vec::with_capacity(execution.dynamic_body_count as usize);
    for layer in 0..fixture.row_count
    {
        let layer_side: u32 = fixture.row_count - layer;
        for depth in 0..layer_side
        {
            for column in 0..layer_side
            {
                let position: Vector = Vector::new(
                    fixture.base_center.x
                        + (column as f32 - 0.5 * (layer_side - 1) as f32)
                            * fixture.box_spacing.x,
                    fixture.base_center.y + layer as f32 * fixture.box_spacing.y,
                    fixture.base_center.z
                        + (depth as f32 - 0.5 * (layer_side - 1) as f32)
                            * fixture.box_spacing.z,
                );
                let body: RigidBody = RigidBodyBuilder::dynamic()
                    .pose(Pose::from_parts(position, rotation))
                    .can_sleep(execution.sleep_mode == CaseExecutionToggle::Enabled)
                    .ccd_enabled(execution.continuous_collision_mode == CaseExecutionToggle::Enabled)
                    .build();
                let handle: RigidBodyHandle = rigid_body_set.insert(body);
                let collider: Collider = ColliderBuilder::new(shape.clone())
                .friction(execution.friction)
                .restitution(execution.restitution)
                .density(fixture.box_density)
                .build();
                collider_set.insert_with_parent(collider, handle, &mut rigid_body_set);
                dynamic_handles.push(handle);
            }
        }
    }
    for projectile_index in 0..fixture.projectile_count
    {
        let projectile: RigidBody = RigidBodyBuilder::dynamic()
            .translation(Vector::new(
                fixture.projectile_initial_center.x
                    + projectile_index as f32 * fixture.projectile_center_spacing.x,
                fixture.projectile_initial_center.y
                    + projectile_index as f32 * fixture.projectile_center_spacing.y,
                fixture.projectile_initial_center.z
                    + projectile_index as f32 * fixture.projectile_center_spacing.z,
            ))
            .can_sleep(execution.sleep_mode == CaseExecutionToggle::Enabled)
            .ccd_enabled(execution.continuous_collision_mode == CaseExecutionToggle::Enabled)
            .build();
        let projectile_handle: RigidBodyHandle = rigid_body_set.insert(projectile);
        let projectile_collider: Collider = ColliderBuilder::ball(fixture.projectile_radius)
            .friction(execution.friction)
            .restitution(execution.restitution)
            .density(fixture.projectile_density)
            .build();
        collider_set.insert_with_parent(projectile_collider, projectile_handle, &mut rigid_body_set);
        dynamic_handles.push(projectile_handle);
    }

    let floor: RigidBody = RigidBodyBuilder::fixed()
        .translation(Vector::new(0.0, -fixture.floor_half_extents.y, 0.0))
        .build();
    let floor_handle: RigidBodyHandle = rigid_body_set.insert(floor);
    let floor_collider: Collider = ColliderBuilder::cuboid(
        fixture.floor_half_extents.x,
        fixture.floor_half_extents.y,
        fixture.floor_half_extents.z,
    )
    .friction(execution.friction)
    .restitution(execution.restitution)
    .build();
    collider_set.insert_with_parent(floor_collider, floor_handle, &mut rigid_body_set);
    if rigid_body_set.len() != execution.body_count as usize
        || collider_set.len() != execution.shape_count as usize
        || dynamic_handles.len() != execution.dynamic_body_count as usize
    {
        return Err(2);
    }
    Ok(RapierLargePyramidWorld
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
        completed_step_count: 0,
    })
}

pub fn step_once(world: &mut RapierLargePyramidWorld)
{
    let execution: CaseExecutionSpec = world.execution;
    let gravity: Vector = Vector::new(execution.gravity.x, execution.gravity.y, execution.gravity.z);
    let mut parameters: IntegrationParameters = IntegrationParameters::default();
    parameters.dt = 1.0 / execution.timestep_hz as f32;
    parameters.num_solver_iterations = execution.solver_values[crate::case_execution_wire::CaseSolverField::SolverIterations as usize] as usize;
    parameters.max_ccd_substeps = match execution.continuous_collision_mode
    {
        CaseExecutionToggle::Disabled => 0,
        CaseExecutionToggle::Enabled => 1,
    };
    world.physics_pipeline.step(
        gravity,
        &parameters,
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

pub fn warmup_world(world: &mut RapierLargePyramidWorld, work_unit_count: usize)
{
    for _ in 0..work_unit_count
    {
        step_once(world);
    }
}

pub fn step_world_timed(world: &mut RapierLargePyramidWorld, durations: &mut [Duration])
{
    let fixture: crate::case_execution_wire::CaseExecutionLargePyramid = world.execution.large_pyramid;
    for duration in durations
    {
        if world.completed_step_count == fixture.projectile_launch_after_work_units as usize
        {
            let first_projectile: usize = world.dynamic_handles.len() - fixture.projectile_count as usize;
            for handle in &world.dynamic_handles[first_projectile..]
            {
                let body: &mut RigidBody = &mut world.rigid_body_set[*handle];
                body.set_linvel(
                    Vector::new(
                        fixture.projectile_launch_velocity.x,
                        fixture.projectile_launch_velocity.y,
                        fixture.projectile_launch_velocity.z,
                    ),
                    true,
                );
                body.wake_up(true);
            }
        }
        let start: Instant = Instant::now();
        step_once(world);
        *duration = start.elapsed();
        world.completed_step_count += 1;
    }
}

pub fn sample_world_transforms(
    world: &RapierLargePyramidWorld,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    if transforms.len() < world.dynamic_handles.len()
    {
        return Err(2);
    }
    for (index, handle) in world.dynamic_handles.iter().enumerate()
    {
        let body: &RigidBody = &world.rigid_body_set[*handle];
        let position: Vector = body.translation();
        let rotation: &Rotation = body.rotation();
        transforms[index] = case_registry::VisualStableTransform
        {
            stable_slot: index as u32,
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

pub fn invalid_world_transform_count(world: &RapierLargePyramidWorld) -> usize
{
    let mut count: usize = 0;
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
            count += 1;
        }
    }
    count
}

pub fn build_visual_scene(
    state: &case_registry::CaseView,
    geometries: &mut [case_registry::VisualGeometry],
    meshes: &mut case_registry::VisualMeshStorage,
    instances: &mut [case_registry::VisualInstance],
) -> Result<(usize, usize), i32>
{
    let case_registry::CaseView::LargePyramid(world) = state else
    {
        return Err(2);
    };
    let execution: CaseExecutionSpec = world.execution;
    let fixture: crate::case_execution_wire::CaseExecutionLargePyramid = execution.large_pyramid;
    if geometries.len() < 3 || instances.len() < execution.visual_instance_count as usize
    {
        return Err(2);
    }
    geometries[0] = case_registry::build_resolved_visual_geometry(&execution,
        &execution.selected_geometry, meshes)?;
    geometries[1] = case_registry::VisualGeometry
    {
        kind: 1,
        parameter_x: fixture.projectile_radius, parameter_y: 0.0, parameter_z: 0.0,
        ..case_registry::VisualGeometry::default()
    };
    geometries[2] = case_registry::VisualGeometry
    {
        kind: 2,
        parameter_x: fixture.floor_half_extents.x, parameter_y: fixture.floor_half_extents.y,
        parameter_z: fixture.floor_half_extents.z,
        ..case_registry::VisualGeometry::default()
    };
    let first_projectile: usize = world.dynamic_handles.len() - fixture.projectile_count as usize;
    for (index, handle) in world.dynamic_handles.iter().enumerate()
    {
        let body: &RigidBody = &world.rigid_body_set[*handle];
        let position: Vector = body.translation();
        let rotation: &Rotation = body.rotation();
        instances[index] = case_registry::VisualInstance
        {
            geometry_index: if index >= first_projectile
            {
                1
            }
            else
            {
                0
            },
            stable_slot: index as u32,
            transform_slot: index as u32,
            initial_transform: case_registry::VisualTransform
            {
                position_x: position.x, position_y: position.y, position_z: position.z,
                rotation_x: rotation.x, rotation_y: rotation.y,
                rotation_z: rotation.z, rotation_w: rotation.w,
            },
        };
    }
    let floor_slot: usize = world.dynamic_handles.len();
    instances[floor_slot] = case_registry::VisualInstance
    {
        geometry_index: 2,
        stable_slot: floor_slot as u32,
        transform_slot: u32::MAX,
        initial_transform: case_registry::VisualTransform
        {
            position_x: 0.0, position_y: -fixture.floor_half_extents.y, position_z: 0.0,
            rotation_x: 0.0, rotation_y: 0.0, rotation_z: 0.0, rotation_w: 1.0,
        },
    };
    Ok((3, execution.visual_instance_count as usize))
}

pub fn sample_visual_transforms(
    state: &case_registry::CaseView,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    let case_registry::CaseView::LargePyramid(world) = state else
    {
        return Err(2);
    };
    sample_world_transforms(world, transforms)
}

pub fn build_visual_debug_primitives(
    _state: &case_registry::CaseView,
    primitives: &mut [case_registry::VisualDebugPrimitive],
) -> Result<(), i32>
{
    if primitives.is_empty()
    {
        Ok(())
    }
    else
    {
        Err(2)
    }
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
    format!("solver_iterations={native_count}; sleep={sleep}; ccd={ccd}; worker_count={thread_count}")
}

pub fn run_headless(
    args: &runner_args::RunnerArgs,
    effective_thread_count: usize,
) -> Result<(), i32>
{
    let mut world: RapierLargePyramidWorld = create_world(&args.case_execution)?;
    let mut capture: Option<crate::stack_state_capture::Capture> = match args.verification_mode
    {
        runner_args::VerificationMode::On => Some(crate::stack_state_capture::open(args)?),
        runner_args::VerificationMode::Off => None,
    };
    for ordinal in 0..=args.warmup_steps
    {
        if ordinal != 0
        {
            warmup_world(&mut world, 1);
        }
        if let Some(capture) = capture.as_mut()
        {
            capture.frame_start = Instant::now();
            sample_visual_transforms(&case_registry::CaseView::LargePyramid(&mut world), &mut capture.transforms)?;
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
    let mut durations: Vec<Duration> = vec![Duration::ZERO; args.step_count];
    let mut recording: Option<crate::replay_recording::RecordingWriter> = match args.recording_mode
    {
        crate::runner_args::RecordingMode::On => Some(crate::replay_recording::begin_recording(args, &case_registry::CaseView::LargePyramid(&mut world))?),
        crate::runner_args::RecordingMode::Off => None,
    };
    for (index, duration) in durations.iter_mut().enumerate()
    {
        step_world_timed(&mut world, std::slice::from_mut(duration));
        if let Some(capture) = capture.as_mut()
        {
            capture.frame_start = Instant::now();
            sample_visual_transforms(&case_registry::CaseView::LargePyramid(&mut world), &mut capture.transforms)?;
            crate::stack_state_capture::append(capture, crate::stack_state_capture::Phase::Measured, 0, index as u32 + 1)?;
        }
        if let Some(writer) = recording.as_mut()
        {
            crate::replay_recording::append_frame(writer, args,
                &case_registry::CaseView::LargePyramid(&mut world), index as u64 + 1)?;
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
    let elapsed_ms: f64 = durations.iter().map(Duration::as_secs_f64).sum::<f64>() * 1000.0;
    let invalid_transform_count: usize = invalid_world_transform_count(&world);
    let counts_valid: bool = world.dynamic_handles.len() == args.case_execution.dynamic_body_count as usize
        && world.rigid_body_set.len() == args.case_execution.body_count as usize
        && world.collider_set.len() == args.case_execution.shape_count as usize;
    let result: result_writer::BenchmarkResult = result_writer::BenchmarkResult
    {
        physics_settings: visual_physics_settings(&args.case_execution, effective_thread_count),
        body_count: world.rigid_body_set.len(),
        shape_count: world.collider_set.len(),
        query_count: 0,
        constraint_count: 0,
        invalid_transform_count,
        effective_thread_count,
        effective_worker_count: effective_thread_count,
        completed_work_unit_count: world.completed_step_count,
        workload_elapsed_ms: elapsed_ms,
        case_validity: if invalid_transform_count == 0 && counts_valid
        {
            result_writer::ResultValidity::Valid
        }
        else
        {
            result_writer::ResultValidity::Invalid
        },
        metric_validity: if counts_valid && elapsed_ms > 0.0 && elapsed_ms.is_finite()
            && world.completed_step_count == args.step_count
        {
            result_writer::ResultValidity::Valid
        }
            else
        {
            result_writer::ResultValidity::Invalid
        },
        observations: Vec::new(),
    };
    result_writer::write_result(args, &result)?;
    result_writer::write_step_timing(args, &durations)
}
