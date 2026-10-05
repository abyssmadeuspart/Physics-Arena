use crate::case_execution_wire::{
    CaseExecutionRagdoll, CaseExecutionRagdollPart,
    CaseExecutionSpec, CaseExecutionToggle,
};
use crate::{case_registry, result_writer, runner_args};
use avian3d::{math::RVector, prelude::*};
use bevy::{
    MinimalPlugins,
    app::{App, PluginsState},
    ecs::entity::Entity,
    math::Quat,
    prelude::Transform,
    time::{Time, TimeUpdateStrategy},
    transform::TransformPlugin,
};
use std::time::Duration;

#[path = "../../common/ragdoll_quality.rs"]
pub mod ragdoll_quality;

pub const DESCRIPTOR: case_registry::AvianCaseDescriptor =
    case_registry::AvianCaseDescriptor
    {
        engine_id: case_registry::ENGINE_ID,
    };

pub const REGISTRATION: case_registry::CaseRegistration =
    case_registry::CaseRegistration
    {
    descriptor: DESCRIPTOR,
    run_headless,
    build_visual_scene,
    sample_visual_transforms,
    build_visual_debug_primitives,
};

pub struct AvianRagdollWorld
{
    pub execution: CaseExecutionSpec,
    pub quality: ragdoll_quality::Quality,
    pub app: App,
    pub dynamic_entities: Box<[Entity]>,
    pub joint_entities: Box<[Entity]>,
    pub body_count: usize,
    pub shape_count: usize,
    pub constraint_count: usize,
    pub completed_step_count: usize,
}

pub fn ragdoll_rotation(fixture: CaseExecutionRagdoll, ragdoll_index: usize) -> Quat
{
    let yaw: f32 = fixture.yaw_pattern_degrees[
        ragdoll_index % fixture.yaw_pattern_count as usize].to_radians();
    Quat::from_rotation_y(yaw) * Quat::from_rotation_x(fixture.pitch_degrees.to_radians())
}

pub fn ragdoll_base(fixture: CaseExecutionRagdoll, row: usize, column: usize) -> RVector
{
    RVector::new(
        (column as f32 - 0.5 * (fixture.ragdoll_grid[1] - 1) as f32) *
            fixture.column_spacing,
        (fixture.stair_count as usize - 1 - row) as f32 * fixture.stair_rise +
            fixture.base_height_offset,
        (row as f32 - 0.5 * (fixture.stair_count - 1) as f32) *
            fixture.row_spacing,
    )
}

pub fn add_static_box(
    app: &mut App,
    position: RVector,
    half_extents: RVector,
    execution: &CaseExecutionSpec,
)
{
    app.world_mut().spawn((
        RigidBody::Static,
        Collider::cuboid(
            half_extents.x * 2.0,
            half_extents.y * 2.0,
            half_extents.z * 2.0,
        ),
        Position(position),
        Rotation::IDENTITY,
        Transform::from_translation(position),
        Friction::new(execution.friction),
        Restitution::new(execution.restitution),
    ));
}

