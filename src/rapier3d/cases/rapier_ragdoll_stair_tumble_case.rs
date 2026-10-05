use crate::case_execution_wire::{
    CaseExecutionRagdoll, CaseExecutionRagdollPart,
    CaseExecutionSpec, CaseExecutionToggle,
};
use crate::{case_registry, result_writer, runner_args};
use rapier3d::prelude::*;

#[path = "../../common/ragdoll_quality.rs"]
pub mod ragdoll_quality;

pub const DESCRIPTOR: case_registry::RapierCaseDescriptor =
    case_registry::RapierCaseDescriptor
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

pub struct RapierRagdollWorld
{
    pub execution: CaseExecutionSpec,
    pub quality: ragdoll_quality::Quality,
    pub completed_step_count: usize,
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
    pub dynamic_handles: Box<[RigidBodyHandle]>,
    pub joint_handles: Box<[ImpulseJointHandle]>,
}

pub fn ragdoll_rotation(fixture: CaseExecutionRagdoll, ragdoll_index: usize) -> Rotation
{
    let yaw: f32 = fixture.yaw_pattern_degrees[
        ragdoll_index % fixture.yaw_pattern_count as usize].to_radians();
    Rotation::from_rotation_y(yaw) *
        Rotation::from_rotation_x(fixture.pitch_degrees.to_radians())
}

pub fn ragdoll_base(fixture: CaseExecutionRagdoll, row: usize, column: usize) -> Vector
{
    Vector::new(
        (column as f32 - 0.5 * (fixture.ragdoll_grid[1] - 1) as f32) *
            fixture.column_spacing,
        (fixture.stair_count as usize - 1 - row) as f32 * fixture.stair_rise +
            fixture.base_height_offset,
        (row as f32 - 0.5 * (fixture.stair_count - 1) as f32) *
            fixture.row_spacing,
    )
}

pub fn add_static_box(
    rigid_body_set: &mut RigidBodySet,
    collider_set: &mut ColliderSet,
    translation: Vector,
    half_extents: Vector,
    execution: &CaseExecutionSpec,
)
{
    let body_handle: RigidBodyHandle = rigid_body_set.insert(
        RigidBodyBuilder::fixed().translation(translation).build());
    let collider: Collider = ColliderBuilder::cuboid(
        half_extents.x, half_extents.y, half_extents.z)
        .friction(execution.friction)
        .restitution(execution.restitution)
        .build();
    collider_set.insert_with_parent(collider, body_handle, rigid_body_set);
}

