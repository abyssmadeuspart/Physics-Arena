use crate::case_execution_wire::{CaseExecutionSpec, CaseExecutionToggle};
use crate::{case_registry, result_writer, runner_args, pyramid_wall};
use crate::pyramid_wall::PyramidWallObservation;
use avian3d::{math::RVector, prelude::*};
use bevy::{
    MinimalPlugins,
    app::{App, PluginsState},
    ecs::entity::Entity,
    prelude::Transform,
    time::{Time, TimeUpdateStrategy},
    transform::TransformPlugin,
};
use std::time::{Duration, Instant};

pub const DESCRIPTOR: case_registry::AvianCaseDescriptor = case_registry::AvianCaseDescriptor
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
pub struct AvianWallBodyInput
{
    pub position: RVector,
    pub rotation: bevy::math::Quat,
    pub linear: RVector,
    pub angular: bevy::math::Vec3,
    pub inertia: ComputedAngularInertia,
    pub mass: f32,
    pub sleep_flags: u32,
}

pub struct AvianPyramidWallWorld
{
    pub execution: CaseExecutionSpec,
    pub verification_mode: runner_args::VerificationMode,
    pub app: App,
    pub dynamic_entities: Vec<Entity>,
    pub body_count: usize,
    pub shape_count: usize,
    pub completed_step_count: usize,
    pub observations: [PyramidWallObservation; 4],
    pub observation_inputs: [Vec<AvianWallBodyInput>; 4],
    pub initial_potential_energy: f64,
}