pub fn create_world(execution: &CaseExecutionSpec) -> Result<AvianRagdollWorld, i32>
{
    let fixture: crate::case_execution_wire::CaseExecutionRagdoll = execution.ragdoll;
    let mut app: App = App::new();
    app.add_plugins((
        MinimalPlugins,
        TransformPlugin,
        PhysicsPlugins::new(case_registry::AvianBenchmarkSchedule),
    ))
    .insert_resource(Gravity(RVector::new(
        execution.gravity.x, execution.gravity.y, execution.gravity.z)))
    .insert_resource(SubstepCount(execution.solver_values[crate::case_execution_wire::CaseSolverField::Substeps as usize]))
    .insert_resource(Time::from_hz(execution.timestep_hz as f64))
    .insert_resource(TimeUpdateStrategy::ManualDuration(Duration::from_secs_f64(
        1.0 / execution.timestep_hz as f64,
    )));
    while app.plugins_state() != PluginsState::Ready
    {
        bevy::tasks::tick_global_task_pools_on_main_thread();
    }
    app.finish();
    app.cleanup();

    for row in 0..fixture.stair_count as usize
    {
        add_static_box(
            &mut app,
            RVector::new(
                0.0,
                (fixture.stair_count as usize - 1 - row) as f32 * fixture.stair_rise -
                    fixture.stair_half_height,
                (row as f32 - 0.5 * (fixture.stair_count - 1) as f32) *
                    fixture.stair_depth,
            ),
            RVector::new(fixture.stair_half_width, fixture.stair_half_height,
                fixture.stair_half_depth),
            execution,
        );
    }
    for static_box in fixture.extra_static_boxes
        .iter().take(fixture.extra_static_box_count as usize)
    {
        add_static_box(
            &mut app,
            RVector::new(static_box.center.x, static_box.center.y, static_box.center.z),
            RVector::new(static_box.half_extents.x, static_box.half_extents.y,
                static_box.half_extents.z),
            execution,
        );
    }

    let mut dynamic_entities: Vec<bevy::ecs::entity::Entity> = Vec::with_capacity(execution.dynamic_body_count as usize);
    let mut part_shapes: Vec<Collider> = Vec::with_capacity(fixture.part_count as usize);
    let mut part_densities: Vec<f32> = Vec::with_capacity(fixture.part_count as usize);
    for part_index in 0..fixture.part_count as usize
    {
        let geometry: crate::case_execution_wire::CaseExecutionGeometry =
            crate::case_execution_wire::part_geometry(&fixture.parts[part_index]);
        let shared_index: Option<usize> = (0..part_index).find(|previous|
            crate::case_execution_wire::same_geometry(geometry,
                crate::case_execution_wire::part_geometry(&fixture.parts[*previous])) != 0);
        let shape: Collider = match shared_index
        {
            Some(index) => part_shapes[index].clone(),
            None => case_registry::create_resolved_shape(&geometry, execution)?,
        };
        let unit_mass: f32 = avian3d::dynamics::rigid_body::mass_properties::bevy_heavy::ComputeMassProperties3d::mass(&shape, 1.0);
        if !unit_mass.is_finite() || unit_mass <= 0.0
        {
            return Err(2);
        }
        part_densities.push(fixture.part_mass / unit_mass);
        part_shapes.push(shape);
    }
    for row in 0..fixture.ragdoll_grid[0] as usize
    {
        for column in 0..fixture.ragdoll_grid[1] as usize
        {
            let ragdoll_index: usize = row * fixture.ragdoll_grid[1] as usize + column;
            let rotation: bevy::math::Quat = ragdoll_rotation(fixture, ragdoll_index);
            let base: RVector = ragdoll_base(fixture, row, column);
            for (part_index, part) in fixture.parts.iter().take(fixture.part_count as usize).enumerate()
            {
                let center: RVector = RVector::new(part.center.x, part.center.y, part.center.z);
                let position: RVector = base + rotation * center;
                let body_rotation: bevy::math::Quat = rotation * case_registry::shape_rotation(part.axis);
                let mut entity_commands: bevy::ecs::world::EntityWorldMut<'_> = app.world_mut().spawn((
                    RigidBody::Dynamic,
                    part_shapes[part_index].clone(),
                    ColliderDensity(part_densities[part_index]),
                    Position(position),
                    Rotation(body_rotation),
                    Transform::from_translation(position).with_rotation(body_rotation),
                    LinearVelocity(RVector::new(
                        0.0,
                        0.0,
                        if row == 0
                        {
                            fixture.trigger_row_speed
                        }
                        else
                        {
                            fixture.follower_row_speed
                        },
                    )),
                    AngularVelocity(RVector::ZERO),
                    LinearDamping(fixture.linear_damping),
                    AngularDamping(fixture.angular_damping),
                    Friction::new(execution.friction),
                    Restitution::new(execution.restitution),
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
                let entity: bevy::ecs::entity::Entity = entity_commands.id();
                dynamic_entities.push(entity);
            }
        }
    }

    let mut joint_entities: Vec<bevy::ecs::entity::Entity> = Vec::with_capacity(execution.constraint_count as usize);
    let ragdoll_count: usize = (fixture.ragdoll_grid[0] * fixture.ragdoll_grid[1]) as usize;
    for ragdoll_index in 0..ragdoll_count
    {
        for link in fixture.links.iter().take(fixture.link_count as usize)
        {
            let parent_part: usize = link.parent_part as usize;
            let child_part: usize = link.child_part as usize;
            let joint: SphericalJoint = SphericalJoint::new(
                dynamic_entities[ragdoll_index * fixture.part_count as usize + parent_part],
                dynamic_entities[ragdoll_index * fixture.part_count as usize + child_part],
            )
            .with_local_anchor1(RVector::new(
                link.parent_local_anchor.x,
                link.parent_local_anchor.y,
                link.parent_local_anchor.z,
            ))
            .with_local_anchor2(RVector::new(
                link.child_local_anchor.x,
                link.child_local_anchor.y,
                link.child_local_anchor.z,
            ));
            let mut joint_commands: bevy::ecs::world::EntityWorldMut = app.world_mut().spawn(joint);
            if fixture.linked_collision_mode == CaseExecutionToggle::Disabled
            {
                joint_commands.insert(JointCollisionDisabled);
            }
            joint_entities.push(joint_commands.id());
        }
    }

    let (native_body_count, native_shape_count, native_constraint_count): (usize, usize, usize) =
    {
        let world: &mut bevy::ecs::world::World = app.world_mut();
        let mut body_query: bevy::ecs::query::QueryState<&RigidBody> = world.query::<&RigidBody>();
        let body_count: usize = body_query.iter(world).count();
        let mut shape_query: bevy::ecs::query::QueryState<&Collider> = world.query::<&Collider>();
        let shape_count: usize = shape_query.iter(world).count();
        let mut constraint_query: bevy::ecs::query::QueryState<&SphericalJoint> = world.query::<&SphericalJoint>();
        let constraint_count: usize = constraint_query.iter(world).count();
        (body_count, shape_count, constraint_count)
    };
    if dynamic_entities.len() != execution.dynamic_body_count as usize
        || joint_entities.len() != execution.constraint_count as usize
        || native_body_count != execution.body_count as usize
        || native_shape_count != execution.shape_count as usize
        || native_constraint_count != execution.constraint_count as usize
    {
        eprintln!(
            "invalid_result body_count={} shape_count={} dynamic_body_count={} constraint_count={}",
            native_body_count,
            native_shape_count,
            dynamic_entities.len(),
            native_constraint_count
        );
        return Err(2);
    }

    Ok(AvianRagdollWorld
    {
        execution: *execution,
        quality: ragdoll_quality::Quality::default(),
        app,
        dynamic_entities: dynamic_entities.into_boxed_slice(),
        joint_entities: joint_entities.into_boxed_slice(),
        body_count: native_body_count,
        shape_count: native_shape_count,
        constraint_count: native_constraint_count,
        completed_step_count: 0,
    })
}

