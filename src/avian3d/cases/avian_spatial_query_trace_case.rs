use crate::case_execution_wire::CaseExecutionSpec;
use crate::{case_registry, result_writer, runner_args};
use avian3d::{collider_tree::ColliderTrees, math::RVector, prelude::*};
use bevy::{
    MinimalPlugins,
    app::{App, PluginsState},
    ecs::{resource::Resource, schedule::ScheduleLabel},
    math::{Dir3, Quat},
    prelude::Transform,
    transform::TransformPlugin,
};
use rayon::prelude::*;
use std::time::{Duration, Instant};

pub const DESCRIPTOR: case_registry::AvianCaseDescriptor =
    case_registry::AvianCaseDescriptor
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

#[derive(Clone, Copy)]
pub struct SpatialQueryInput
{
    pub origin_or_center: RVector,
    pub direction: Dir3,
}

#[derive(Resource)]
pub struct SpatialQueryBatchState
{
    pub execution: CaseExecutionSpec,
    pub ray_inputs: Box<[SpatialQueryInput]>,
    pub sphere_cast_inputs: Box<[SpatialQueryInput]>,
    pub overlap_inputs: Box<[ColliderAabb]>,
    pub lane_hit_counts: Box<[u64]>,
    pub filter: SpatialQueryFilter,
    pub sphere: Collider,
    pub cast_config: ShapeCastConfig,
    pub debug_hits: Box<[u8]>,
    pub debug_hit_distances: Box<[f32]>,
    pub phase: SpatialQueryBatchPhase,
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

#[derive(ScheduleLabel, Clone, Debug, PartialEq, Eq, Hash)]
pub struct SpatialQueryBatchSchedule;

#[derive(ScheduleLabel, Clone, Debug, PartialEq, Eq, Hash)]
pub struct SpatialQueryDebugCaptureSchedule;

pub struct AvianSpatialQueryWorld
{
    pub app: App,
    pub dynamic_proxy_count: usize,
    pub kinematic_proxy_count: usize,
    pub static_proxy_count: usize,
    pub standalone_proxy_count: usize,
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
) -> Result<AvianSpatialQueryWorld, i32>
{
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = execution.spatial_query;
    if thread_count == 0
        || (fixture.ray_count as usize) < thread_count
        || (fixture.sphere_cast_count as usize) < thread_count
        || (fixture.overlap_count as usize) < thread_count
    {
        return Err(2);
    }
    let mut app: App = App::new();
    let ray_inputs: Box<[SpatialQueryInput]> = (0..fixture.ray_count as usize)
        .map(|index| generate_query(execution, index))
        .collect::<Box<[SpatialQueryInput]>>();
    let sphere_cast_inputs: Box<[SpatialQueryInput]> = (0..fixture.sphere_cast_count as usize)
        .map(|index| generate_query(execution, fixture.ray_count as usize + index))
        .collect::<Box<[SpatialQueryInput]>>();
    let half_extent: RVector = RVector::new(fixture.overlap_half_extents.x,
        fixture.overlap_half_extents.y, fixture.overlap_half_extents.z);
    let overlap_inputs: Box<[ColliderAabb]> = (0..fixture.overlap_count as usize)
        .map(|index|
        {
            let query: SpatialQueryInput = generate_query(execution,
                (fixture.ray_count + fixture.sphere_cast_count) as usize + index);
            ColliderAabb::from_min_max(
                query.origin_or_center - half_extent,
                query.origin_or_center + half_extent,
            )
        })
        .collect::<Box<[ColliderAabb]>>();
    let debug_count: usize = match recording_mode
    {
        crate::runner_args::RecordingMode::On => execution.visual_debug_primitive_count as usize,
        crate::runner_args::RecordingMode::Off => 0,
    };
    let debug_hits: Box<[u8]> = vec![0u8; debug_count].into_boxed_slice();
    let debug_hit_distances: Box<[f32]> = vec![fixture.query_distance; debug_count].into_boxed_slice();
    app.add_plugins((
        MinimalPlugins,
        TransformPlugin,
        PhysicsPlugins::new(case_registry::AvianBenchmarkSchedule),
    ))
    .insert_resource(Gravity(RVector::new(execution.gravity.x,
        execution.gravity.y, execution.gravity.z)))
    .insert_resource(SubstepCount(1))
    .insert_resource(SpatialQueryBatchState
    {
        execution: *execution,
        ray_inputs,
        sphere_cast_inputs,
        overlap_inputs,
        lane_hit_counts: vec![0u64; thread_count].into_boxed_slice(),
        filter: SpatialQueryFilter::default(),
        sphere: Collider::sphere(fixture.sphere_cast_radius),
        cast_config: ShapeCastConfig::from_max_distance(fixture.query_distance),
        debug_hits,
        debug_hit_distances,
        phase: SpatialQueryBatchPhase::Warmup,
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
    })
    .add_systems(SpatialQueryBatchSchedule, execute_spatial_query_batch);
    if matches!(recording_mode, crate::runner_args::RecordingMode::On)
    {
        app.add_systems(SpatialQueryDebugCaptureSchedule, capture_spatial_query_debug_samples);
    }
    while app.plugins_state() != PluginsState::Ready
    {
        bevy::tasks::tick_global_task_pools_on_main_thread();
    }
    app.finish();
    app.cleanup();

    let shape: Collider = case_registry::create_resolved_shape(&execution.selected_geometry, execution)?;
    let shape_rotation: bevy::math::Quat = case_registry::shape_rotation(execution.selected_geometry.axis);
    for iy in 0..fixture.static_grid[1] as usize
    {
        for iz in 0..fixture.static_grid[2] as usize
        {
            for ix in 0..fixture.static_grid[0] as usize
            {
                let x: f32 = centered_grid_coordinate(fixture.static_base_center.x,
                    fixture.static_spacing.x, ix, fixture.static_grid[0] as usize);
                let y: f32 = uncentered_grid_coordinate(fixture.static_base_center.y,
                    fixture.static_spacing.y, iy);
                let z: f32 = centered_grid_coordinate(fixture.static_base_center.z,
                    fixture.static_spacing.z, iz, fixture.static_grid[2] as usize);
                app.world_mut().spawn((
                    RigidBody::Static,
                    shape.clone(),
                    Friction::new(execution.friction),
                    Restitution::new(execution.restitution),
                    Position::from_xyz(x, y, z),
                    Rotation(shape_rotation),
                    Transform::from_xyz(x, y, z).with_rotation(shape_rotation),
                ));
            }
        }
    }
    app.world_mut()
        .run_schedule(case_registry::AvianBenchmarkSchedule);
    let (dynamic_proxy_count, kinematic_proxy_count, static_proxy_count, standalone_proxy_count): (usize, usize, usize, usize) =
    {
        let trees: &ColliderTrees = app.world().resource::<ColliderTrees>();
        (
            trees.dynamic_tree.proxies.len(),
            trees.kinematic_tree.proxies.len(),
            trees.static_tree.proxies.len(),
            trees.standalone_tree.proxies.len(),
        )
    };
    if matches!(recording_mode, crate::runner_args::RecordingMode::On)
    {
        app.world_mut()
            .run_schedule(SpatialQueryDebugCaptureSchedule);
    }
    Ok(AvianSpatialQueryWorld
    {
        app,
        dynamic_proxy_count,
        kinematic_proxy_count,
        static_proxy_count,
        standalone_proxy_count,
    })
}

pub fn check_query_batch(state: &SpatialQueryBatchState, phase: &str, batch: usize) -> i32
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
            eprintln!("run_failed reason=query_batch engine=avian3d phase={phase} batch={batch} family={family_name} expected={expected} actual={actual}");
            return 2;
        }
    }
    0
}