pub fn create_world(execution: &CaseExecutionSpec, verification_mode: runner_args::VerificationMode) -> Result<AvianPyramidWallWorld, i32>
{
    let fixture: crate::case_execution_wire::CaseExecutionPyramidWall = execution.pyramid_wall;
    let timestep: f64 = 1.0 / execution.timestep_hz as f64;
    let mut app: App = App::new();
    app.add_plugins((
        MinimalPlugins,
        TransformPlugin,
        PhysicsPlugins::new(case_registry::AvianBenchmarkSchedule),
    ))
    .insert_resource(Gravity(RVector::new(execution.gravity.x, execution.gravity.y, execution.gravity.z)))
    .insert_resource(SubstepCount(execution.solver_values[crate::case_execution_wire::CaseSolverField::Substeps as usize]))
    .insert_resource(Time::from_hz(execution.timestep_hz as f64))
    .insert_resource(TimeUpdateStrategy::ManualDuration(Duration::from_secs_f64(timestep)));
    while app.plugins_state() != PluginsState::Ready
    {
        bevy::tasks::tick_global_task_pools_on_main_thread();
    }
    app.finish();
    app.cleanup();
    let mut dynamic_entities: Vec<bevy::ecs::entity::Entity> = Vec::with_capacity(execution.dynamic_body_count as usize);
    let shape: Collider = case_registry::create_resolved_shape(&execution.selected_geometry, execution)?;
    for index in 0..execution.dynamic_body_count
    {
        let p: crate::case_execution_wire::CaseExecutionVector3 = pyramid_wall::position(&fixture, index);
        let mut entity: bevy::ecs::world::EntityWorldMut<'_> = app.world_mut().spawn((
            RigidBody::Dynamic, shape.clone(), ColliderDensity(fixture.density),
            Friction::new(execution.friction).with_combine_rule(CoefficientCombine::Average),
            Restitution::new(execution.restitution).with_combine_rule(CoefficientCombine::Average),
            Position::from_xyz(p.x, p.y, p.z), Rotation::IDENTITY, Transform::from_xyz(p.x, p.y, p.z),
            LinearVelocity(RVector::ZERO), AngularVelocity(bevy::math::Vec3::ZERO),
            LinearDamping(0.0), AngularDamping(0.0),
        ));
        if execution.sleep_mode == CaseExecutionToggle::Disabled
        {
            entity.insert(SleepingDisabled);
        }
        entity.insert(SweptCcd::default().with_filter(
            if execution.continuous_collision_mode == CaseExecutionToggle::Enabled
            {
                CcdFilter::DEFAULT
            }
            else
            {
                CcdFilter::NONE
            },
        ));
        dynamic_entities.push(entity.id());
    }
    let floor: Entity = app.world_mut().spawn((
        RigidBody::Static,
        Collider::cuboid(
            fixture.floor_half_extents.x * 2.0,
            fixture.floor_half_extents.y * 2.0,
            fixture.floor_half_extents.z * 2.0,
        ),
        Friction::new(execution.friction).with_combine_rule(CoefficientCombine::Average),
        Restitution::new(execution.restitution).with_combine_rule(CoefficientCombine::Average),
        Position::from_xyz(0.0, -fixture.floor_half_extents.y, 0.0),
        Rotation::IDENTITY,
        Transform::from_xyz(0.0, -fixture.floor_half_extents.y, 0.0),
    )).id();
    app.world_mut().flush();
    let mut mass_state: bevy::ecs::system::SystemState<MassPropertyHelper> = bevy::ecs::system::SystemState::new(app.world_mut());
    {
        let mut helper: MassPropertyHelper = mass_state.get_mut(app.world_mut()).map_err(|_| 2)?;
        for &entity in &dynamic_entities
        {
            helper.update_mass_properties(entity);
        }
    }
    mass_state.apply(app.world_mut());
    let mut initial_potential_energy: f64 = 0.0;
    let expected_mass: f32 = 8.0 * fixture.half_extent.powi(3) * fixture.density;
    let expected_inertia: f32 = (2.0 / 3.0) * expected_mass * fixture.half_extent.powi(2);
    for (index, &id) in dynamic_entities.iter().enumerate()
    {
        let entity: bevy::ecs::world::EntityRef<'_> = app.world().entity(id);
        let p: RVector = entity.get::<Position>().ok_or(2)?.0;
        let initial: crate::case_execution_wire::CaseExecutionVector3 = pyramid_wall::position(&fixture, index as u32);
        let mass: f32 = entity.get::<ComputedMass>().ok_or(2)?.value();
        let inertia: ComputedAngularInertia = *entity.get::<ComputedAngularInertia>().ok_or(2)?;
        if p != RVector::new(initial.x, initial.y, initial.z) || entity.get::<Rotation>().ok_or(2)?.0 != bevy::math::Quat::IDENTITY ||
            entity.get::<LinearVelocity>().ok_or(2)?.0 != RVector::ZERO ||
            entity.get::<AngularVelocity>().ok_or(2)?.0 != bevy::math::Vec3::ZERO ||
            !mass.is_finite() || mass <= 0.0 || (mass - expected_mass).abs() > 1e-5 * expected_mass ||
            !(inertia * bevy::math::Vec3::ONE).is_finite() ||
            (inertia * bevy::math::Vec3::X - bevy::math::Vec3::X * expected_inertia).abs().max_element() > 1e-5 * expected_inertia ||
            (inertia * bevy::math::Vec3::Y - bevy::math::Vec3::Y * expected_inertia).abs().max_element() > 1e-5 * expected_inertia ||
            (inertia * bevy::math::Vec3::Z - bevy::math::Vec3::Z * expected_inertia).abs().max_element() > 1e-5 * expected_inertia ||
            entity.contains::<SleepingDisabled>() != (execution.sleep_mode == CaseExecutionToggle::Disabled) ||
            entity.contains::<Sleeping>() ||
            entity.get::<SweptCcd>().unwrap().filter !=
                if execution.continuous_collision_mode == CaseExecutionToggle::Enabled
                {
                    CcdFilter::DEFAULT
                }
                else
                {
                    CcdFilter::NONE
                } ||
            entity.get::<LinearDamping>().ok_or(2)?.0 != 0.0 || entity.get::<AngularDamping>().ok_or(2)?.0 != 0.0 ||
            entity.get::<Friction>().ok_or(2)?.dynamic_coefficient != execution.friction ||
            entity.get::<Friction>().ok_or(2)?.static_coefficient != execution.friction ||
            entity.get::<Restitution>().ok_or(2)?.coefficient != execution.restitution
        {
            return Err(2);
        }
        if verification_mode == runner_args::VerificationMode::On
        {
            initial_potential_energy -= mass as f64 * (execution.gravity.x as f64 * p.x as f64 +
                execution.gravity.y as f64 * p.y as f64 + execution.gravity.z as f64 * p.z as f64);
        }
    }
    let native_floor: bevy::ecs::world::EntityRef<'_> = app.world().entity(floor);
    if *native_floor.get::<RigidBody>().ok_or(2)? != RigidBody::Static ||
        native_floor.get::<Position>().ok_or(2)?.0 != RVector::new(0.0, -fixture.floor_half_extents.y, 0.0) ||
        native_floor.get::<Rotation>().ok_or(2)?.0 != bevy::math::Quat::IDENTITY ||
        native_floor.get::<Friction>().ok_or(2)?.dynamic_coefficient != execution.friction ||
        native_floor.get::<Friction>().ok_or(2)?.static_coefficient != execution.friction ||
        native_floor.get::<Restitution>().ok_or(2)?.coefficient != execution.restitution
    {
        return Err(2);
    }
    let body_count: usize = app.world_mut().query::<&RigidBody>().iter(app.world()).count();
    let shape_count: usize = app.world_mut().query::<&Collider>().iter(app.world()).count();
    if dynamic_entities.len() != execution.dynamic_body_count as usize ||
        body_count != execution.body_count as usize || shape_count != execution.shape_count as usize
    {
        return Err(2);
    }
    Ok(AvianPyramidWallWorld
    {
        execution: *execution,
        verification_mode,
        app,
        dynamic_entities,
        body_count,
        shape_count,
        completed_step_count: 0,
        observations: [PyramidWallObservation::default(); 4],
        observation_inputs: std::array::from_fn(|ordinal| vec![AvianWallBodyInput::default(); if verification_mode == runner_args::VerificationMode::On && ordinal < 4.min(execution.measured_work_unit_count) as usize { execution.dynamic_body_count as usize } else { 0 }]),
        initial_potential_energy,
    })
}