pub fn step_world(world: &mut AvianRagdollWorld, step_count: usize)
{
    let timestep: f64 = 1.0 / world.execution.timestep_hz as f64;
    for _ in 0..step_count
    {
        world.app.world_mut().resource_mut::<Time>()
            .advance_by(Duration::from_secs_f64(timestep));
        world.app.world_mut().run_schedule(case_registry::AvianBenchmarkSchedule);
        world.completed_step_count += 1;
    }
}

pub fn step_world_quality(world: &mut AvianRagdollWorld, step_count: usize) -> Result<(), i32>
{
    if world.completed_step_count + step_count > world.execution.measured_work_unit_count as usize
    {
        return Err(2);
    }
    for _ in 0..step_count
    {
        step_world(world, 1);
        let mut quality: ragdoll_quality::Quality = world.quality;
        ragdoll_quality::accumulate(&world.execution, |index: usize| -> Option<ragdoll_quality::Pose>
        {
            let entity: bevy::ecs::world::EntityRef<'_> = world.app.world().get_entity(world.dynamic_entities[index]).ok()?;
            let position: &Position = entity.get::<Position>()?;
            let rotation: &Rotation = entity.get::<Rotation>()?;
            Some(ragdoll_quality::Pose
            {
                position: [position.x, position.y, position.z],
                rotation: [rotation.x, rotation.y, rotation.z, rotation.w],
            })
        }, world.completed_step_count, &mut quality);
        world.quality = quality;
    }
    Ok(())
}