pub fn warmup_world(world: &mut AvianSpatialQueryWorld, batch_count: usize) -> i32
{
    if batch_count != world.app.world().resource::<SpatialQueryBatchState>()
        .execution.warmup_work_unit_count as usize
    {
        return 2;
    }
    world
        .app
        .world_mut()
        .resource_mut::<SpatialQueryBatchState>()
        .phase = SpatialQueryBatchPhase::Warmup;
    for batch in 0..batch_count
    {
        world
            .app
            .world_mut()
            .run_schedule(SpatialQueryBatchSchedule);
        let state: &SpatialQueryBatchState = world.app.world().resource::<SpatialQueryBatchState>();
        if check_query_batch(state, "warmup", batch) != 0
        {
            return 2;
        }
    }
    0
}

pub fn step_world_timed(
    world: &mut AvianSpatialQueryWorld,
    durations: &mut [Duration],
) -> Result<(), i32>
{
    let completed: usize = world
        .app
        .world()
        .resource::<SpatialQueryBatchState>()
        .completed_batch_count;
    let measured: usize = world.app.world().resource::<SpatialQueryBatchState>()
        .execution.measured_work_unit_count as usize;
    if durations.len() > measured - completed
    {
        return Err(2);
    }
    world
        .app
        .world_mut()
        .resource_mut::<SpatialQueryBatchState>()
        .phase = SpatialQueryBatchPhase::Measured;
    for duration in durations.iter_mut()
    {
        world
            .app
            .world_mut()
            .run_schedule(SpatialQueryBatchSchedule);
        let state: &SpatialQueryBatchState = world.app.world().resource::<SpatialQueryBatchState>();
        if check_query_batch(state, "measured", state.completed_batch_count - 1) != 0
        {
            return Err(2);
        }
        *duration = world
            .app
            .world()
            .resource::<SpatialQueryBatchState>()
            .latest_batch_elapsed;
    }
    Ok(())
}