pub fn step_once(world: &mut AvianPyramidWallWorld)
{
    let timestep: f64 = 1.0 / world.execution.timestep_hz as f64;
    world.app.world_mut().resource_mut::<Time>().advance_by(Duration::from_secs_f64(timestep));
    world.app.world_mut().run_schedule(case_registry::AvianBenchmarkSchedule);
}

pub fn warmup_world(world: &mut AvianPyramidWallWorld, count: usize)
{
    for _ in 0..count
    {
        step_once(world);
    }
}

pub fn step_world_timed(
    world: &mut AvianPyramidWallWorld,
    durations: &mut [Duration],
) -> Result<(), i32>
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
                capture_observation(world, ordinal)?;
            }
        }
    }
    Ok(())
}

pub fn world_transform(
    world: &AvianPyramidWallWorld,
    index: usize,
) -> Result<case_registry::VisualTransform, i32>
{
    let entity: bevy::ecs::world::EntityRef<'_> = world.app.world().entity(world.dynamic_entities[index]);
    let Some(position) = entity.get::<Position>() else
    {
        return Err(2);
    };
    let Some(rotation) = entity.get::<Rotation>() else
    {
        return Err(2);
    };
    Ok(case_registry::VisualTransform
    {
        position_x: position.0.x,
        position_y: position.0.y,
        position_z: position.0.z,
        rotation_x: rotation.0.x,
        rotation_y: rotation.0.y,
        rotation_z: rotation.0.z,
        rotation_w: rotation.0.w,
    })
}

pub fn sample_world_transforms(
    world: &AvianPyramidWallWorld,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    if transforms.len() < world.dynamic_entities.len()
    {
        return Err(2);
    }
    for (index, transform) in transforms.iter_mut().take(world.dynamic_entities.len()).enumerate()
    {
        *transform = case_registry::VisualStableTransform
        {
            stable_slot: index as u32,
            transform: world_transform(world, index)?,
        };
    }
    Ok(())
}

pub fn capture_observation(world: &mut AvianPyramidWallWorld, ordinal: usize) -> Result<(), i32>
{
    let start: Instant = Instant::now();
    for (index, &id) in world.dynamic_entities.iter().enumerate()
    {
        let entity: bevy::ecs::world::EntityRef<'_> = world.app.world().entity(id);
        world.observation_inputs[ordinal][index] = AvianWallBodyInput
        {
            position: entity.get::<Position>().ok_or(2)?.0,
            rotation: entity.get::<Rotation>().ok_or(2)?.0,
            linear: entity.get::<LinearVelocity>().ok_or(2)?.0,
            angular: entity.get::<AngularVelocity>().ok_or(2)?.0,
            inertia: *entity.get::<ComputedAngularInertia>().ok_or(2)?,
            mass: entity.get::<ComputedMass>().ok_or(2)?.value(),
            sleep_flags: (entity.contains::<SleepingDisabled>() as u32) | ((entity.contains::<Sleeping>() as u32) << 1),
        };
    }
    world.observations[ordinal].elapsed_ms = start.elapsed().as_secs_f64() * 1000.0;
    Ok(())
}