pub fn create_world(execution: &CaseExecutionSpec) -> Result<RapierRagdollWorld, i32>
{
    let fixture: crate::case_execution_wire::CaseExecutionRagdoll = execution.ragdoll;
    let mut part_shapes: Vec<SharedShape> = Vec::with_capacity(fixture.part_count as usize);
    for part_index in 0..fixture.part_count as usize
    {
        let geometry: crate::case_execution_wire::CaseExecutionGeometry = crate::case_execution_wire::part_geometry(&fixture.parts[part_index]);
        let mut prior: usize = 0;
        while prior < part_index && crate::case_execution_wire::same_geometry(geometry, crate::case_execution_wire::part_geometry(&fixture.parts[prior])) == 0
        {
            prior += 1;
        }
        let shape: SharedShape = if prior < part_index
        {
            part_shapes[prior].clone()
        }
        else
        {
            crate::case_registry::create_resolved_shape(geometry, execution)?
        };
        part_shapes.push(shape);
    }
    let mut rigid_body_set: RigidBodySet = RigidBodySet::with_capacity(execution.body_count as usize);
    let mut collider_set: ColliderSet = ColliderSet::with_capacity(execution.shape_count as usize);
    let mut impulse_joint_set: ImpulseJointSet = ImpulseJointSet::new();
    let mut dynamic_handles: Vec<RigidBodyHandle> = Vec::with_capacity(execution.dynamic_body_count as usize);
    let mut joint_handles: Vec<ImpulseJointHandle> = Vec::with_capacity(execution.constraint_count as usize);

    for row in 0..fixture.stair_count as usize
    {
        add_static_box(
            &mut rigid_body_set,
            &mut collider_set,
            Vector::new(
                0.0,
                (fixture.stair_count as usize - 1 - row) as f32 * fixture.stair_rise -
                    fixture.stair_half_height,
                (row as f32 - 0.5 * (fixture.stair_count - 1) as f32) *
                    fixture.stair_depth,
            ),
            Vector::new(fixture.stair_half_width, fixture.stair_half_height,
                fixture.stair_half_depth),
            execution,
        );
    }
    for static_box in fixture.extra_static_boxes
        .iter().take(fixture.extra_static_box_count as usize)
    {
        add_static_box(
            &mut rigid_body_set,
            &mut collider_set,
            Vector::new(static_box.center.x, static_box.center.y, static_box.center.z),
            Vector::new(static_box.half_extents.x, static_box.half_extents.y,
                static_box.half_extents.z),
            execution,
        );
    }

    for row in 0..fixture.ragdoll_grid[0] as usize
    {
        for column in 0..fixture.ragdoll_grid[1] as usize
        {
            let ragdoll_index: usize = row * fixture.ragdoll_grid[1] as usize + column;
            let rotation: Rotation = ragdoll_rotation(fixture, ragdoll_index);
            let base: Vector = ragdoll_base(fixture, row, column);
            for (part_index, part) in fixture.parts.iter().take(fixture.part_count as usize).enumerate()
            {
                let local_center: Vector = Vector::new(part.center.x, part.center.y, part.center.z);
                let pose: Pose = Pose::from_parts(base + rotation * local_center,
                    rotation * crate::case_registry::shape_rotation(part.axis));
                let body_handle: RigidBodyHandle = rigid_body_set.insert(
                    RigidBodyBuilder::dynamic()
                        .pose(pose)
                        .linvel(Vector::new(
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
                        ))
                        .angvel(Vector::ZERO)
                        .linear_damping(fixture.linear_damping)
                        .angular_damping(fixture.angular_damping)
                        .can_sleep(execution.sleep_mode == CaseExecutionToggle::Enabled)
                        .ccd_enabled(execution.continuous_collision_mode == CaseExecutionToggle::Enabled)
                        .build(),
                );
                let collider: Collider = ColliderBuilder::new(part_shapes[part_index].clone())
                .friction(execution.friction)
                .restitution(execution.restitution)
                .mass(fixture.part_mass)
                .build();
                collider_set.insert_with_parent(
                    collider, body_handle, &mut rigid_body_set);
                dynamic_handles.push(body_handle);
            }
        }
    }

    let ragdoll_count: usize = (fixture.ragdoll_grid[0] * fixture.ragdoll_grid[1]) as usize;
    for ragdoll_index in 0..ragdoll_count
    {
        for link in fixture.links.iter().take(fixture.link_count as usize)
        {
            let parent_part: usize = link.parent_part as usize;
            let child_part: usize = link.child_part as usize;
            let joint: SphericalJointBuilder = SphericalJointBuilder::new()
                .local_anchor1(Vector::new(
                    link.parent_local_anchor.x,
                    link.parent_local_anchor.y,
                    link.parent_local_anchor.z,
                ))
                .local_anchor2(Vector::new(
                    link.child_local_anchor.x,
                    link.child_local_anchor.y,
                    link.child_local_anchor.z,
                ))
                .contacts_enabled(fixture.linked_collision_mode == CaseExecutionToggle::Enabled);
            let handle: ImpulseJointHandle = impulse_joint_set.insert(
                dynamic_handles[ragdoll_index * fixture.part_count as usize + parent_part],
                dynamic_handles[ragdoll_index * fixture.part_count as usize + child_part],
                joint,
                true,
            );
            joint_handles.push(handle);
        }
    }

    if rigid_body_set.len() != execution.body_count as usize
        || collider_set.len() != execution.shape_count as usize
        || dynamic_handles.len() != execution.dynamic_body_count as usize
        || impulse_joint_set.len() != execution.constraint_count as usize
        || joint_handles.len() != execution.constraint_count as usize
    {
        eprintln!(
            "invalid_result body_count={} shape_count={} dynamic_body_count={} constraint_count={}",
            rigid_body_set.len(),
            collider_set.len(),
            dynamic_handles.len(),
            impulse_joint_set.len()
        );
        return Err(2);
    }

    Ok(RapierRagdollWorld
    {
        execution: *execution,
        quality: ragdoll_quality::Quality::default(),
        completed_step_count: 0,
        rigid_body_set,
        collider_set,
        physics_pipeline: PhysicsPipeline::new(),
        island_manager: IslandManager::new(),
        broad_phase: DefaultBroadPhase::new(),
        narrow_phase: NarrowPhase::new(),
        impulse_joint_set,
        multibody_joint_set: MultibodyJointSet::new(),
        soft_body_set: SoftBodySet::new(),
        ccd_solver: CCDSolver::new(),
        dynamic_handles: dynamic_handles.into_boxed_slice(),
        joint_handles: joint_handles.into_boxed_slice(),
    })
}

