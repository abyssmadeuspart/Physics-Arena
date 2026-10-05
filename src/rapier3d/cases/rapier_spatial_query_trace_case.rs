use crate::case_execution_wire::CaseExecutionSpec;
use crate::{case_registry, result_writer, runner_args};
use rapier3d::{
    parry::{bounding_volume::Aabb, query::ShapeCastOptions, shape::Ball},
    prelude::*,
};
use rapier3d::rayon::prelude::*;
use std::time::{Duration, Instant};

pub const DESCRIPTOR: case_registry::RapierCaseDescriptor =
    case_registry::RapierCaseDescriptor
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
pub struct SpatialQuery
{
    pub origin_or_center: Vector,
    pub direction: Vector,
}

#[derive(Clone, Copy)]
pub struct RapierSphereCastInput
{
    pub pose: Pose,
    pub direction: Vector,
}

pub struct RapierSpatialQueryWorld
{
    pub execution: CaseExecutionSpec,
    pub rigid_body_set: RigidBodySet,
    pub collider_set: ColliderSet,
    pub broad_phase: BroadPhaseBvh,
    pub narrow_phase: NarrowPhase,
    pub sphere: Ball,
    pub cast_options: ShapeCastOptions,
    pub ray_inputs: Box<[Ray]>,
    pub sphere_cast_inputs: Box<[RapierSphereCastInput]>,
    pub overlap_inputs: Box<[Aabb]>,
    pub lane_hit_counts: Box<[u64]>,
    pub debug_hits: Box<[u8]>,
    pub debug_hit_distances: Box<[f32]>,
    pub ray_elapsed: Duration,
    pub sphere_cast_elapsed: Duration,
    pub overlap_elapsed: Duration,
    pub workload_elapsed: Duration,
    pub latest_batch_elapsed: Duration,
    pub ray_hit_count: u64,
    pub sphere_cast_hit_count: u64,
    pub overlap_hit_count: u64,
    pub thread_count: usize,
    pub completed_batch_count: usize,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum SpatialQueryBatchPhase
{
    Warmup,
    Measured,
}

pub fn sample_visual_transforms(
    state: &case_registry::CaseView,
    transforms: &mut [case_registry::VisualStableTransform],
) -> Result<(), i32>
{
    if matches!(state, case_registry::CaseView::SpatialQuery(_)) && transforms.is_empty()
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
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = execution.spatial_query;
    format!(
        "query_world=static_only; worker_count={thread_count}; rays={}; \
         sphere_casts={}; overlaps={}",
        fixture.ray_count, fixture.sphere_cast_count, fixture.overlap_count
    )
}

pub fn centered_grid_coordinate(base: f32, spacing: f32, coordinate: usize, count: usize) -> f32
{
    base + spacing * (coordinate as f32 - 0.5 * (count as f32 - 1.0))
}

pub fn uncentered_grid_coordinate(base: f32, spacing: f32, coordinate: usize) -> f32
{
    base + spacing * coordinate as f32
}

pub fn create_world(
    execution: &CaseExecutionSpec,
    thread_count: usize,
    recording_mode: crate::runner_args::RecordingMode,
) -> Result<RapierSpatialQueryWorld, i32>
{
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = execution.spatial_query;
    if thread_count == 0
        || (fixture.ray_count as usize) < thread_count
        || (fixture.sphere_cast_count as usize) < thread_count
        || (fixture.overlap_count as usize) < thread_count
    {
        return Err(2);
    }
    let static_body_count: usize = execution.static_body_count as usize;
    let shape: SharedShape = crate::case_registry::create_resolved_shape(execution.selected_geometry, execution)?;
    let rotation: Rotation = crate::case_registry::shape_rotation(execution.selected_geometry.axis);
    let mut rigid_body_set: RigidBodySet = RigidBodySet::with_capacity(static_body_count);
    let mut collider_set: ColliderSet = ColliderSet::with_capacity(execution.shape_count as usize);
    let mut collider_handles: Vec<ColliderHandle> = Vec::with_capacity(execution.shape_count as usize);
    for iy in 0..fixture.static_grid[1] as usize
    {
        for iz in 0..fixture.static_grid[2] as usize
        {
            for ix in 0..fixture.static_grid[0] as usize
            {
                let translation: Vector = Vector::new(
                    centered_grid_coordinate(fixture.static_base_center.x, fixture.static_spacing.x,
                        ix, fixture.static_grid[0] as usize),
                    uncentered_grid_coordinate(fixture.static_base_center.y,
                        fixture.static_spacing.y, iy),
                    centered_grid_coordinate(fixture.static_base_center.z, fixture.static_spacing.z,
                        iz, fixture.static_grid[2] as usize),
                );
                let body: RigidBody = RigidBodyBuilder::fixed().pose(Pose::from_parts(translation, rotation)).build();
                let body_handle: RigidBodyHandle = rigid_body_set.insert(body);
                let collider: Collider = ColliderBuilder::new(shape.clone())
                    .friction(execution.friction)
                    .restitution(execution.restitution)
                    .build();
                let collider_handle: ColliderHandle =
                    collider_set.insert_with_parent(collider, body_handle, &mut rigid_body_set);
                collider_handles.push(collider_handle);
            }
        }
    }
    if rigid_body_set.len() != static_body_count || collider_set.len() != execution.shape_count as usize
    {
        return Err(2);
    }
    let mut broad_phase: BroadPhaseBvh = BroadPhaseBvh::new();
    let params: IntegrationParameters = IntegrationParameters::default();
    let mut broad_phase_events: Vec<rapier3d::geometry::BroadPhasePairEvent> = Vec::new();
    broad_phase.update(
        &params,
        &collider_set,
        &rigid_body_set,
        &collider_handles,
        &[],
        &mut broad_phase_events,
    );
    let ray_inputs: Box<[Ray]> = (0..fixture.ray_count as usize)
        .map(|index|
        {
            let query: SpatialQuery = generate_query(execution, index);
            Ray::new(query.origin_or_center, query.direction)
        })
        .collect::<Box<[Ray]>>();
    let sphere_cast_inputs: Box<[RapierSphereCastInput]> = (0..fixture.sphere_cast_count as usize)
        .map(|index|
        {
            let query: SpatialQuery = generate_query(execution, fixture.ray_count as usize + index);
            RapierSphereCastInput
            {
                pose: Pose::translation(
                    query.origin_or_center.x,
                    query.origin_or_center.y,
                    query.origin_or_center.z,
                ),
                direction: query.direction,
            }
        })
        .collect::<Box<[RapierSphereCastInput]>>();
    let half_extent: Vector = Vector::new(fixture.overlap_half_extents.x,
        fixture.overlap_half_extents.y, fixture.overlap_half_extents.z);
    let overlap_inputs: Box<[Aabb]> = (0..fixture.overlap_count as usize)
        .map(|index|
        {
            let query: SpatialQuery = generate_query(execution,
                (fixture.ray_count + fixture.sphere_cast_count) as usize + index);
            Aabb::new(
                query.origin_or_center - half_extent,
                query.origin_or_center + half_extent,
            )
        })
        .collect::<Box<[Aabb]>>();
    let debug_count: usize = match recording_mode
    {
        crate::runner_args::RecordingMode::On => execution.visual_debug_primitive_count as usize,
        crate::runner_args::RecordingMode::Off => 0,
    };
    let debug_hits: Box<[u8]> = vec![0u8; debug_count].into_boxed_slice();
    let debug_hit_distances: Box<[f32]> = vec![fixture.query_distance; debug_count].into_boxed_slice();
    let mut world: RapierSpatialQueryWorld = RapierSpatialQueryWorld
    {
        execution: *execution,
        rigid_body_set,
        collider_set,
        broad_phase,
        narrow_phase: NarrowPhase::new(),
        sphere: Ball::new(fixture.sphere_cast_radius),
        cast_options: ShapeCastOptions::with_max_time_of_impact(fixture.query_distance),
        ray_inputs,
        sphere_cast_inputs,
        overlap_inputs,
        lane_hit_counts: vec![0u64; thread_count].into_boxed_slice(),
        debug_hits,
        debug_hit_distances,
        ray_elapsed: Duration::ZERO,
        sphere_cast_elapsed: Duration::ZERO,
        overlap_elapsed: Duration::ZERO,
        workload_elapsed: Duration::ZERO,
        latest_batch_elapsed: Duration::ZERO,
        ray_hit_count: 0,
        sphere_cast_hit_count: 0,
        overlap_hit_count: 0,
        thread_count,
        completed_batch_count: 0,
    };
    if matches!(recording_mode, crate::runner_args::RecordingMode::On)
    {
        capture_debug_samples(&mut world);
    }
    Ok(world)
}

pub fn capture_debug_samples(world: &mut RapierSpatialQueryWorld)
{
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = world.execution.spatial_query;
    let samples: usize = fixture.debug_samples_per_family as usize;
    let pipeline: QueryPipeline<'_> = world.broad_phase.as_query_pipeline(
        world.narrow_phase.query_dispatcher(),
        &world.rigid_body_set,
        &world.collider_set,
        QueryFilter::default(),
    );
    for index in 0..samples
    {
        if let Some((_, distance)) =
            pipeline.cast_ray(&world.ray_inputs[index], fixture.query_distance, true)
        {
            world.debug_hits[index] = 1;
            world.debug_hit_distances[index] = distance;
        }
        else
        {
            world.debug_hits[index] = 0;
            world.debug_hit_distances[index] = fixture.query_distance;
        }
    }
    for index in 0..samples
    {
        let input: RapierSphereCastInput = world.sphere_cast_inputs[index];
        let debug_index: usize = samples + index;
        if let Some((_, hit)) = pipeline.cast_shape(
            &input.pose,
            input.direction,
            &world.sphere,
            world.cast_options,
        )
        {
            world.debug_hits[debug_index] = 1;
            world.debug_hit_distances[debug_index] = hit.time_of_impact;
        }
        else
        {
            world.debug_hits[debug_index] = 0;
            world.debug_hit_distances[debug_index] = fixture.query_distance;
        }
    }
    for index in 0..samples
    {
        let debug_index: usize = 2 * samples + index;
        world.debug_hits[debug_index] = pipeline
            .intersect_aabb_conservative(world.overlap_inputs[index])
            .next()
            .is_some() as u8;
        world.debug_hit_distances[debug_index] = fixture.query_distance;
    }
}

