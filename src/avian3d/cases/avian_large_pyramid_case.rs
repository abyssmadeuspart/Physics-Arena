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
    build_visual_debug_primitives,
};

pub struct AvianLargePyramidWorld
{
    pub execution: CaseExecutionSpec,
    pub app: App,
    pub dynamic_entities: Vec<Entity>,
    pub body_count: usize,
    pub shape_count: usize,
    pub completed_step_count: usize,
}

pub fn create_world(execution: &CaseExecutionSpec) -> Result<AvianLargePyramidWorld, i32>
{
    let fixture: crate::case_execution_wire::CaseExecutionLargePyramid = execution.large_pyramid;
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
    let shape_rotation: bevy::math::Quat = case_registry::shape_rotation(execution.selected_geometry.axis);
    for layer in 0..fixture.row_count
    {
        let layer_side: u32 = fixture.row_count - layer;
        for depth in 0..layer_side
        {
            for column in 0..layer_side
            {
                let x: f32 = fixture.base_center.x
                    + (column as f32 - 0.5 * (layer_side - 1) as f32)
                        * fixture.box_spacing.x;
                let y: f32 = fixture.base_center.y + layer as f32 * fixture.box_spacing.y;
                let z: f32 = fixture.base_center.z
                    + (depth as f32 - 0.5 * (layer_side - 1) as f32)
                        * fixture.box_spacing.z;
                let mut commands: bevy::ecs::world::EntityWorldMut<'_> = app.world_mut().spawn((
                    RigidBody::Dynamic,
                    shape.clone(),
                    ColliderDensity(fixture.box_density),
                    Friction::new(execution.friction),
                    Restitution::new(execution.restitution),
                    Position::from_xyz(x, y, z),
                    Rotation(shape_rotation),
                    Transform::from_xyz(x, y, z).with_rotation(shape_rotation),
                ));
                if execution.sleep_mode == CaseExecutionToggle::Disabled
                {
                    commands.insert(SleepingDisabled);
                }
                commands.insert(SweptCcd::default().with_filter(
                    if execution.continuous_collision_mode == CaseExecutionToggle::Enabled
                    {
                        CcdFilter::DEFAULT
                    }
                    else
                    {
                        CcdFilter::NONE
                    },
                ));
                dynamic_entities.push(commands.id());
            }
        }
    }
    for projectile_index in 0..fixture.projectile_count
    {
        let x: f32 = fixture.projectile_initial_center.x
            + projectile_index as f32 * fixture.projectile_center_spacing.x;
        let y: f32 = fixture.projectile_initial_center.y
            + projectile_index as f32 * fixture.projectile_center_spacing.y;
        let z: f32 = fixture.projectile_initial_center.z
            + projectile_index as f32 * fixture.projectile_center_spacing.z;
        let mut projectile_commands: bevy::ecs::world::EntityWorldMut<'_> = app.world_mut().spawn((
            RigidBody::Dynamic,
            Collider::sphere(fixture.projectile_radius),
            ColliderDensity(fixture.projectile_density),
            Friction::new(execution.friction),
            Restitution::new(execution.restitution),
            Position::from_xyz(x, y, z),
            Rotation::IDENTITY,
            Transform::from_xyz(x, y, z),
        ));
        if execution.sleep_mode == CaseExecutionToggle::Disabled
        {
            projectile_commands.insert(SleepingDisabled);
        }
        projectile_commands.insert(SweptCcd::default().with_filter(
            if execution.continuous_collision_mode == CaseExecutionToggle::Enabled
            {
                CcdFilter::DEFAULT
            }
            else
            {
                CcdFilter::NONE
            },
        ));
        dynamic_entities.push(projectile_commands.id());
    }
    app.world_mut().spawn((
        RigidBody::Static,
        Collider::cuboid(
            fixture.floor_half_extents.x * 2.0,
            fixture.floor_half_extents.y * 2.0,
            fixture.floor_half_extents.z * 2.0,
        ),
        Friction::new(execution.friction),
        Restitution::new(execution.restitution),
        Position::from_xyz(0.0, -fixture.floor_half_extents.y, 0.0),
        Rotation::IDENTITY,
        Transform::from_xyz(0.0, -fixture.floor_half_extents.y, 0.0),
    ));
    if dynamic_entities.len() != execution.dynamic_body_count as usize
    {
        return Err(2);
    }
    Ok(AvianLargePyramidWorld
    {
        execution: *execution,
        app,
        dynamic_entities,
        body_count: execution.body_count as usize,
        shape_count: execution.shape_count as usize,
        completed_step_count: 0,
    })
}