pub fn capture_spatial_query_debug_samples(
    spatial_query: SpatialQuery,
    mut state: bevy::prelude::ResMut<SpatialQueryBatchState>,
)
{
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = state.execution.spatial_query;
    let samples: usize = fixture.debug_samples_per_family as usize;
    for index in 0..samples
    {
        let input: SpatialQueryInput = state.ray_inputs[index];
        if let Some(hit) = spatial_query.cast_ray(
            input.origin_or_center,
            input.direction,
            fixture.query_distance,
            true,
            &state.filter,
        )
        {
            state.debug_hits[index] = 1;
            state.debug_hit_distances[index] = hit.distance;
        }
        else
        {
            state.debug_hits[index] = 0;
            state.debug_hit_distances[index] = fixture.query_distance;
        }
    }
    for index in 0..samples
    {
        let input: SpatialQueryInput = state.sphere_cast_inputs[index];
        let debug_index: usize = samples + index;
        if let Some(hit) = spatial_query.cast_shape(
            &state.sphere,
            input.origin_or_center,
            Quat::IDENTITY,
            input.direction,
            &state.cast_config,
            &state.filter,
        )
        {
            state.debug_hits[debug_index] = 1;
            state.debug_hit_distances[debug_index] = hit.distance;
        }
        else
        {
            state.debug_hits[debug_index] = 0;
            state.debug_hit_distances[debug_index] = fixture.query_distance;
        }
    }
    for index in 0..samples
    {
        let mut hit: u8 = 0u8;
        spatial_query.aabb_intersections_with_aabb_callback(
            state.overlap_inputs[index],
            |_|
            {
                hit = 1;
                false
            },
        );
        let debug_index: usize = 2 * samples + index;
        state.debug_hits[debug_index] = hit;
        state.debug_hit_distances[debug_index] = fixture.query_distance;
    }
}