pub fn check_query_batch(state: &RapierSpatialQueryWorld, phase: &str, batch: usize) -> i32
{
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = state.execution.spatial_query;
    for family in 0..3
    {
        let count: u32 = match family
        {
            0 => fixture.ray_count,
            1 => fixture.sphere_cast_count,
            _ => fixture.overlap_count,
        };
        let expected: u64 = (count / 2 + count % 2) as u64;
        let actual: u64 = match family
        {
            0 => state.ray_hit_count,
            1 => state.sphere_cast_hit_count,
            _ => state.overlap_hit_count,
        };
        if actual != expected
        {
            let family_name: &str = match family
            {
                0 => "ray",
                1 => "sphere_cast",
                _ => "overlap",
            };
            eprintln!("run_failed reason=query_batch engine=rapier3d phase={phase} batch={batch} family={family_name} expected={expected} actual={actual}");
            return 2;
        }
    }
    0
}

pub fn warmup_world(world: &mut RapierSpatialQueryWorld, batch_count: usize) -> i32
{
    if batch_count != world.execution.warmup_work_unit_count as usize
    {
        return 2;
    }
    for batch in 0..batch_count
    {
        execute_batch(world, SpatialQueryBatchPhase::Warmup);
        if check_query_batch(world, "warmup", batch) != 0
        {
            return 2;
        }
    }
    0
}

