use crate::rapier_pyramid_wall_case as pyramid_wall;
use crate::rapier_box_contact_islands_10k_case as contact_islands;
use crate::rapier_box_container_pile_10k_case as box_container_pile;
use crate::rapier_ragdoll_stair_tumble_case as ragdoll_stair_tumble;
use crate::rapier_large_pyramid_case as large_pyramid;
use crate::rapier_spatial_query_trace_case as spatial_query;
use crate::runner_args;
use crate::case_execution_wire::{CaseExecutionSpec, CaseFixtureKind};
use crate::case_execution_wire::{CaseExecutionGeometry, CaseExecutionShape, CaseExecutionAxis, HULL_POINT_COUNT};
use rapier3d::prelude::{SharedShape, Rotation, Vector};

pub const ENGINE_ID: &str = "rapier3d";

pub fn shape_rotation(axis: CaseExecutionAxis) -> Rotation
{
    match axis
    {
        CaseExecutionAxis::Y => Rotation::IDENTITY,
        CaseExecutionAxis::X => Rotation::from_rotation_z(-std::f32::consts::FRAC_PI_2),
        CaseExecutionAxis::Z => Rotation::from_rotation_x(std::f32::consts::FRAC_PI_2),
    }
}

pub fn create_resolved_shape(geometry: CaseExecutionGeometry, execution: &CaseExecutionSpec) -> Result<SharedShape, i32>
{
    match geometry.shape
    {
        CaseExecutionShape::Box => Ok(SharedShape::cuboid(geometry.half_extents.x, geometry.half_extents.y, geometry.half_extents.z)),
        CaseExecutionShape::Sphere => Ok(SharedShape::ball(geometry.radius)),
        CaseExecutionShape::Capsule => Ok(SharedShape::capsule_y(geometry.half_segment, geometry.radius)),
        CaseExecutionShape::ConvexHull =>
        {
            let mut points: [Vector; HULL_POINT_COUNT] = [Vector::ZERO; HULL_POINT_COUNT];
            for index in 0..HULL_POINT_COUNT
            {
                let point: crate::case_execution_wire::CaseExecutionVector3 = execution.hull_points[index];
                points[index] = Vector::new(point.x * geometry.half_extents.x, point.y * geometry.half_extents.y, point.z * geometry.half_extents.z);
            }
            SharedShape::convex_hull(&points).ok_or(2)
        }
        CaseExecutionShape::Unknown => Err(2),
    }
}

#[derive(Clone, Copy)]
pub struct RapierCaseDescriptor
{
    pub engine_id: &'static str,
}

#[derive(Clone, Copy, Default)]
pub struct VisualTransform
{
    pub position_x: f32,
    pub position_y: f32,
    pub position_z: f32,
    pub rotation_x: f32,
    pub rotation_y: f32,
    pub rotation_z: f32,
    pub rotation_w: f32,
}

#[derive(Clone, Copy, Default)]
pub struct VisualGeometry
{
    pub kind: u32,
    pub parameter_x: f32,
    pub parameter_y: f32,
    pub parameter_z: f32,
    pub vertex_offset: u32,
    pub vertex_count: u32,
    pub index_offset: u32,
    pub index_count: u32,
    pub edge_offset: u32,
    pub edge_count: u32,
}

#[derive(Default)]
pub struct VisualMeshStorage
{
    pub vertices: Vec<[f32; 3]>,
    pub indices: Vec<u32>,
    pub edges: Vec<[u32; 2]>,
}

