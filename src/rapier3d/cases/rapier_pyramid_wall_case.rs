use crate::case_execution_wire::{CaseExecutionSpec, CaseExecutionToggle};
use crate::{case_registry, result_writer, runner_args, pyramid_wall};
use crate::pyramid_wall::PyramidWallObservation;
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

#[derive(Clone, Copy, Default)]
pub struct RapierWallBodyInput
{
    pub position: Vector,
    pub rotation: Rotation,
    pub linear: Vector,
    pub angular: Vector,
    pub inertia_frame: Rotation,
    pub inertia: Vector,
    pub mass: f32,
    pub sleep_threshold: f32,
    pub sleep_flags: u32,
}

pub struct RapierPyramidWallWorld
{
    pub execution: CaseExecutionSpec,
    pub verification_mode: runner_args::VerificationMode,
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
    pub observations: [PyramidWallObservation; 4],
    pub observation_inputs: [Vec<RapierWallBodyInput>; 4],
    pub initial_potential_energy: f64,
}

pub fn create_world(execution: &CaseExecutionSpec, verification_mode: runner_args::VerificationMode) -> Result<RapierPyramidWallWorld, i32>
{
    let shape: SharedShape = crate::case_registry::create_resolved_shape(execution.selected_geometry, execution)?;
    let fixture: crate::case_execution_wire::CaseExecutionPyramidWall = execution.pyramid_wall;
    let mut rigid_body_set: RigidBodySet = RigidBodySet::with_capacity(execution.body_count as usize);
    let mut collider_set: ColliderSet = ColliderSet::with_capacity(execution.shape_count as usize);
    let mut dynamic_handles: Vec<RigidBodyHandle> = Vec::with_capacity(execution.dynamic_body_count as usize);
    let mut initial_potential_energy: f64 = 0.0;
    let expected_mass: f32 = 8.0 * fixture.half_extent.powi(3) * fixture.density;
    let expected_inertia: f32 = (2.0 / 3.0) * expected_mass * fixture.half_extent.powi(2);
    for index in 0..execution.dynamic_body_count
    {
        let p: crate::case_execution_wire::CaseExecutionVector3 = pyramid_wall::position(&fixture, index);
        let body: RigidBody = RigidBodyBuilder::dynamic()
            .translation(Vector::new(p.x, p.y, p.z))
            .linear_damping(0.0).angular_damping(0.0)
            .can_sleep(execution.sleep_mode == CaseExecutionToggle::Enabled)
            .ccd_enabled(execution.continuous_collision_mode == CaseExecutionToggle::Enabled).build();
        let handle: RigidBodyHandle = rigid_body_set.insert(body);
        let collider: Collider = ColliderBuilder::new(shape.clone())
            .friction(execution.friction).restitution(execution.restitution)
            .friction_combine_rule(CoefficientCombineRule::Average)
            .restitution_combine_rule(CoefficientCombineRule::Average)
            .density(fixture.density).build();
        let collider_handle: ColliderHandle = collider_set.insert_with_parent(collider, handle, &mut rigid_body_set);
        let body: &mut RigidBody = &mut rigid_body_set[handle];
        body.recompute_mass_properties_from_colliders(&collider_set);
        let inertia: Vector = body.mass_properties().local_mprops.principal_inertia();
        if body.translation() != Vector::new(p.x, p.y, p.z) || *body.rotation() != Rotation::IDENTITY ||
            body.linvel().length_squared() != 0.0 || body.angvel().length_squared() != 0.0 ||
            !body.mass().is_finite() || body.mass() <= 0.0 || !inertia.is_finite() ||
            (body.mass() - expected_mass).abs() > 1e-5 * expected_mass ||
            (inertia - Vector::splat(expected_inertia)).abs().max_element() > 1e-5 * expected_inertia ||
            body.linear_damping() != 0.0 || body.angular_damping() != 0.0 || body.is_ccd_enabled() != (execution.continuous_collision_mode == CaseExecutionToggle::Enabled) || body.is_sleeping() ||
            (body.activation().normalized_linear_threshold >= 0.0) != (execution.sleep_mode == CaseExecutionToggle::Enabled) ||
            collider_set[collider_handle].friction() != execution.friction ||
            collider_set[collider_handle].restitution() != execution.restitution
        {
            return Err(2);
        }
        let native: Vector = body.translation();
        if verification_mode == runner_args::VerificationMode::On
        {
            initial_potential_energy -= body.mass() as f64 * (execution.gravity.x as f64 * native.x as f64 +
                execution.gravity.y as f64 * native.y as f64 + execution.gravity.z as f64 * native.z as f64);
        }
        dynamic_handles.push(handle);
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
    .friction_combine_rule(CoefficientCombineRule::Average)
    .restitution_combine_rule(CoefficientCombineRule::Average)
    .build();
    let floor_collider_handle: ColliderHandle = collider_set.insert_with_parent(floor_collider, floor_handle, &mut rigid_body_set);
    let native_floor: &RigidBody = &rigid_body_set[floor_handle];
    let native_floor_collider: &Collider = &collider_set[floor_collider_handle];
    if !native_floor.is_fixed() || native_floor.translation() != Vector::new(0.0, -fixture.floor_half_extents.y, 0.0) ||
        *native_floor.rotation() != Rotation::IDENTITY || native_floor_collider.friction() != execution.friction ||
        native_floor_collider.restitution() != execution.restitution
    {
        return Err(2);
    }
    if rigid_body_set.len() != execution.body_count as usize
        || collider_set.len() != execution.shape_count as usize
        || dynamic_handles.len() != execution.dynamic_body_count as usize
    {
        return Err(2);
    }
    Ok(RapierPyramidWallWorld
    {
        execution: *execution,
        verification_mode,
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
        observations: [PyramidWallObservation::default(); 4],
        observation_inputs: std::array::from_fn(|ordinal| vec![RapierWallBodyInput::default(); if verification_mode == runner_args::VerificationMode::On && ordinal < 4.min(execution.measured_work_unit_count) as usize { execution.dynamic_body_count as usize } else { 0 }]),
        initial_potential_energy,
    })
}

pub fn step_once(world: &mut RapierPyramidWallWorld)
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

pub fn warmup_world(world: &mut RapierPyramidWallWorld, work_unit_count: usize)
{
    for _ in 0..work_unit_count
    {
        step_once(world);
    }
}

pub fn step_world_timed(world: &mut RapierPyramidWallWorld, durations: &mut [Duration])
{
    for duration in durations
    {
        let start: Instant = Instant::now();
        step_once(world);
        *duration = start.elapsed();
        world.completed_step_count += 1;
        if world.verification_mode == runner_args::VerificationMode::On
        {
            if let Some(ordinal) = pyramid_wall::observation_index(world.completed_step_count as u32, world.execution.measured_work_unit_count)
            {
                capture_observation(world, ordinal);
            }
        }
    }
}

pub fn sample_world_transforms(
    world: &RapierPyramidWallWorld,
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

pub fn capture_observation(world: &mut RapierPyramidWallWorld, ordinal: usize)
{
    let start: Instant = Instant::now();
    for (index, handle) in world.dynamic_handles.iter().enumerate()
    {
        let body: &RigidBody = &world.rigid_body_set[*handle];
        world.observation_inputs[ordinal][index] = RapierWallBodyInput
        {
            position: body.translation(),
            rotation: *body.rotation(),
            linear: body.linvel(),
            angular: body.angvel(),
            inertia_frame: body.mass_properties().local_mprops.principal_inertia_local_frame,
            inertia: body.mass_properties().local_mprops.principal_inertia(),
            mass: body.mass(),
            sleep_threshold: body.activation().normalized_linear_threshold,
            sleep_flags: body.is_sleeping() as u32,
        };
    }
    world.observations[ordinal].elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;
}

pub fn reduce_observation(world: &RapierPyramidWallWorld, ordinal: usize) -> PyramidWallObservation
{
    let start: Instant = Instant::now();
    let mut sample: PyramidWallObservation = PyramidWallObservation::default();
    for (index, input) in world.observation_inputs[ordinal].iter().enumerate()
    {
        let p: Vector = input.position;
        let q: &Rotation = &input.rotation;
        let linear: Vector = input.linear;
        let angular: Vector = input.angular;
        let axes: Rotation = *q * input.inertia_frame;
        let local: Vector = axes.inverse() * angular;
        let inertia: Vector = input.inertia;
        let energy: f64 = 0.5 * (local.x as f64 * local.x as f64 * inertia.x as f64 +
            local.y as f64 * local.y as f64 * inertia.y as f64 + local.z as f64 * local.z as f64 * inertia.z as f64);
        let sleep_matches: i32 = ((input.sleep_threshold >= 0.0) ==
            (world.execution.sleep_mode == CaseExecutionToggle::Enabled) &&
            (world.execution.sleep_mode == CaseExecutionToggle::Enabled || input.sleep_flags == 0)) as i32;
        pyramid_wall::accumulate(&world.execution.pyramid_wall, index as u32, p.to_array(), q.to_array(),
            linear.to_array(), angular.to_array(), input.mass as f64, energy, sleep_matches, world.execution.gravity, &mut sample);
    }
    pyramid_wall::finish(world.execution.dynamic_body_count, &mut sample);
    sample.elapsed_ms = world.observations[ordinal].elapsed_ms + start.elapsed().as_secs_f64() * 1000.0;
    sample
}

pub fn build_visual_scene(
    state: &case_registry::CaseView,
    geometries: &mut [case_registry::VisualGeometry],
    meshes: &mut case_registry::VisualMeshStorage,
    instances: &mut [case_registry::VisualInstance],
) -> Result<(usize, usize), i32>
{
    let case_registry::CaseView::PyramidWall(world) = state else
    {
        return Err(2);
    };
    let execution: CaseExecutionSpec = world.execution;
    let fixture: crate::case_execution_wire::CaseExecutionPyramidWall = execution.pyramid_wall;
    if geometries.len() < 2 || instances.len() < execution.visual_instance_count as usize
    {
        return Err(2);
    }
    geometries[0] = case_registry::build_resolved_visual_geometry(&execution,
        &execution.selected_geometry, meshes)?;
    geometries[1] = case_registry::VisualGeometry
    {
        kind: 2,
        parameter_x: fixture.floor_half_extents.x, parameter_y: fixture.floor_half_extents.y,
        parameter_z: fixture.floor_half_extents.z,
        ..case_registry::VisualGeometry::default()
    };
    for (index, handle) in world.dynamic_handles.iter().enumerate()
    {
        let body: &RigidBody = &world.rigid_body_set[*handle];
        let position: Vector = body.translation();
        let rotation: &Rotation = body.rotation();
        instances[index] = case_registry::VisualInstance
        {
            geometry_index: 0,
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
        geometry_index: 1,
        stable_slot: floor_slot as u32,
        transform_slot: u32::MAX,
        initial_transform: case_registry::VisualTransform
        {
            position_x: 0.0, position_y: -fixture.floor_half_extents.y, position_z: 0.0,
            rotation_x: 0.0, rotation_y: 0.0, rotation_z: 0.0, rotation_w: 1.0,
        },
    };
    Ok((2, execution.visual_instance_count as usize))
}

pub fn sample_visual_transforms(
    state: &case_registry::CaseView,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    let case_registry::CaseView::PyramidWall(world) = state else
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
    let native_count: u32 = execution.solver_values[crate::case_execution_wire::CaseSolverField::SolverIterations as usize];
    let sleep: &str = if execution.sleep_mode == CaseExecutionToggle::Enabled
    {
        "enabled"
    }
    else
    {
        "disabled"
    };
    let ccd: &str = if execution.continuous_collision_mode == CaseExecutionToggle::Enabled
    {
        "enabled"
    }
    else
    {
        "disabled"
    };
    format!("solver_iterations={native_count}; sleep={sleep}; ccd={ccd}; linear_damping=0; angular_damping=0; worker_count={thread_count}")
}

pub fn run_headless(
    args: &runner_args::RunnerArgs,
    effective_thread_count: usize,
) -> Result<(), i32>
{
    let mut world: RapierPyramidWallWorld = create_world(&args.case_execution, args.verification_mode)?;
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
            sample_visual_transforms(&case_registry::CaseView::PyramidWall(&mut world), &mut capture.transforms)?;
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
        crate::runner_args::RecordingMode::On => Some(crate::replay_recording::begin_recording(args, &case_registry::CaseView::PyramidWall(&mut world))?),
        crate::runner_args::RecordingMode::Off => None,
    };
    for (index, duration) in durations.iter_mut().enumerate()
    {
        step_world_timed(&mut world, std::slice::from_mut(duration));
        if let Some(capture) = capture.as_mut()
        {
            capture.frame_start = Instant::now();
            sample_visual_transforms(&case_registry::CaseView::PyramidWall(&mut world), &mut capture.transforms)?;
            crate::stack_state_capture::append(capture, crate::stack_state_capture::Phase::Measured, 0, index as u32 + 1)?;
        }
        if let Some(writer) = recording.as_mut()
        {
            crate::replay_recording::append_frame(writer, args,
                &case_registry::CaseView::PyramidWall(&mut world), index as u64 + 1)?;
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
    let sample_count: usize = match args.verification_mode
    {
        runner_args::VerificationMode::On => 4.min(args.case_execution.measured_work_unit_count) as usize,
        runner_args::VerificationMode::Off => 0,
    };
    for ordinal in 0..sample_count
    {
        world.observations[ordinal] = reduce_observation(&world, ordinal);
    }
    let elapsed_ms: f64 = durations.iter().map(Duration::as_secs_f64).sum::<f64>() * 1000.0;
    let invalid_transform_count: usize = match args.verification_mode
    {
        runner_args::VerificationMode::On => world.observations.iter().map(|sample| sample.invalid_bodies as usize).sum(),
        runner_args::VerificationMode::Off => world.rigid_body_set.iter().filter(|(_, body): &(RigidBodyHandle, &RigidBody)|
        {
            !body.translation().is_finite() || !body.rotation().is_finite()
        }).count(),
    };
    let counts_valid: i32 = ( world.dynamic_handles.len() == args.case_execution.dynamic_body_count as usize
        && world.rigid_body_set.len() == args.case_execution.body_count as usize
        && world.collider_set.len() == args.case_execution.shape_count as usize) as i32;
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
        case_validity: if invalid_transform_count == 0 && counts_valid != 0
        {
            result_writer::ResultValidity::Valid
        }
        else
        {
            result_writer::ResultValidity::Invalid
        },
        metric_validity: if counts_valid != 0 && elapsed_ms > 0.0 && elapsed_ms.is_finite()
            && world.completed_step_count == args.step_count
        {
            result_writer::ResultValidity::Valid
        }
            else
        {
            result_writer::ResultValidity::Invalid
        },
        observations: match args.verification_mode
        {
            runner_args::VerificationMode::On => pyramid_wall::observation_rows(args.case_execution.measured_work_unit_count, &world.observations, world.initial_potential_energy),
            runner_args::VerificationMode::Off => Vec::new(),
        },
    };
    result_writer::write_result(args, &result)?;
    result_writer::write_step_timing(args, &durations)
}