pub fn step_world_timed(
    world: &mut RapierSpatialQueryWorld,
    durations: &mut [Duration],
) -> Result<(), i32>
{
    if durations.len() > world.execution.measured_work_unit_count as usize - world.completed_batch_count
    {
        return Err(2);
    }
    for duration in durations.iter_mut()
    {
        *duration = execute_batch(world, SpatialQueryBatchPhase::Measured);
        if check_query_batch(world, "measured", world.completed_batch_count) != 0
        {
            return Err(2);
        }
        world.latest_batch_elapsed = *duration;
        world.workload_elapsed += *duration;
        world.completed_batch_count += 1;
    }
    Ok(())
}

pub fn execute_batch(
    world: &mut RapierSpatialQueryWorld,
    phase: SpatialQueryBatchPhase,
) -> Duration
{
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = world.execution.spatial_query;
    let pipeline: QueryPipeline<'_> = world.broad_phase.as_query_pipeline(
        world.narrow_phase.query_dispatcher(),
        &world.rigid_body_set,
        &world.collider_set,
        QueryFilter::default(),
    );
    let batch_start: Instant = Instant::now();
    let ray_start: Instant = Instant::now();
    let mut ray_hit_count: u64 = 0u64;
    if world.thread_count == 1
    {
        for index in 0..fixture.ray_count as usize
        {
            if pipeline
                .cast_ray(&world.ray_inputs[index], fixture.query_distance, true)
                .is_some()
            {
                ray_hit_count += 1;
            }
        }
    }
    else
    {
        let thread_count: usize = world.thread_count;
        let ray_inputs: &Box<[Ray]> = &world.ray_inputs;
        let broad_phase: &BroadPhaseBvh = &world.broad_phase;
        let narrow_phase: &NarrowPhase = &world.narrow_phase;
        let rigid_body_set: &RigidBodySet = &world.rigid_body_set;
        let collider_set: &ColliderSet = &world.collider_set;
        world.lane_hit_counts
            .par_iter_mut()
            .enumerate()
            .for_each(|(lane_index, lane_hit_count)|
            {
                let start: usize = ray_inputs.len() * lane_index / thread_count;
                let end: usize = ray_inputs.len() * (lane_index + 1) / thread_count;
                let mut hit_count: u64 = 0u64;
                let lane_pipeline: QueryPipeline<'_> = broad_phase.as_query_pipeline(
                    narrow_phase.query_dispatcher(),
                    rigid_body_set,
                    collider_set,
                    QueryFilter::default(),
                );
                for input in &ray_inputs[start..end]
                {
                    hit_count += lane_pipeline
                        .cast_ray(input, fixture.query_distance, true)
                        .is_some() as u64;
                }
                *lane_hit_count = hit_count;
            });
        for lane_index in 0..thread_count
        {
            ray_hit_count += world.lane_hit_counts[lane_index];
        }
    }
    world.ray_hit_count = ray_hit_count;
    let ray_elapsed: Duration = ray_start.elapsed();

    let cast_start: Instant = Instant::now();
    let mut sphere_cast_hit_count: u64 = 0u64;
    if world.thread_count == 1
    {
        for index in 0..fixture.sphere_cast_count as usize
        {
            let input: RapierSphereCastInput = world.sphere_cast_inputs[index];
            if pipeline
                .cast_shape(
                    &input.pose,
                    input.direction,
                    &world.sphere,
                    world.cast_options,
                )
                .is_some()
            {
                sphere_cast_hit_count += 1;
            }
        }
    }
    else
    {
        let thread_count: usize = world.thread_count;
        let sphere_cast_inputs: &Box<[RapierSphereCastInput]> = &world.sphere_cast_inputs;
        let sphere: &Ball = &world.sphere;
        let cast_options: ShapeCastOptions = world.cast_options;
        let broad_phase: &BroadPhaseBvh = &world.broad_phase;
        let narrow_phase: &NarrowPhase = &world.narrow_phase;
        let rigid_body_set: &RigidBodySet = &world.rigid_body_set;
        let collider_set: &ColliderSet = &world.collider_set;
        world.lane_hit_counts
            .par_iter_mut()
            .enumerate()
            .for_each(|(lane_index, lane_hit_count)|
            {
                let start: usize = sphere_cast_inputs.len() * lane_index / thread_count;
                let end: usize = sphere_cast_inputs.len() * (lane_index + 1) / thread_count;
                let mut hit_count: u64 = 0u64;
                let lane_pipeline: QueryPipeline<'_> = broad_phase.as_query_pipeline(
                    narrow_phase.query_dispatcher(),
                    rigid_body_set,
                    collider_set,
                    QueryFilter::default(),
                );
                for input in &sphere_cast_inputs[start..end]
                {
                    hit_count += lane_pipeline
                        .cast_shape(&input.pose, input.direction, sphere, cast_options)
                        .is_some() as u64;
                }
                *lane_hit_count = hit_count;
            });
        for lane_index in 0..thread_count
        {
            sphere_cast_hit_count += world.lane_hit_counts[lane_index];
        }
    }
    world.sphere_cast_hit_count = sphere_cast_hit_count;
    let sphere_cast_elapsed: Duration = cast_start.elapsed();

    let overlap_start: Instant = Instant::now();
    let mut overlap_hit_count: u64 = 0u64;
    if world.thread_count == 1
    {
        for index in 0..fixture.overlap_count as usize
        {
            let hit: bool = pipeline
                .intersect_aabb_conservative(world.overlap_inputs[index])
                .next()
                .is_some();
            overlap_hit_count += hit as u64;
        }
    }
    else
    {
        let thread_count: usize = world.thread_count;
        let overlap_inputs: &Box<[Aabb]> = &world.overlap_inputs;
        let broad_phase: &BroadPhaseBvh = &world.broad_phase;
        let narrow_phase: &NarrowPhase = &world.narrow_phase;
        let rigid_body_set: &RigidBodySet = &world.rigid_body_set;
        let collider_set: &ColliderSet = &world.collider_set;
        world.lane_hit_counts
            .par_iter_mut()
            .enumerate()
            .for_each(|(lane_index, lane_hit_count)|
            {
                let start: usize = overlap_inputs.len() * lane_index / thread_count;
                let end: usize = overlap_inputs.len() * (lane_index + 1) / thread_count;
                let mut hit_count: u64 = 0u64;
                let lane_pipeline: QueryPipeline<'_> = broad_phase.as_query_pipeline(
                    narrow_phase.query_dispatcher(),
                    rigid_body_set,
                    collider_set,
                    QueryFilter::default(),
                );
                for input in &overlap_inputs[start..end]
                {
                    hit_count += lane_pipeline
                        .intersect_aabb_conservative(*input)
                        .next()
                        .is_some() as u64;
                }
                *lane_hit_count = hit_count;
            });
        for lane_index in 0..thread_count
        {
            overlap_hit_count += world.lane_hit_counts[lane_index];
        }
    }
    world.overlap_hit_count = overlap_hit_count;
    let overlap_elapsed: Duration = overlap_start.elapsed();

    if phase == SpatialQueryBatchPhase::Measured
    {
        world.ray_elapsed += ray_elapsed;
        world.sphere_cast_elapsed += sphere_cast_elapsed;
        world.overlap_elapsed += overlap_elapsed;
    }
    batch_start.elapsed()
}