pub fn reduce_observation(world: &AvianPyramidWallWorld, ordinal: usize) -> Result<PyramidWallObservation, i32>
{
    let start: Instant = Instant::now();
    let mut sample: PyramidWallObservation = PyramidWallObservation::default();
    for (index, input) in world.observation_inputs[ordinal].iter().enumerate()
    {
        let p: RVector = input.position;
        let q: bevy::math::Quat = input.rotation;
        let linear: RVector = input.linear;
        let angular: bevy::math::Vec3 = input.angular;
        let local: bevy::math::Vec3 = q.inverse() * angular;
        let inertia: ComputedAngularInertia = input.inertia;
        let momentum: bevy::math::Vec3 = inertia * local;
        let energy: f64 = 0.5 * (local.x as f64 * momentum.x as f64 + local.y as f64 * momentum.y as f64 + local.z as f64 * momentum.z as f64);
        let sleep_matches: i32 = (((input.sleep_flags & 1) != 0) == (world.execution.sleep_mode == CaseExecutionToggle::Disabled) &&
            (world.execution.sleep_mode == CaseExecutionToggle::Enabled || (input.sleep_flags & 2) == 0)) as i32;
        pyramid_wall::accumulate(&world.execution.pyramid_wall, index as u32, p.to_array(), q.to_array(),
            linear.to_array(), angular.to_array(), input.mass as f64, energy,
            sleep_matches, world.execution.gravity, &mut sample);
    }
    pyramid_wall::finish(world.execution.dynamic_body_count, &mut sample);
    sample.elapsed_ms = world.observations[ordinal].elapsed_ms + start.elapsed().as_secs_f64() * 1000.0;
    Ok(sample)
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
    for index in 0..world.dynamic_entities.len()
    {
        instances[index] = case_registry::VisualInstance
        {
            geometry_index: 0,
            stable_slot: index as u32,
            transform_slot: index as u32,
            initial_transform: world_transform(world, index)?,
        };
    }
    let floor_slot: usize = world.dynamic_entities.len();
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
    let native_count: u32 = execution.solver_values[crate::case_execution_wire::CaseSolverField::Substeps as usize];
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
    format!("substeps={native_count}; sleep={sleep}; ccd={ccd}; linear_damping=0; angular_damping=0; solver_defaults=avian; worker_count={thread_count}")
}

pub fn run_headless(
    args: &runner_args::RunnerArgs,
    effective_thread_count: usize,
) -> Result<(), i32>
{
    let mut world: AvianPyramidWallWorld = create_world(&args.case_execution, args.verification_mode)?;
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
        step_world_timed(&mut world, std::slice::from_mut(duration))?;
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
        world.observations[ordinal] = reduce_observation(&world, ordinal)?;
    }
    let elapsed_ms: f64 = durations.iter().map(Duration::as_secs_f64).sum::<f64>() * 1000.0;
    let mut invalid_transform_count: usize = match args.verification_mode
    {
        runner_args::VerificationMode::On => world.observations.iter().map(|sample| sample.invalid_bodies as usize).sum(),
        runner_args::VerificationMode::Off => 0,
    };
    if args.verification_mode == runner_args::VerificationMode::Off
    {
        for index in 0..world.dynamic_entities.len()
        {
            let transform: case_registry::VisualTransform = world_transform(&world, index)?;
            if ![transform.position_x, transform.position_y, transform.position_z, transform.rotation_x, transform.rotation_y, transform.rotation_z, transform.rotation_w].iter().all(|value: &f32| value.is_finite())
            {
                invalid_transform_count += 1;
            }
        }
    }
    let counts_valid: i32 = ( world.dynamic_entities.len() == args.case_execution.dynamic_body_count as usize
        && world.body_count == args.case_execution.body_count as usize
        && world.shape_count == args.case_execution.shape_count as usize) as i32;
    let result: result_writer::BenchmarkResult = result_writer::BenchmarkResult
    {
        physics_settings: visual_physics_settings(&args.case_execution, effective_thread_count),
        body_count: world.body_count,
        shape_count: world.shape_count,
        query_count: 0,
        constraint_count: 0,
        invalid_transform_count,
        effective_thread_count,
        effective_worker_count: effective_thread_count,
        completed_work_unit_count: world.completed_step_count,
        workload_elapsed_ms: elapsed_ms,
        case_validity: if counts_valid != 0 && invalid_transform_count == 0
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
