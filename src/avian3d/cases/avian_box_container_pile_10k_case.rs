use crate::case_execution_wire::{CaseExecutionSpec, CaseExecutionToggle};
use crate::{case_registry, result_writer, runner_args};
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
    build_visual_debug_primitives: build_no_visual_debug_primitives,
};

pub fn build_no_visual_debug_primitives(
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

pub fn sample_visual_transforms(
    state: &case_registry::CaseView,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    let case_registry::CaseView::ContainerPile(world) = state else
    {
        return Err(2);
    };
    sample_transforms(world, transforms)
}

pub struct AvianWorld
{
    pub execution: CaseExecutionSpec,
    pub app: App,
    pub dynamic_entities: Vec<Entity>,
    pub body_count: usize,
    pub shape_count: usize,
    pub completed_step_count: usize,
}

pub fn create_world(execution: &CaseExecutionSpec) -> Result<AvianWorld, i32>
{
    let fixture: crate::case_execution_wire::CaseExecutionOpenContainer = execution.open_container;
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
    .insert_resource(TimeUpdateStrategy::ManualDuration(Duration::from_secs_f64(
        timestep,
    )));

    while app.plugins_state() != PluginsState::Ready
    {
        bevy::tasks::tick_global_task_pools_on_main_thread();
    }
    app.finish();
    app.cleanup();

    let mut dynamic_entities: Vec<bevy::ecs::entity::Entity> = Vec::with_capacity(execution.dynamic_body_count as usize);
    let shape: Collider = case_registry::create_resolved_shape(&execution.selected_geometry, execution)?;
    let shape_rotation: bevy::math::Quat = case_registry::shape_rotation(execution.selected_geometry.axis);

    for static_box in fixture.static_boxes.iter().take(fixture.static_box_count as usize)
    {
        add_static_box(&mut app, static_box, execution);
    }

    let origin_x: f32 = -0.5 * (fixture.dynamic_grid[0] as f32 - 1.0) * fixture.dynamic_spacing.x;
    let origin_z: f32 = -0.5 * (fixture.dynamic_grid[2] as f32 - 1.0) * fixture.dynamic_spacing.z;
    for y in 0..fixture.dynamic_grid[1]
    {
        for z in 0..fixture.dynamic_grid[2]
        {
            for x in 0..fixture.dynamic_grid[0]
            {
                let position_x: f32 = origin_x + x as f32 * fixture.dynamic_spacing.x;
                let position_y: f32 = fixture.dynamic_initial_y + y as f32 * fixture.dynamic_spacing.y;
                let position_z: f32 = origin_z + z as f32 * fixture.dynamic_spacing.z;
                let mut entity_commands: bevy::ecs::world::EntityWorldMut<'_> = app.world_mut().spawn((
                        RigidBody::Dynamic,
                        shape.clone(),
                        ColliderDensity(fixture.density),
                        Friction::new(execution.friction),
                        Restitution::new(execution.restitution),
                        Position::from_xyz(position_x, position_y, position_z),
                        Rotation(shape_rotation),
                        Transform::from_xyz(position_x, position_y, position_z).with_rotation(shape_rotation),
                    ));
                if execution.sleep_mode == CaseExecutionToggle::Disabled
                {
                    entity_commands.insert(SleepingDisabled);
                }
                entity_commands.insert(SweptCcd::default().with_filter(
                    if execution.continuous_collision_mode == CaseExecutionToggle::Enabled
                    {
                        CcdFilter::DEFAULT
                    }
                    else
                    {
                        CcdFilter::NONE
                    },
                ));
                dynamic_entities.push(entity_commands.id());
            }
        }
    }

    if dynamic_entities.len() != execution.dynamic_body_count as usize
    {
        eprintln!(
            "invalid_result body_count={} shape_count={} dynamic_body_count={}",
            execution.body_count,
            execution.shape_count,
            dynamic_entities.len()
        );
        return Err(2);
    }

    Ok(AvianWorld
    {
        execution: *execution,
        app,
        dynamic_entities,
        body_count: execution.body_count as usize,
        shape_count: execution.shape_count as usize,
        completed_step_count: 0,
    })
}

pub fn add_static_box(app: &mut App,
    static_box: &crate::case_execution_wire::CaseExecutionBox,
    execution: &CaseExecutionSpec)
{
    app.world_mut().spawn((
        RigidBody::Static,
        Collider::cuboid(static_box.half_extents.x * 2.0,
            static_box.half_extents.y * 2.0, static_box.half_extents.z * 2.0),
        Friction::new(execution.friction),
        Restitution::new(execution.restitution),
        Position::from_xyz(static_box.center.x, static_box.center.y, static_box.center.z),
        Rotation::IDENTITY,
        Transform::from_xyz(static_box.center.x, static_box.center.y, static_box.center.z),
    ));
}