pub fn build_visual_scene(
    state: &case_registry::CaseView,
    geometries: &mut [case_registry::VisualGeometry],
    meshes: &mut case_registry::VisualMeshStorage,
    instances: &mut [case_registry::VisualInstance],
) -> Result<(usize, usize), i32>
{
    let case_registry::CaseView::SpatialQuery(world) = state else
    {
        return Err(2);
    };
    let execution: CaseExecutionSpec = world.execution;
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = execution.spatial_query;
    if geometries.is_empty() || instances.len() < execution.static_body_count as usize
    {
        return Err(2);
    }
    geometries[0] = case_registry::build_resolved_visual_geometry(&execution,
        &execution.selected_geometry, meshes)?;
    let rotation: Rotation = case_registry::shape_rotation(execution.selected_geometry.axis);
    for iy in 0..fixture.static_grid[1] as usize
    {
        for iz in 0..fixture.static_grid[2] as usize
        {
            for ix in 0..fixture.static_grid[0] as usize
            {
                let slot: usize = (iy * fixture.static_grid[2] as usize + iz) *
                    fixture.static_grid[0] as usize + ix;
                instances[slot] = case_registry::VisualInstance
                {
                    geometry_index: 0,
                    stable_slot: slot as u32,
                    transform_slot: u32::MAX,
                    initial_transform: case_registry::VisualTransform
                    {
                        position_x: centered_grid_coordinate(fixture.static_base_center.x,
                            fixture.static_spacing.x, ix, fixture.static_grid[0] as usize),
                        position_y: uncentered_grid_coordinate(fixture.static_base_center.y,
                            fixture.static_spacing.y, iy),
                        position_z: centered_grid_coordinate(fixture.static_base_center.z,
                            fixture.static_spacing.z, iz, fixture.static_grid[2] as usize),
                        rotation_x: rotation.x,
                        rotation_y: rotation.y,
                        rotation_z: rotation.z,
                        rotation_w: rotation.w,
                    },
                };
            }
        }
    }
    Ok((1, execution.static_body_count as usize))
}