pub fn step_once(world: &mut AvianLargePyramidWorld)
{
    let timestep: f64 = 1.0 / world.execution.timestep_hz as f64;
    world.app.world_mut().resource_mut::<Time>().advance_by(Duration::from_secs_f64(timestep));
    world.app.world_mut().run_schedule(case_registry::AvianBenchmarkSchedule);
}

pub fn warmup_world(world: &mut AvianLargePyramidWorld, count: usize)
{
    for _ in 0..count
    {
        step_once(world);
    }
}

pub fn launch_projectile(world: &mut AvianLargePyramidWorld) -> Result<(), i32>
{
    let fixture: crate::case_execution_wire::CaseExecutionLargePyramid = world.execution.large_pyramid;
    let first_projectile: usize = world.dynamic_entities.len() - fixture.projectile_count as usize;
    for index in first_projectile..world.dynamic_entities.len()
    {
        let entity_id: bevy::ecs::entity::Entity = world.dynamic_entities[index];
        let mut entity: bevy::ecs::world::EntityWorldMut<'_> = world.app.world_mut().entity_mut(entity_id);
        let Some(mut linear_velocity) = entity.get_mut::<LinearVelocity>() else
        {
            return Err(2);
        };
        *linear_velocity = LinearVelocity(RVector::new(
            fixture.projectile_launch_velocity.x,
            fixture.projectile_launch_velocity.y,
            fixture.projectile_launch_velocity.z,
        ));
        drop(linear_velocity);
        entity.remove::<Sleeping>();
    }
    Ok(())
}

pub fn step_world_timed(
    world: &mut AvianLargePyramidWorld,
    durations: &mut [Duration],
) -> Result<(), i32>
{
    let launch_after: usize = world.execution.large_pyramid.projectile_launch_after_work_units as usize;
    for duration in durations
    {
        if world.completed_step_count == launch_after
        {
            launch_projectile(world)?;
        }
        let start: Instant = Instant::now();
        step_once(world);
        *duration = start.elapsed();
        world.completed_step_count += 1;
    }
    Ok(())
}

pub fn world_transform(
    world: &AvianLargePyramidWorld,
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
    world: &AvianLargePyramidWorld,
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

pub fn invalid_world_transform_count(world: &AvianLargePyramidWorld) -> usize
{
    let mut count: usize = 0;
    for index in 0..world.dynamic_entities.len()
    {
        let Ok(transform) = world_transform(world, index) else
        {
            count += 1;
            continue;
        };
        if !transform.position_x.is_finite()
            || !transform.position_y.is_finite()
            || !transform.position_z.is_finite()
            || !transform.rotation_x.is_finite()
            || !transform.rotation_y.is_finite()
            || !transform.rotation_z.is_finite()
            || !transform.rotation_w.is_finite()
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
    let first_projectile: usize = world.dynamic_entities.len() - fixture.projectile_count as usize;
    for index in 0..world.dynamic_entities.len()
    {
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
            initial_transform: world_transform(world, index)?,
        };
    }
    let floor_slot: usize = world.dynamic_entities.len();
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
    let mut world: AvianLargePyramidWorld = create_world(&args.case_execution)?;
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
        step_world_timed(&mut world, std::slice::from_mut(duration))?;
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
    let counts_valid: bool = world.dynamic_entities.len() == args.case_execution.dynamic_body_count as usize
        && world.body_count == args.case_execution.body_count as usize
        && world.shape_count == args.case_execution.shape_count as usize;
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
        case_validity: if counts_valid && invalid_transform_count == 0
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