pub fn sample_transforms(
    world: &AvianRagdollWorld,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    if transforms.len() < world.execution.dynamic_body_count as usize
    {
        return Err(2);
    }
    for (index, entity) in world.dynamic_entities.iter().enumerate()
    {
        let entity_ref: bevy::ecs::world::EntityRef<'_> = world.app.world().entity(*entity);
        let Some(position) = entity_ref.get::<Position>() else
        {
            return Err(2);
        };
        let Some(rotation) = entity_ref.get::<Rotation>() else
        {
            return Err(2);
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

pub fn count_world_invalid_transforms(world: &AvianRagdollWorld) -> usize
{
    let mut invalid_count: usize = 0;
    for entity in &world.dynamic_entities
    {
        let Ok(entity_ref) = world.app.world().get_entity(*entity) else
        {
            invalid_count += 1;
            continue;
        };
        let Some(position) = entity_ref.get::<Position>() else
        {
            invalid_count += 1;
            continue;
        };
        let Some(rotation) = entity_ref.get::<Rotation>() else
        {
            invalid_count += 1;
            continue;
        };
        if !position.x.is_finite()
            || !position.y.is_finite()
            || !position.z.is_finite()
            || !rotation.x.is_finite()
            || !rotation.y.is_finite()
            || !rotation.z.is_finite()
            || !rotation.w.is_finite()
        {
            invalid_count += 1;
        }
    }
    invalid_count
}

pub fn sample_visual_transforms(
    state: &case_registry::CaseView,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    let case_registry::CaseView::RagdollStairTumble(world) = state else
    {
        return Err(2);
    };
    sample_transforms(world, transforms)
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum GeometryMatch
{
    Different,
    Equal,
}

pub fn same_geometry(left: CaseExecutionRagdollPart, right: CaseExecutionRagdollPart) -> GeometryMatch
{
    if left.shape == right.shape
        && left.radius == right.radius
        && left.half_segment == right.half_segment
        && left.axis == right.axis
        && left.half_extents.x == right.half_extents.x
        && left.half_extents.y == right.half_extents.y
        && left.half_extents.z == right.half_extents.z
    {
        GeometryMatch::Equal
    }
    else
    {
        GeometryMatch::Different
    }
}

pub fn geometry_index(fixture: CaseExecutionRagdoll, part_index: usize) -> u32
{
    let mut result: u32 = 0;
    for index in 0..part_index
    {
        if same_geometry(fixture.parts[index], fixture.parts[part_index]) == GeometryMatch::Equal
        {
            return geometry_index(fixture, index);
        }
        if (0..index).all(|prior|
            same_geometry(fixture.parts[prior], fixture.parts[index]) == GeometryMatch::Different)
        {
            result += 1;
        }
    }
    result
}

pub fn build_visual_scene(
    state: &case_registry::CaseView,
    geometries: &mut [case_registry::VisualGeometry],
    meshes: &mut case_registry::VisualMeshStorage,
    instances: &mut [case_registry::VisualInstance],
) -> Result<(usize, usize), i32>
{
    let case_registry::CaseView::RagdollStairTumble(world) = state else
    {
        return Err(2);
    };
    let execution: CaseExecutionSpec = world.execution;
    let fixture: crate::case_execution_wire::CaseExecutionRagdoll = execution.ragdoll;
    let mut part_geometry_count: usize = 0usize;
    for part_index in 0..fixture.part_count as usize
    {
        let index: usize = geometry_index(fixture, part_index) as usize;
        if index < part_geometry_count
        {
            continue;
        }
        if index != part_geometry_count
        {
            return Err(2);
        }
        part_geometry_count += 1;
    }
    let total_geometry_count: usize = part_geometry_count + 2;
    if geometries.len() < total_geometry_count ||
        instances.len() < execution.visual_instance_count as usize
    {
        return Err(2);
    }
    for part_index in 0..fixture.part_count as usize
    {
        let part: crate::case_execution_wire::CaseExecutionRagdollPart = fixture.parts[part_index];
        if (0..part_index).any(|prior| same_geometry(fixture.parts[prior], part) == GeometryMatch::Equal)
        {
            continue;
        }
        let index: usize = geometry_index(fixture, part_index) as usize;
        geometries[index] = case_registry::build_resolved_visual_geometry(&execution,
            &crate::case_execution_wire::part_geometry(&part), meshes)?;
    }
    geometries[part_geometry_count] = case_registry::VisualGeometry
    {
        kind: 2,
        parameter_x: fixture.stair_half_width,
        parameter_y: fixture.stair_half_height,
        parameter_z: fixture.stair_half_depth,
        ..case_registry::VisualGeometry::default()
    };
    let floor: crate::case_execution_wire::CaseExecutionBox = fixture.extra_static_boxes[0];
    geometries[part_geometry_count + 1] = case_registry::VisualGeometry
    {
        kind: 2,
        parameter_x: floor.half_extents.x,
        parameter_y: floor.half_extents.y,
        parameter_z: floor.half_extents.z,
        ..case_registry::VisualGeometry::default()
    };
    for (index, entity) in world.dynamic_entities.iter().enumerate()
    {
        let entity_ref: bevy::ecs::world::EntityRef<'_> = world.app.world().entity(*entity);
        let Some(position) = entity_ref.get::<Position>() else
        {
            return Err(2);
        };
        let Some(rotation) = entity_ref.get::<Rotation>() else
        {
            return Err(2);
        };
        instances[index] = case_registry::VisualInstance
        {
            geometry_index: geometry_index(fixture, index % fixture.part_count as usize),
            stable_slot: index as u32,
            transform_slot: index as u32,
            initial_transform: case_registry::VisualTransform
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
    for row in 0..fixture.stair_count as usize
    {
        let stable_slot: usize = execution.dynamic_body_count as usize + row;
        instances[stable_slot] = case_registry::VisualInstance
        {
            geometry_index: part_geometry_count as u32,
            stable_slot: stable_slot as u32,
            transform_slot: u32::MAX,
            initial_transform: case_registry::VisualTransform
            {
                position_x: 0.0,
                position_y: (fixture.stair_count as usize - 1 - row) as f32 *
                    fixture.stair_rise - fixture.stair_half_height,
                position_z: (row as f32 - 0.5 * (fixture.stair_count - 1) as f32) *
                    fixture.stair_depth,
                rotation_x: 0.0,
                rotation_y: 0.0,
                rotation_z: 0.0,
                rotation_w: 1.0,
            },
        };
    }
    let floor_slot: usize = execution.visual_instance_count as usize - 1;
    instances[floor_slot] = case_registry::VisualInstance
    {
        geometry_index: (part_geometry_count + 1) as u32,
        stable_slot: floor_slot as u32,
        transform_slot: u32::MAX,
        initial_transform: case_registry::VisualTransform
        {
            position_x: floor.center.x,
            position_y: floor.center.y,
            position_z: floor.center.z,
            rotation_x: 0.0,
            rotation_y: 0.0,
            rotation_z: 0.0,
            rotation_w: 1.0,
        },
    };
    Ok((total_geometry_count, execution.visual_instance_count as usize))
}

pub fn build_visual_debug_primitives(
    state: &case_registry::CaseView,
    primitives: &mut [case_registry::VisualDebugPrimitive],
) -> Result<(), i32>
{
    let case_registry::CaseView::RagdollStairTumble(world) = state else
    {
        return Err(2);
    };
    let execution: CaseExecutionSpec = world.execution;
    let fixture: crate::case_execution_wire::CaseExecutionRagdoll = execution.ragdoll;
    let debug_count: usize = execution.visual_debug_primitive_count as usize;
    if primitives.len() < debug_count || debug_count > fixture.extra_static_box_count as usize
    {
        return Err(2);
    }
    let first: usize = fixture.extra_static_box_count as usize - debug_count;
    for (index, primitive) in primitives.iter_mut().take(debug_count).enumerate()
    {
        let static_box: crate::case_execution_wire::CaseExecutionBox = fixture.extra_static_boxes[first + index];
        *primitive = case_registry::VisualDebugPrimitive
        {
            kind: 2,
            material_index: 6,
            origin_or_center_x: static_box.center.x,
            origin_or_center_y: static_box.center.y,
            origin_or_center_z: static_box.center.z,
            end_or_half_extents_x: static_box.half_extents.x,
            end_or_half_extents_y: static_box.half_extents.y,
            end_or_half_extents_z: static_box.half_extents.z,
            radius: 0.0,
            reserved: 0,
        };
    }
    Ok(())
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
    let linked: &str = if execution.ragdoll.linked_collision_mode == CaseExecutionToggle::Enabled
    {
        "enabled"
    }
    else
    {
        "disabled"
    };
    format!(
        "substeps={native_count}; sleep={sleep}; ccd={ccd}; linked_collision={linked}; solver_defaults=avian; worker_count={thread_count}"
    )
}

pub fn run_headless(
    args: &runner_args::RunnerArgs,
    effective_thread_count: usize,
) -> Result<(), i32>
{
    let execution: CaseExecutionSpec = args.case_execution;
    if args.warmup_steps != execution.warmup_work_unit_count as usize
        || args.step_count != execution.measured_work_unit_count as usize
    {
        return Err(2);
    }
    let mut warmup_world: AvianRagdollWorld = create_world(&execution)?;
    step_world(&mut warmup_world, args.warmup_steps);
    drop(warmup_world);

    let mut world: AvianRagdollWorld = create_world(&execution)?;
    let mut recording: Option<crate::replay_recording::RecordingWriter> = match args.recording_mode
    {
        crate::runner_args::RecordingMode::On => Some(crate::replay_recording::begin_recording(args, &case_registry::CaseView::RagdollStairTumble(&mut world))?),
        crate::runner_args::RecordingMode::Off => None,
    };
    for ordinal in 1..=args.step_count
    {
        match args.verification_mode
        {
            runner_args::VerificationMode::On => step_world_quality(&mut world, 1)?,
            runner_args::VerificationMode::Off => step_world(&mut world, 1),
        }
        if let Some(writer) = recording.as_mut()
        {
            crate::replay_recording::append_frame(writer, args,
                &case_registry::CaseView::RagdollStairTumble(&mut world), ordinal as u64)?;
        }
    }
    if let Some(writer) = recording
    {
        crate::replay_recording::complete_recording(writer)?;
    }
    let observations: Vec<result_writer::ObservationRow> = match args.verification_mode
    {
        runner_args::VerificationMode::Off => Vec::new(),
        runner_args::VerificationMode::On =>
        {
            let values: [u64; 10] = ragdoll_quality::values(&world.quality)?;
             (0..10).map(|index: usize| result_writer::ObservationRow
            {
                metric_id: ragdoll_quality::IDS[index],
                phase_id: "final",
                sample_index: world.completed_step_count as u32,
                value: if index < 2
                {
                    result_writer::ObservationValue::Float64(values[index])
                }
                else
                {
                    result_writer::ObservationValue::Uint64(values[index])
                },
            }).collect()
        }
    };
    let invalid_transform_count: usize = count_world_invalid_transforms(&world);
    let case_validity: result_writer::ResultValidity = if world.dynamic_entities.len() == execution.dynamic_body_count as usize
        && world.body_count == execution.body_count as usize
        && world.shape_count == execution.shape_count as usize
        && world.constraint_count == execution.constraint_count as usize
    {
        result_writer::ResultValidity::Valid
    }
    else
    {
        result_writer::ResultValidity::Invalid
    };
    let metric_validity: result_writer::ResultValidity = if world.completed_step_count == execution.measured_work_unit_count as usize
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
        query_count: 0,
        constraint_count: world.constraint_count,
        invalid_transform_count,
        effective_thread_count,
        effective_worker_count: effective_thread_count,
        completed_work_unit_count: world.completed_step_count,
        workload_elapsed_ms: f64::NAN,
        case_validity,
        metric_validity,
        observations,
    };
    result_writer::write_result(args, &result)
}