pub fn build_visual_debug_primitives(
    state: &case_registry::CaseView,
    primitives: &mut [case_registry::VisualDebugPrimitive],
) -> Result<(), i32>
{
    let case_registry::CaseView::SpatialQuery(world) = state else
    {
        return Err(2);
    };
    let execution: CaseExecutionSpec = world.execution;
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = execution.spatial_query;
    let samples: usize = fixture.debug_samples_per_family as usize;
    if primitives.len() < execution.visual_debug_primitive_count as usize
    {
        return Err(2);
    }
    for local in 0..samples
    {
        for family in 0..3
        {
            let primitive_index: usize = family * samples + local;
            let mut primitive: case_registry::VisualDebugPrimitive = case_registry::VisualDebugPrimitive
            {
                kind: family as u32,
                material_index: if world.debug_hits[primitive_index] != 0
                {
                    6
                }
                else
                {
                    7
                },
                origin_or_center_x: 0.0,
                origin_or_center_y: 0.0,
                origin_or_center_z: 0.0,
                end_or_half_extents_x: 0.0,
                end_or_half_extents_y: 0.0,
                end_or_half_extents_z: 0.0,
                radius: 0.0,
                reserved: 0,
            };
            if family == 0
            {
                let input: Ray = world.ray_inputs[local];
                primitive.origin_or_center_x = input.origin.x;
                primitive.origin_or_center_y = input.origin.y;
                primitive.origin_or_center_z = input.origin.z;
                let end: Vector = input.origin +
                    input.dir * world.debug_hit_distances[primitive_index];
                primitive.end_or_half_extents_x = end.x;
                primitive.end_or_half_extents_y = end.y;
                primitive.end_or_half_extents_z = end.z;
            }
            else if family == 1
            {
                let input: RapierSphereCastInput = world.sphere_cast_inputs[local];
                let origin: Vector = input.pose.translation;
                primitive.origin_or_center_x = origin.x;
                primitive.origin_or_center_y = origin.y;
                primitive.origin_or_center_z = origin.z;
                let end: Vector = origin + input.direction * world.debug_hit_distances[primitive_index];
                primitive.end_or_half_extents_x = end.x;
                primitive.end_or_half_extents_y = end.y;
                primitive.end_or_half_extents_z = end.z;
                primitive.radius = fixture.sphere_cast_radius;
            }
            else
            {
                let bounds: Aabb = world.overlap_inputs[local];
                let center: Vector = (bounds.mins + bounds.maxs) * 0.5;
                primitive.origin_or_center_x = center.x;
                primitive.origin_or_center_y = center.y;
                primitive.origin_or_center_z = center.z;
                primitive.end_or_half_extents_x = fixture.overlap_half_extents.x;
                primitive.end_or_half_extents_y = fixture.overlap_half_extents.y;
                primitive.end_or_half_extents_z = fixture.overlap_half_extents.z;
            }
            primitives[primitive_index] = primitive;
        }
    }
    Ok(())
}