pub fn build_resolved_visual_geometry(execution: &CaseExecutionSpec,
    geometry: &CaseExecutionGeometry, meshes: &mut VisualMeshStorage) -> Result<VisualGeometry, i32>
{
    let mut visual: VisualGeometry = VisualGeometry::default();
    match geometry.shape
    {
        CaseExecutionShape::Box =>
        {
            visual.kind = 2;
            visual.parameter_x = geometry.half_extents.x;
            visual.parameter_y = geometry.half_extents.y;
            visual.parameter_z = geometry.half_extents.z;
            return Ok(visual);
        }
        CaseExecutionShape::Sphere =>
        {
            visual.kind = 1;
            visual.parameter_x = geometry.radius;
            return Ok(visual);
        }
        CaseExecutionShape::Capsule | CaseExecutionShape::ConvexHull =>
        {
        }
        CaseExecutionShape::Unknown => return Err(2),
    }
    visual.kind = 3;
    visual.vertex_offset = meshes.vertices.len() as u32;
    visual.index_offset = meshes.indices.len() as u32;
    visual.edge_offset = meshes.edges.len() as u32;
    let (vertex_count, index_count, edge_count): (usize, usize, usize) =
        if geometry.shape == CaseExecutionShape::Capsule
        {
            (130, 768, 272)
        }
        else
        {
            (24, 132, 36)
        };
    if meshes.vertices.len() + vertex_count > 8192 || meshes.indices.len() + index_count > 49152
        || meshes.edges.len() + edge_count > 24576
    {
        return Err(2);
    }
    meshes.vertices.reserve(vertex_count);
    meshes.indices.reserve(index_count);
    meshes.edges.reserve(edge_count);
    if geometry.shape == CaseExecutionShape::Capsule
    {
        meshes.vertices.push([0.0, geometry.half_segment + geometry.radius, 0.0]);
        for ring in 0..8usize
        {
            let latitude: f32 = (if ring < 4
            {
                ring + 1
            }
            else
            {
                ring
            }) as f32 * std::f32::consts::PI / 8.0;
            let y: f32 = (if ring < 4
            {
                geometry.half_segment
            }
            else
            {
                -geometry.half_segment
            })
                + geometry.radius * latitude.cos();
            let radial: f32 = geometry.radius * latitude.sin();
            for longitude in 0..16usize
            {
                let angle: f32 = longitude as f32 * std::f32::consts::TAU / 16.0;
                meshes.vertices.push([radial * angle.cos(), y, radial * angle.sin()]);
            }
        }
        meshes.vertices.push([0.0, -geometry.half_segment - geometry.radius, 0.0]);
        for longitude in 0..16u32
        {
            let next: u32 = (longitude + 1) % 16;
            meshes.indices.extend_from_slice(&[0, 1 + next, 1 + longitude]);
            meshes.edges.push([0, 1 + longitude]);
            for ring in 0..8u32
            {
                let current: u32 = 1 + ring * 16;
                meshes.edges.push([current + longitude, current + next]);
                if ring < 7
                {
                    let lower: u32 = current + 16;
                    meshes.indices.extend_from_slice(&[current + longitude, current + next, lower + longitude,
                        current + next, lower + next, lower + longitude]);
                    meshes.edges.push([current + longitude, lower + longitude]);
                }
            }
            meshes.indices.extend_from_slice(&[113 + longitude, 113 + next, 129]);
            meshes.edges.push([113 + longitude, 129]);
        }
    }
    else
    {
        for point in &execution.hull_points
        {
            meshes.vertices.push([point.x * geometry.half_extents.x,
                point.y * geometry.half_extents.y, point.z * geometry.half_extents.z]);
        }
        for face in 0..14usize
        {
            let mut corners: [u32; 8] = [0; 8];
            let mut count: usize = 0;
            if face < 6
            {
                let axis: usize = face / 2;
                let sign: f32 = if face & 1 != 0
                {
                    1.0
                }
                else
                {
                    -1.0
                };
                let mut angles: [f32; 8] = [0.0; 8];
                for (index, point) in execution.hull_points.iter().enumerate()
                {
                    let components: [f32; 3] = [point.x, point.y, point.z];
                    if components[axis] != sign
                    {
                        continue;
                    }
                    if count == 8
                    {
                        return Err(2);
                    }
                    let angle: f32 = sign * components[(axis + 2) % 3].atan2(components[(axis + 1) % 3]);
                    let mut insertion: usize = count;
                    count += 1;
                    while insertion != 0 && angles[insertion - 1] > angle
                    {
                        corners[insertion] = corners[insertion - 1];
                        angles[insertion] = angles[insertion - 1];
                        insertion -= 1;
                    }
                    corners[insertion] = index as u32;
                    angles[insertion] = angle;
                }
                if count != 8
                {
                    return Err(2);
                }
            }
            else
            {
                let signs: u32 = (face - 6) as u32;
                let positive_count: u32 = signs.count_ones();
                corners[0] = signs;
                corners[1] = if positive_count & 1 != 0
                {
                    8 + signs
                }
                else
                {
                    16 + signs
                };
                corners[2] = if positive_count & 1 != 0
                {
                    16 + signs
                }
                else
                {
                    8 + signs
                };
                count = 3;
            }
            for corner in 1..count - 1
            {
                meshes.indices.extend_from_slice(&[corners[0], corners[corner], corners[corner + 1]]);
            }
            for corner in 0..count
            {
                let a: u32 = corners[corner];
                let b: u32 = corners[(corner + 1) % count];
                let edge: [u32; 2] = [a.min(b), a.max(b)];
                if !meshes.edges[visual.edge_offset as usize..].contains(&edge)
                {
                    meshes.edges.push(edge);
                }
            }
        }
    }
    visual.vertex_count = (meshes.vertices.len() - visual.vertex_offset as usize) as u32;
    visual.index_count = (meshes.indices.len() - visual.index_offset as usize) as u32;
    visual.edge_count = (meshes.edges.len() - visual.edge_offset as usize) as u32;
    if visual.vertex_count as usize != vertex_count || visual.index_count as usize != index_count
        || visual.edge_count as usize != edge_count
    {
        return Err(2);
    }
    Ok(visual)
}