pub fn execute_spatial_query_batch(
    spatial_query: SpatialQuery,
    mut state: bevy::prelude::ResMut<SpatialQueryBatchState>,
)
{
    let state: &mut SpatialQueryBatchState = &mut *state;
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = state.execution.spatial_query;
    let batch_start: Instant = Instant::now();

    let ray_start: Instant = Instant::now();
    let mut ray_hit_count: u64 = 0u64;
    if state.thread_count == 1
    {
        for index in 0..fixture.ray_count as usize
        {
            let input: SpatialQueryInput = state.ray_inputs[index];
            if spatial_query
                .cast_ray(
                    input.origin_or_center,
                    input.direction,
                    fixture.query_distance,
                    true,
                    &state.filter,
                )
                .is_some()
            {
                ray_hit_count += 1;
            }
        }
    }
    else
    {
        let thread_count: usize = state.thread_count;
        let ray_inputs: &Box<[SpatialQueryInput]> = &state.ray_inputs;
        let filter: &SpatialQueryFilter = &state.filter;
        state.lane_hit_counts
            .par_iter_mut()
            .enumerate()
            .for_each(|(lane_index, lane_hit_count)|
            {
                let start: usize = ray_inputs.len() * lane_index / thread_count;
                let end: usize = ray_inputs.len() * (lane_index + 1) / thread_count;
                let mut hit_count: u64 = 0u64;
                for input in &ray_inputs[start..end]
                {
                    hit_count += spatial_query
                        .cast_ray(
                            input.origin_or_center,
                            input.direction,
                            fixture.query_distance,
                            true,
                            filter,
                        )
                        .is_some() as u64;
                }
                *lane_hit_count = hit_count;
            });
        for lane_index in 0..thread_count
        {
            ray_hit_count += state.lane_hit_counts[lane_index];
        }
    }
    state.ray_hit_count = ray_hit_count;
    let ray_elapsed: Duration = ray_start.elapsed();

    let cast_start: Instant = Instant::now();
    let mut sphere_cast_hit_count: u64 = 0u64;
    if state.thread_count == 1
    {
        for index in 0..fixture.sphere_cast_count as usize
        {
            let input: SpatialQueryInput = state.sphere_cast_inputs[index];
            if spatial_query
                .cast_shape(
                    &state.sphere,
                    input.origin_or_center,
                    Quat::IDENTITY,
                    input.direction,
                    &state.cast_config,
                    &state.filter,
                )
                .is_some()
            {
                sphere_cast_hit_count += 1;
            }
        }
    }
    else
    {
        let thread_count: usize = state.thread_count;
        let sphere_cast_inputs: &Box<[SpatialQueryInput]> = &state.sphere_cast_inputs;
        let sphere: &Collider = &state.sphere;
        let cast_config: &ShapeCastConfig = &state.cast_config;
        let filter: &SpatialQueryFilter = &state.filter;
        state.lane_hit_counts
            .par_iter_mut()
            .enumerate()
            .for_each(|(lane_index, lane_hit_count)|
            {
                let start: usize = sphere_cast_inputs.len() * lane_index / thread_count;
                let end: usize = sphere_cast_inputs.len() * (lane_index + 1) / thread_count;
                let mut hit_count: u64 = 0u64;
                for input in &sphere_cast_inputs[start..end]
                {
                    hit_count += spatial_query
                        .cast_shape(
                            sphere,
                            input.origin_or_center,
                            Quat::IDENTITY,
                            input.direction,
                            cast_config,
                            filter,
                        )
                        .is_some() as u64;
                }
                *lane_hit_count = hit_count;
            });
        for lane_index in 0..thread_count
        {
            sphere_cast_hit_count += state.lane_hit_counts[lane_index];
        }
    }
    state.sphere_cast_hit_count = sphere_cast_hit_count;
    let sphere_cast_elapsed: Duration = cast_start.elapsed();

    let overlap_start: Instant = Instant::now();
    let mut overlap_hit_count: u64 = 0u64;
    if state.thread_count == 1
    {
        for index in 0..fixture.overlap_count as usize
        {
            let mut hit: u8 = 0u8;
            spatial_query.aabb_intersections_with_aabb_callback(
                state.overlap_inputs[index],
                |_|
                {
                    hit = 1;
                    false
                },
            );
            overlap_hit_count += hit as u64;
        }
    }
    else
    {
        let thread_count: usize = state.thread_count;
        let overlap_inputs: &Box<[ColliderAabb]> = &state.overlap_inputs;
        state.lane_hit_counts
            .par_iter_mut()
            .enumerate()
            .for_each(|(lane_index, lane_hit_count)|
            {
                let start: usize = overlap_inputs.len() * lane_index / thread_count;
                let end: usize = overlap_inputs.len() * (lane_index + 1) / thread_count;
                let mut hit_count: u64 = 0u64;
                for input in &overlap_inputs[start..end]
                {
                    let mut hit: u8 = 0u8;
                    spatial_query.aabb_intersections_with_aabb_callback(
                        *input,
                        |_|
                        {
                            hit = 1;
                            false
                        },
                    );
                    hit_count += hit as u64;
                }
                *lane_hit_count = hit_count;
            });
        for lane_index in 0..thread_count
        {
            overlap_hit_count += state.lane_hit_counts[lane_index];
        }
    }
    state.overlap_hit_count = overlap_hit_count;
    let overlap_elapsed: Duration = overlap_start.elapsed();
    let batch_elapsed: Duration = batch_start.elapsed();

    if state.phase == SpatialQueryBatchPhase::Measured
    {
        state.ray_elapsed += ray_elapsed;
        state.sphere_cast_elapsed += sphere_cast_elapsed;
        state.overlap_elapsed += overlap_elapsed;
        state.workload_elapsed += batch_elapsed;
        state.latest_batch_elapsed = batch_elapsed;
        state.completed_batch_count += 1;
    }
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
    let execution: CaseExecutionSpec = world.app.world().resource::<SpatialQueryBatchState>().execution;
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = execution.spatial_query;
    if geometries.is_empty() || instances.len() < execution.static_body_count as usize
    {
        return Err(2);
    }
    geometries[0] = case_registry::build_resolved_visual_geometry(&execution,
        &execution.selected_geometry, meshes)?;
    let rotation: Quat = case_registry::shape_rotation(execution.selected_geometry.axis);
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
    let batch: &SpatialQueryBatchState = world.app.world().resource::<SpatialQueryBatchState>();
    let execution: CaseExecutionSpec = batch.execution;
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
                material_index: if batch.debug_hits[primitive_index] != 0
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
                let input: SpatialQueryInput = batch.ray_inputs[local];
                primitive.origin_or_center_x = input.origin_or_center.x;
                primitive.origin_or_center_y = input.origin_or_center.y;
                primitive.origin_or_center_z = input.origin_or_center.z;
                let end: RVector = input.origin_or_center
                    + *input.direction * batch.debug_hit_distances[primitive_index];
                primitive.end_or_half_extents_x = end.x;
                primitive.end_or_half_extents_y = end.y;
                primitive.end_or_half_extents_z = end.z;
            }
            else if family == 1
            {
                let input: SpatialQueryInput = batch.sphere_cast_inputs[local];
                primitive.origin_or_center_x = input.origin_or_center.x;
                primitive.origin_or_center_y = input.origin_or_center.y;
                primitive.origin_or_center_z = input.origin_or_center.z;
                let end: RVector = input.origin_or_center
                    + *input.direction * batch.debug_hit_distances[primitive_index];
                primitive.end_or_half_extents_x = end.x;
                primitive.end_or_half_extents_y = end.y;
                primitive.end_or_half_extents_z = end.z;
                primitive.radius = fixture.sphere_cast_radius;
            }
            else
            {
                let center: RVector = batch.overlap_inputs[local].center();
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
    let mut world: AvianSpatialQueryWorld = create_world(&args.case_execution, effective_thread_count, args.recording_mode)?;
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
    let batch: &SpatialQueryBatchState = world.app.world().resource::<SpatialQueryBatchState>();
    let valid: bool = batch.completed_batch_count == args.step_count
        && batch.workload_elapsed.as_nanos() > 0
        && batch.ray_elapsed.as_nanos() > 0
        && batch.sphere_cast_elapsed.as_nanos() > 0
        && batch.overlap_elapsed.as_nanos() > 0
        && world.static_proxy_count == args.case_execution.static_body_count as usize
        && world.dynamic_proxy_count == 0
        && world.kinematic_proxy_count == 0
        && world.standalone_proxy_count == 0;
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
        completed_work_unit_count: batch.completed_batch_count,
        workload_elapsed_ms: batch.workload_elapsed.as_secs_f64() * 1000.0,
        case_validity: validity,
        metric_validity: validity,
        observations: headless_observations(batch).into(),
    };
    result_writer::write_result(args, &result)?;
    result_writer::write_step_timing(args, &durations)
}