pub fn run_headless(
    args: &runner_args::RunnerArgs,
    effective_thread_count: usize,
) -> Result<(), i32>
{
    if args.step_count != args.case_execution.measured_work_unit_count as usize
        || args.warmup_steps != args.case_execution.warmup_work_unit_count as usize
    {
        return Err(2);
    }
    let mut world: RapierSpatialQueryWorld = create_world(&args.case_execution, effective_thread_count, args.recording_mode)?;
    if warmup_world(&mut world, args.warmup_steps) != 0
    {
        return Err(2);
    }
    let mut durations: Vec<Duration> = vec![Duration::ZERO; args.step_count];
    let mut recording: Option<crate::replay_recording::RecordingWriter> = match args.recording_mode
    {
        crate::runner_args::RecordingMode::On => Some(crate::replay_recording::begin_recording(args, &case_registry::CaseView::SpatialQuery(&mut world))?),
        crate::runner_args::RecordingMode::Off => None,
    };
    for (index, duration) in durations.iter_mut().enumerate()
    {
        step_world_timed(&mut world, std::slice::from_mut(duration))?;
        if let Some(writer) = recording.as_mut()
        {
            crate::replay_recording::append_frame(writer, args,
                &case_registry::CaseView::SpatialQuery(&mut world), index as u64 + 1)?;
        }
    }
    if let Some(writer) = recording
    {
        crate::replay_recording::complete_recording(writer)?;
    }
    let valid: bool = world.rigid_body_set.len() == args.case_execution.static_body_count as usize
        && world.collider_set.len() == args.case_execution.shape_count as usize
        && world.completed_batch_count == args.step_count
        && world.workload_elapsed.as_nanos() > 0
        && world.ray_elapsed.as_nanos() > 0
        && world.sphere_cast_elapsed.as_nanos() > 0
        && world.overlap_elapsed.as_nanos() > 0;
    let validity: result_writer::ResultValidity = if valid
    {
        result_writer::ResultValidity::Valid
    }
    else
    {
        result_writer::ResultValidity::Invalid
    };
    let result: result_writer::BenchmarkResult = result_writer::BenchmarkResult
    {
        physics_settings: visual_physics_settings(
            &args.case_execution, effective_thread_count),
        body_count: args.case_execution.body_count as usize,
        shape_count: args.case_execution.shape_count as usize,
        query_count: args.case_execution.query_count as usize,
        constraint_count: args.case_execution.constraint_count as usize,
        invalid_transform_count: 0,
        effective_thread_count,
        effective_worker_count: effective_thread_count,
        completed_work_unit_count: world.completed_batch_count,
        workload_elapsed_ms: world.workload_elapsed.as_secs_f64() * 1000.0,
        case_validity: validity,
        metric_validity: validity,
        observations: headless_observations(&world).into(),
    };
    result_writer::write_result(args, &result)?;
    result_writer::write_step_timing(args, &durations)
}