#[derive(Clone, Copy, Default)]
pub struct VisualInstance
{
    pub geometry_index: u32,
    pub stable_slot: u32,
    pub transform_slot: u32,
    pub initial_transform: VisualTransform,
}

#[derive(Clone, Copy, Default)]
pub struct VisualStableTransform
{
    pub stable_slot: u32,
    pub transform: VisualTransform,
}

#[derive(Clone, Copy, Default)]
pub struct VisualDebugPrimitive
{
    pub kind: u32,
    pub material_index: u32,
    pub origin_or_center_x: f32,
    pub origin_or_center_y: f32,
    pub origin_or_center_z: f32,
    pub end_or_half_extents_x: f32,
    pub end_or_half_extents_y: f32,
    pub end_or_half_extents_z: f32,
    pub radius: f32,
    pub reserved: u32,
}

pub enum CaseView<'a>
{
    ContainerPile(&'a mut box_container_pile::RapierWorld),
    ContactIslands(&'a mut contact_islands::RapierContactIslandsWorld),
    SpatialQuery(&'a mut spatial_query::RapierSpatialQueryWorld),
    RagdollStairTumble(&'a mut ragdoll_stair_tumble::RapierRagdollWorld),
    LargePyramid(&'a mut large_pyramid::RapierLargePyramidWorld),
    PyramidWall(&'a mut pyramid_wall::RapierPyramidWallWorld),
}

pub struct CaseRegistration
{
    pub descriptor: RapierCaseDescriptor,
    pub run_headless: fn(&runner_args::RunnerArgs, usize) -> Result<(), i32>,
    pub build_visual_scene: fn(
        &CaseView,
        &mut [VisualGeometry],
        &mut VisualMeshStorage,
        &mut [VisualInstance],
    ) -> Result<(usize, usize), i32>,
    pub sample_visual_transforms:
        fn(&CaseView, &mut [VisualStableTransform]) -> Result<(), i32>,
    pub build_visual_debug_primitives:
        fn(&CaseView, &mut [VisualDebugPrimitive]) -> Result<(), i32>,
}

pub static CASE_REGISTRATIONS: [CaseRegistration; 6] = [
    box_container_pile::REGISTRATION,
    contact_islands::REGISTRATION,
    spatial_query::REGISTRATION,
    ragdoll_stair_tumble::REGISTRATION,
    large_pyramid::REGISTRATION,
    pyramid_wall::REGISTRATION,
];

pub fn resolve(execution: &CaseExecutionSpec) -> Result<&'static CaseRegistration, i32>
{
    let fixture_kind = execution.fixture_kind;
    if fixture_kind != CaseFixtureKind::SpatialQueryTrace &&
        (execution.solver_fields != 8 || execution.solver_values[crate::case_execution_wire::CaseSolverField::SolverIterations as usize] == 0)
    {
        return Err(2);
    }
    let index: usize = match fixture_kind
    {
        CaseFixtureKind::OpenContainerFallingPile => 0,
        CaseFixtureKind::BoxContactIslands => 1,
        CaseFixtureKind::SpatialQueryTrace => 2,
        CaseFixtureKind::RagdollStairTumble => 3,
        CaseFixtureKind::LargePyramid => 4,
        CaseFixtureKind::PyramidWall => 5,
        CaseFixtureKind::Unknown => return Err(2),
    };
    Ok(&CASE_REGISTRATIONS[index])
}