pub fn headless_observations(
    batch: &SpatialQueryBatchState,
) -> [result_writer::ObservationRow; 6]
{
    let fixture: crate::case_execution_wire::CaseExecutionSpatialQuery = batch.execution.spatial_query;
    [
        float_observation(
            "ray_queries_per_second",
            query_rate(fixture.ray_count as usize, batch.completed_batch_count, batch.ray_elapsed),
        ),
        float_observation(
            "sphere_cast_queries_per_second",
            query_rate(
                fixture.sphere_cast_count as usize,
                batch.completed_batch_count,
                batch.sphere_cast_elapsed,
            ),
        ),
        float_observation(
            "overlap_queries_per_second",
            query_rate(
                fixture.overlap_count as usize,
                batch.completed_batch_count,
                batch.overlap_elapsed,
            ),
        ),
        uint_observation("ray_hit_count", batch.ray_hit_count),
        uint_observation("sphere_cast_hit_count", batch.sphere_cast_hit_count),
        uint_observation("overlap_hit_count", batch.overlap_hit_count),
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

pub fn generate_query(execution: &CaseExecutionSpec, index: usize) -> SpatialQueryInput
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
    let center: RVector = RVector::new(
        centered_grid_coordinate(fixture.static_base_center.x, fixture.static_spacing.x,
            ix, fixture.static_grid[0] as usize),
        uncentered_grid_coordinate(fixture.static_base_center.y, fixture.static_spacing.y, iy),
        centered_grid_coordinate(fixture.static_base_center.z, fixture.static_spacing.z,
            iz, fixture.static_grid[2] as usize),
    );
    let scene_minimum: RVector = RVector::new(
        centered_grid_coordinate(fixture.static_base_center.x, fixture.static_spacing.x,
            0, fixture.static_grid[0] as usize) - fixture.static_half_extents.x,
        fixture.static_base_center.y - fixture.static_half_extents.y,
        centered_grid_coordinate(fixture.static_base_center.z, fixture.static_spacing.z,
            0, fixture.static_grid[2] as usize) - fixture.static_half_extents.z,
    );
    let scene_maximum: RVector = RVector::new(
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
        let mut overlap_center: RVector = center;
        if intended_hit == 0
        {
            overlap_center[axis] = scene_maximum[axis] + fixture.miss_offset;
        }
        return SpatialQueryInput
        {
            origin_or_center: overlap_center,
            direction: Dir3::X,
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
    let mut origin: RVector = RVector::ZERO;
    let mut direction: RVector = RVector::ZERO;
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
    SpatialQueryInput
    {
        origin_or_center: origin,
        direction: Dir3::new(direction).expect("axis-aligned query direction"),
    }
}