pub fn step_world(world: &mut AvianWorld, step_count: usize)
{
    let timestep: f64 = 1.0 / world.execution.timestep_hz as f64;
    for _ in 0..step_count
    {
        world
            .app
            .world_mut()
            .resource_mut::<Time>()
            .advance_by(Duration::from_secs_f64(timestep));
        world
            .app
            .world_mut()
            .run_schedule(case_registry::AvianBenchmarkSchedule);
        world.completed_step_count += 1;
    }
}

pub fn step_world_timed(world: &mut AvianWorld, step_durations: &mut [Duration])
{
    let timestep: f64 = 1.0 / world.execution.timestep_hz as f64;
    for duration in step_durations
    {
        let start: Instant = Instant::now();
        world
            .app
            .world_mut()
            .resource_mut::<Time>()
            .advance_by(Duration::from_secs_f64(timestep));
        world
            .app
            .world_mut()
            .run_schedule(case_registry::AvianBenchmarkSchedule);
        *duration = start.elapsed();
        world.completed_step_count += 1;
    }
}

pub fn sample_transforms(
    world: &AvianWorld,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    if transforms.len() < world.dynamic_entities.len()
    {
        return Err(2);
    }
    for index in 0..world.dynamic_entities.len()
    {
        let entity: bevy::ecs::world::EntityRef<'_> = world.app.world().entity(world.dynamic_entities[index]);
        let position: RVector = match entity.get::<Position>()
        {
            Some(value) => value.0,
            None => return Err(2),
        };
        let rotation: bevy::math::Quat = match entity.get::<Rotation>()
        {
            Some(value) => value.0,
            None => return Err(2),
        };
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

pub fn build_visual_scene(
    state: &case_registry::CaseView,
    geometries: &mut [case_registry::VisualGeometry],
    meshes: &mut case_registry::VisualMeshStorage,
    instances: &mut [case_registry::VisualInstance],
) -> Result<(usize, usize), i32>
{
    let case_registry::CaseView::ContainerPile(world) = state else
    {
        return Err(2);
    };
    let execution: CaseExecutionSpec = world.execution;
    let fixture: crate::case_execution_wire::CaseExecutionOpenContainer = execution.open_container;
    if geometries.len() < 1 + fixture.static_box_count as usize ||
        instances.len() < execution.body_count as usize
    {
        return Err(2);
    }
    geometries[0] = case_registry::build_resolved_visual_geometry(&execution,
        &execution.selected_geometry, meshes)?;
    for (index, static_box) in fixture.static_boxes.iter().take(fixture.static_box_count as usize).enumerate()
    {
        geometries[index + 1] = case_registry::VisualGeometry
        {
            kind: 2,
            parameter_x: static_box.half_extents.x,
            parameter_y: static_box.half_extents.y,
            parameter_z: static_box.half_extents.z,
            ..case_registry::VisualGeometry::default()
        };
    }
    let mut transforms: Vec<case_registry::VisualStableTransform> = vec![case_registry::VisualStableTransform
    {
        stable_slot: 0,
        transform: case_registry::VisualTransform
        {
            position_x: 0.0, position_y: 0.0, position_z: 0.0,
            rotation_x: 0.0, rotation_y: 0.0, rotation_z: 0.0, rotation_w: 1.0,
        },
    }; world.dynamic_entities.len()];
    sample_transforms(world, &mut transforms)?;
    for index in 0..world.dynamic_entities.len()
    {
        instances[index] = case_registry::VisualInstance
        {
            geometry_index: 0, stable_slot: index as u32, transform_slot: index as u32,
            initial_transform: transforms[index].transform,
        };
    }
    for (index, static_box) in fixture.static_boxes.iter().take(fixture.static_box_count as usize).enumerate()
    {
        let stable_slot: usize = world.dynamic_entities.len() + index;
        instances[stable_slot] = case_registry::VisualInstance
        {
            geometry_index: (index + 1) as u32, stable_slot: stable_slot as u32,
            transform_slot: u32::MAX,
            initial_transform: case_registry::VisualTransform
            {
                position_x: static_box.center.x, position_y: static_box.center.y,
                position_z: static_box.center.z,
                rotation_x: 0.0, rotation_y: 0.0, rotation_z: 0.0, rotation_w: 1.0,
            },
        };
    }
    Ok((1 + fixture.static_box_count as usize, execution.body_count as usize))
}

pub fn invalid_transform_count(world: &AvianWorld) -> usize
{
    let mut invalid_transform_count: usize = 0;

    for entity in &world.dynamic_entities
    {
        let entity_ref: bevy::ecs::world::EntityRef<'_> = world.app.world().entity(*entity);
        let position: RVector = match entity_ref.get::<Position>()
        {
            Some(value) => value.0,
            None =>
            {
                invalid_transform_count += 1;
                continue;
            }
        };
        let rotation: bevy::math::Quat = match entity_ref.get::<Rotation>()
        {
            Some(value) => value.0,
            None =>
            {
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
    }

    invalid_transform_count
}

pub fn host_route() -> &'static str
{
    if cfg!(target_os = "windows")
    {
        "windows"
    }
    else
    {
        "linux"
    }
}

pub fn visual_physics_settings(execution: &CaseExecutionSpec, thread_count: usize) -> String
{
    let native_count = execution.solver_values[crate::case_execution_wire::CaseSolverField::Substeps as usize];
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
    format!("substeps={native_count}; sleep={sleep}; ccd={ccd}; solver_defaults=avian; worker_count={thread_count}")
}

pub fn run_headless(
    args: &runner_args::RunnerArgs,
    effective_thread_count: usize,
) -> Result<(), i32>
{
    let mut capture: Option<crate::stack_state_capture::Capture> = match args.verification_mode
    {
        runner_args::VerificationMode::On => Some(crate::stack_state_capture::open(args)?),
        runner_args::VerificationMode::Off => None,
    };
    if args.warmup_steps > 0
    {
        let mut warmup_world: AvianWorld = create_world(&args.case_execution)?;
        for ordinal in 0..=args.warmup_steps
        {
            if ordinal != 0 { step_world(&mut warmup_world, 1); }
            if let Some(capture) = capture.as_mut()
            {
                capture.frame_start = std::time::Instant::now();
                sample_visual_transforms(&case_registry::CaseView::ContainerPile(&mut warmup_world), &mut capture.transforms)?;
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
    }
    let mut world: AvianWorld = create_world(&args.case_execution)?;
    let segment: u32 = u32::from(args.warmup_steps > 0);
    if let Some(capture) = capture.as_mut()
    {
        capture.frame_start = std::time::Instant::now();
        sample_visual_transforms(&case_registry::CaseView::ContainerPile(&mut world), &mut capture.transforms)?;
        crate::stack_state_capture::append(capture, crate::stack_state_capture::Phase::Construction, segment, 0)?;
    }
    let mut step_durations: Vec<Duration> = vec![Duration::ZERO; args.step_count];
    let mut recording: Option<crate::replay_recording::RecordingWriter> = match args.recording_mode
    {
        crate::runner_args::RecordingMode::On => Some(crate::replay_recording::begin_recording(args, &case_registry::CaseView::ContainerPile(&mut world))?),
        crate::runner_args::RecordingMode::Off => None,
    };
    for (index, duration) in step_durations.iter_mut().enumerate()
    {
        step_world_timed(&mut world, std::slice::from_mut(duration));
        if let Some(capture) = capture.as_mut()
        {
            capture.frame_start = std::time::Instant::now();
            sample_visual_transforms(&case_registry::CaseView::ContainerPile(&mut world), &mut capture.transforms)?;
            crate::stack_state_capture::append(capture, crate::stack_state_capture::Phase::Measured, segment, index as u32 + 1)?;
        }
        if let Some(writer) = recording.as_mut()
        {
            crate::replay_recording::append_frame(writer, args,
                &case_registry::CaseView::ContainerPile(&mut world), index as u64 + 1)?;
        }
    }
    if let Some(writer) = recording
    {
        crate::replay_recording::complete_recording(writer)?;
    }
    if let Some(capture) = capture
    {
        crate::stack_state_capture::close(capture)?;
    }
    let elapsed_ms: f64 = step_durations.iter().map(Duration::as_secs_f64).sum::<f64>() * 1000.0;
    let invalid_transform_count: usize = invalid_transform_count(&world);
    let case_validity: result_writer::ResultValidity = if invalid_transform_count == 0
        && world.dynamic_entities.len() == args.case_execution.dynamic_body_count as usize
        && world.body_count == args.case_execution.body_count as usize
        && world.shape_count == args.case_execution.shape_count as usize
    {
        result_writer::ResultValidity::Valid
    }
    else
    {
        result_writer::ResultValidity::Invalid
    };
    let metric_valid: bool = elapsed_ms > 0.0
        && elapsed_ms.is_finite()
        && world.completed_step_count == args.step_count
        && world.body_count == args.case_execution.body_count as usize
        && world.shape_count == args.case_execution.shape_count as usize
        && world.dynamic_entities.len() == args.case_execution.dynamic_body_count as usize;
    let metric_validity: result_writer::ResultValidity = if metric_valid
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
        body_count: world.body_count,
        shape_count: world.shape_count,
        query_count: args.case_execution.query_count as usize,
        constraint_count: args.case_execution.constraint_count as usize,
        invalid_transform_count,
        effective_thread_count,
        effective_worker_count: effective_thread_count,
        completed_work_unit_count: world.completed_step_count,
        workload_elapsed_ms: elapsed_ms,
        case_validity,
        metric_validity,
        observations: Vec::new(),
    };
    result_writer::write_result(args, &result)?;
    result_writer::write_step_timing(args, &step_durations)
}