pub fn headless_observations(
    world: &RapierSpatialQueryWorld,
) -> [result_writer::ObservationRow; 6]
{
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = world.execution.spatial_query;
    [
        float_observation(
            "ray_queries_per_second",
            query_rate(fixture.ray_count as usize, world.completed_batch_count, world.ray_elapsed),
        ),
        float_observation(
            "sphere_cast_queries_per_second",
            query_rate(
                fixture.sphere_cast_count as usize,
                world.completed_batch_count,
                world.sphere_cast_elapsed,
            ),
        ),
        float_observation(
            "overlap_queries_per_second",
            query_rate(
                fixture.overlap_count as usize,
                world.completed_batch_count,
                world.overlap_elapsed,
            ),
        ),
        uint_observation("ray_hit_count", world.ray_hit_count),
        uint_observation("sphere_cast_hit_count", world.sphere_cast_hit_count),
        uint_observation("overlap_hit_count", world.overlap_hit_count),
    ]
}

pub fn float_observation(
    metric_id: &'static str,
    value: f64,
) -> result_writer::ObservationRow
{
    result_writer::ObservationRow
    {
        metric_id,
        phase_id: "final",
        sample_index: 0,
        value: result_writer::ObservationValue::Float64(value.to_bits()),
    }
}

pub fn uint_observation(
    metric_id: &'static str,
    value: u64,
) -> result_writer::ObservationRow
{
    result_writer::ObservationRow
    {
        metric_id,
        phase_id: "final",
        sample_index: 0,
        value: result_writer::ObservationValue::Uint64(value),
    }
}