pub fn step_world(world: &mut RapierRagdollWorld, step_count: usize)
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
    for _ in 0..step_count
    {
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
        world.completed_step_count += 1;
    }
}

pub fn step_world_quality(world: &mut RapierRagdollWorld, step_count: usize) -> Result<(), i32>
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
            let body: &RigidBody = world.rigid_body_set.get(world.dynamic_handles[index])?;
            let position: Vector = body.translation();
            let rotation: Rotation = *body.rotation();
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
    world: &RapierRagdollWorld,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    if transforms.len() < world.execution.dynamic_body_count as usize
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

pub fn count_world_invalid_transforms(world: &RapierRagdollWorld) -> usize
{
    world.dynamic_handles.iter().filter(|handle|
    {
        let Some(body) = world.rigid_body_set.get(**handle) else
        {
            return true;
        };
        let position: Vector = body.translation();
        let rotation: &Rotation = body.rotation();
        !position.x.is_finite()
            || !position.y.is_finite()
            || !position.z.is_finite()
            || !rotation.x.is_finite()
            || !rotation.y.is_finite()
            || !rotation.z.is_finite()
            || !rotation.w.is_finite()
    }).count()
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
    for (index, handle) in world.dynamic_handles.iter().enumerate()
    {
        let body: &RigidBody = &world.rigid_body_set[*handle];
        let position: Vector = body.translation();
        let rotation: &Rotation = body.rotation();
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
    format!(
        "solver_iterations={native_count}; sleep={sleep}; ccd={ccd}; worker_count={thread_count}"
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
    let mut warmup_world: RapierRagdollWorld = create_world(&execution)?;
    step_world(&mut warmup_world, args.warmup_steps);
    drop(warmup_world);

    let mut world: RapierRagdollWorld = create_world(&execution)?;
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
    let case_validity: result_writer::ResultValidity = if world.dynamic_handles.len() == execution.dynamic_body_count as usize
        && world.rigid_body_set.len() == execution.body_count as usize
        && world.collider_set.len() == execution.shape_count as usize
        && world.impulse_joint_set.len() == execution.constraint_count as usize
        && world.joint_handles.len() == execution.constraint_count as usize
    {
        result_writer::ResultValidity::Valid
    }
    else
    {
        result_writer::ResultValidity::Invalid
    };
    let metric_validity: result_writer::ResultValidity = if args.step_count == execution.measured_work_unit_count as usize
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
        query_count: 0,
        constraint_count: world.impulse_joint_set.len(),
        invalid_transform_count,
        effective_thread_count,
        effective_worker_count: effective_thread_count,
        completed_work_unit_count: args.step_count,
        workload_elapsed_ms: f64::NAN,
        case_validity,
        metric_validity,
        observations,
    };
    result_writer::write_result(args, &result)
}