pub fn query_rate(query_count: usize, batch_count: usize, elapsed: Duration) -> f64
{
    if elapsed.is_zero()
    {
        0.0
    }
    else
    {
        query_count as f64 * batch_count as f64 / elapsed.as_secs_f64()
    }
}

pub fn generate_query(execution: &CaseExecutionSpec, index: usize) -> SpatialQuery
{
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = execution.spatial_query;
    let mut family_index: usize = index;
    if index >= (fixture.ray_count + fixture.sphere_cast_count) as usize
    {
        family_index -= (fixture.ray_count + fixture.sphere_cast_count) as usize;
    }
    else if index >= fixture.ray_count as usize
    {
        family_index -= fixture.ray_count as usize;
    }
    let sample: usize = family_index / 2;
    let intended_hit: i32 = if family_index & 1 == 0
    {
        1
    }
    else
    {
        0
    };
    let slot: usize = sample % execution.static_body_count as usize;
    let ix: usize = slot % fixture.static_grid[0] as usize;
    let iz: usize = (slot / fixture.static_grid[0] as usize) % fixture.static_grid[2] as usize;
    let iy: usize = slot / (fixture.static_grid[0] * fixture.static_grid[2]) as usize;
    let center: Vector = Vector::new(
        centered_grid_coordinate(fixture.static_base_center.x, fixture.static_spacing.x,
            ix, fixture.static_grid[0] as usize),
        uncentered_grid_coordinate(fixture.static_base_center.y, fixture.static_spacing.y, iy),
        centered_grid_coordinate(fixture.static_base_center.z, fixture.static_spacing.z,
            iz, fixture.static_grid[2] as usize),
    );
    let scene_minimum: Vector = Vector::new(
        centered_grid_coordinate(fixture.static_base_center.x, fixture.static_spacing.x,
            0, fixture.static_grid[0] as usize) - fixture.static_half_extents.x,
        fixture.static_base_center.y - fixture.static_half_extents.y,
        centered_grid_coordinate(fixture.static_base_center.z, fixture.static_spacing.z,
            0, fixture.static_grid[2] as usize) - fixture.static_half_extents.z,
    );
    let scene_maximum: Vector = Vector::new(
        centered_grid_coordinate(fixture.static_base_center.x, fixture.static_spacing.x,
            fixture.static_grid[0] as usize - 1, fixture.static_grid[0] as usize) +
            fixture.static_half_extents.x,
        uncentered_grid_coordinate(fixture.static_base_center.y, fixture.static_spacing.y,
            fixture.static_grid[1] as usize - 1) +
            fixture.static_half_extents.y,
        centered_grid_coordinate(fixture.static_base_center.z, fixture.static_spacing.z,
            fixture.static_grid[2] as usize - 1, fixture.static_grid[2] as usize) +
            fixture.static_half_extents.z,
    );
    let face: usize = sample % 6;
    let axis: usize = face / 2;
    if index >= (fixture.ray_count + fixture.sphere_cast_count) as usize
    {
        let mut overlap_center: Vector = center;
        if intended_hit == 0
        {
            overlap_center[axis] = scene_maximum[axis] + fixture.miss_offset;
        }
        return SpatialQuery
        {
            origin_or_center: overlap_center,
            direction: Vector::ZERO,
        };
    }
    let positive_face: bool = face & 1 != 0;
    let first_transverse: usize = if axis == 0
    {
        1
    }
    else
    {
        0
    };
    let mut origin: Vector = Vector::ZERO;
    let mut direction: Vector = Vector::ZERO;
    for component in 0..3
    {
        if component == axis
        {
            origin[component] = if positive_face
            {
                scene_maximum[component] + 5.0
            }
            else
            {
                scene_minimum[component] - 5.0
            };
            direction[component] = if positive_face
            {
                -1.0
            }
            else
            {
                1.0
            };
        }
        else
        {
            origin[component] = if intended_hit == 0 && component == first_transverse
            {
                scene_maximum[component] + fixture.miss_offset
            }
            else
            {
                center[component]
            };
        }
    }
    SpatialQuery
    {
        origin_or_center: origin,
        direction,
    }
}
