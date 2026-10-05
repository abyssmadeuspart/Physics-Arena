pub const PAYLOAD_CAPACITY: usize = 2048;
pub const HEX_CAPACITY: usize = PAYLOAD_CAPACITY * 2;
pub const STATIC_BOX_CAPACITY: usize = 16;
pub const RAGDOLL_PART_CAPACITY: usize = 32;
pub const RAGDOLL_LINK_CAPACITY: usize = 32;
pub const YAW_CAPACITY: usize = 16;
pub const TEXT_CAPACITY: usize = 64;
pub const HULL_POINT_COUNT: usize = 24;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u16)]
pub enum CaseFixtureKind
{
    #[default]
    Unknown = 0,
    OpenContainerFallingPile = 1,
    BoxContactIslands = 2,
    SpatialQueryTrace = 3,
    RagdollStairTumble = 4,
    LargePyramid = 5,
    PyramidWall = 6,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum CaseExecutionShape
{
    #[default]
    Unknown = 0,
    Box = 1,
    Sphere = 2,
    Capsule = 3,
    ConvexHull = 4,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum CaseShapePreset
{
    #[default]
    Authored = 0,
    Sphere = 1,
    Capsule = 2,
    ConvexHull = 3,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum CaseExecutionAxis
{
    #[default]
    Y = 0,
    X = 1,
    Z = 2,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum CaseExecutionToggle
{
    #[default]
    Disabled = 0,
    Enabled = 1,
}

#[derive(Clone, Copy)]
pub struct CaseText
{
    pub bytes: [u8; TEXT_CAPACITY],
    pub len: u8,
}

impl Default for CaseText
{
    fn default() -> Self
    {
        Self
        {
            bytes: [0; TEXT_CAPACITY], len: 0
        }
    }
}

pub fn case_text(text: &CaseText) -> &str
{
    std::str::from_utf8(&text.bytes[..text.len as usize]).expect("validated ASCII case text")
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionVector3
{
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionBox
{
    pub center: CaseExecutionVector3,
    pub half_extents: CaseExecutionVector3,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionGeometry
{
    pub shape: CaseExecutionShape,
    pub half_extents: CaseExecutionVector3,
    pub radius: f32,
    pub half_segment: f32,
    pub axis: CaseExecutionAxis,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionOpenContainer
{
    pub dynamic_grid: [u32; 3],
    pub dynamic_half_extents: CaseExecutionVector3,
    pub dynamic_spacing: CaseExecutionVector3,
    pub dynamic_initial_y: f32,
    pub density: f32,
    pub static_boxes: [CaseExecutionBox; STATIC_BOX_CAPACITY],
    pub static_box_count: u16,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionContactIslands
{
    pub island_grid: [u32; 2],
    pub island_spacing: [f32; 2],
    pub body_grid: [u32; 3],
    pub body_half_extents: CaseExecutionVector3,
    pub body_spacing: CaseExecutionVector3,
    pub body_initial_y: f32,
    pub floor_half_extents: CaseExecutionVector3,
    pub density: f32,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionSpatialQuery
{
    pub static_grid: [u32; 3],
    pub static_half_extents: CaseExecutionVector3,
    pub static_spacing: CaseExecutionVector3,
    pub static_base_center: CaseExecutionVector3,
    pub ray_count: u32,
    pub sphere_cast_count: u32,
    pub overlap_count: u32,
    pub query_distance: f32,
    pub sphere_cast_radius: f32,
    pub overlap_half_extents: CaseExecutionVector3,
    pub miss_offset: f32,
    pub debug_samples_per_family: u32,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionRagdollPart
{
    pub shape: CaseExecutionShape,
    pub center: CaseExecutionVector3,
    pub half_extents: CaseExecutionVector3,
    pub radius: f32,
    pub half_segment: f32,
    pub axis: CaseExecutionAxis,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionRagdollLink
{
    pub parent_part: u16,
    pub child_part: u16,
    pub anchor: CaseExecutionVector3,
    pub parent_local_anchor: CaseExecutionVector3,
    pub child_local_anchor: CaseExecutionVector3,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionRagdoll
{
    pub ragdoll_grid: [u32; 2],
    pub column_spacing: f32,
    pub row_spacing: f32,
    pub base_height_offset: f32,
    pub pitch_degrees: f32,
    pub yaw_pattern_degrees: [f32; YAW_CAPACITY],
    pub yaw_pattern_count: u16,
    pub trigger_row_speed: f32,
    pub follower_row_speed: f32,
    pub stair_count: u32,
    pub stair_rise: f32,
    pub stair_depth: f32,
    pub stair_half_width: f32,
    pub stair_half_height: f32,
    pub stair_half_depth: f32,
    pub extra_static_boxes: [CaseExecutionBox; STATIC_BOX_CAPACITY],
    pub extra_static_box_count: u16,
    pub parts: [CaseExecutionRagdollPart; RAGDOLL_PART_CAPACITY],
    pub part_count: u16,
    pub links: [CaseExecutionRagdollLink; RAGDOLL_LINK_CAPACITY],
    pub link_count: u16,
    pub linear_damping: f32,
    pub angular_damping: f32,
    pub part_mass: f32,
    pub linked_collision_mode: CaseExecutionToggle,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionLargePyramid
{
    pub row_count: u32,
    pub box_half_extents: CaseExecutionVector3,
    pub box_spacing: CaseExecutionVector3,
    pub base_center: CaseExecutionVector3,
    pub floor_half_extents: CaseExecutionVector3,
    pub box_density: f32,
    pub projectile_count: u32,
    pub projectile_radius: f32,
    pub projectile_density: f32,
    pub projectile_initial_center: CaseExecutionVector3,
    pub projectile_center_spacing: CaseExecutionVector3,
    pub projectile_launch_velocity: CaseExecutionVector3,
    pub projectile_launch_after_work_units: u32,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionPyramidWall
{
    pub row_count: u32,
    pub half_extent: f32,
    pub density: f32,
    pub floor_half_extents: CaseExecutionVector3,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct CaseExecutionCamera
{
    pub direction: CaseExecutionVector3,
    pub up: CaseExecutionVector3,
    pub minimum: CaseExecutionVector3,
    pub maximum: CaseExecutionVector3,
    pub eye: CaseExecutionVector3,
    pub target: CaseExecutionVector3,
    pub eye_offset: CaseExecutionVector3,
    pub target_offset: CaseExecutionVector3,
    pub vertical_fov_degrees: f32,
    pub viewport_fill: f32,
    pub near_plane: f32,
    pub far_plane: f32,
    pub stable_slot: u32,
    pub mode: u32,
}

#[repr(usize)]
#[derive(Clone, Copy)]
pub enum CaseSolverField
{
    VelocityIterations = 0,
    PositionIterations = 1,
    ProjectionIterations = 2,
    SolverIterations = 3,
    Substeps = 4,
    CollisionSteps = 5,
}

#[derive(Clone, Copy)]
pub struct CaseExecutionSpec
{
    pub fixture_kind: CaseFixtureKind,
    pub shape_preset: CaseShapePreset,
    pub selected_geometry: CaseExecutionGeometry,
    pub hull_points: [CaseExecutionVector3; HULL_POINT_COUNT],
    pub case_id: CaseText,
    pub fixture_semantic: CaseText,
    pub fixture_revision: u32,
    pub dynamic_body_count: u32,
    pub kinematic_body_count: u32,
    pub static_body_count: u32,
    pub body_count: u32,
    pub shape_count: u32,
    pub visual_instance_count: u32,
    pub mesh_triangle_count: u32,
    pub query_count: u32,
    pub constraint_count: u32,
    pub timestep_hz: u32,
    pub warmup_work_unit_count: u32,
    pub measured_work_unit_count: u32,
    pub visual_debug_primitive_count: u32,
    pub timestep_present: u8,
    pub gravity: CaseExecutionVector3,
    pub friction: f32,
    pub restitution: f32,
    pub replay_camera: CaseExecutionCamera,
    pub solver_fields: u32,
    pub solver_values: [u32; 6],
    pub sleep_mode: CaseExecutionToggle,
    pub continuous_collision_mode: CaseExecutionToggle,
    pub open_container: CaseExecutionOpenContainer,
    pub contact_islands: CaseExecutionContactIslands,
    pub spatial_query: CaseExecutionSpatialQuery,
    pub ragdoll: CaseExecutionRagdoll,
    pub large_pyramid: CaseExecutionLargePyramid,
    pub pyramid_wall: CaseExecutionPyramidWall,
}

impl Default for CaseExecutionSpec
{
    fn default() -> Self
    {
        Self
        {
            fixture_kind: CaseFixtureKind::Unknown,
            shape_preset: CaseShapePreset::Authored,
            selected_geometry: CaseExecutionGeometry::default(),
            hull_points: [CaseExecutionVector3::default(); HULL_POINT_COUNT],
            case_id: CaseText::default(),
            fixture_semantic: CaseText::default(),
            fixture_revision: 0,
            dynamic_body_count: 0,
            kinematic_body_count: 0,
            static_body_count: 0,
            body_count: 0,
            shape_count: 0,
            visual_instance_count: 0,
            mesh_triangle_count: 0,
            query_count: 0,
            constraint_count: 0,
            timestep_hz: 0,
            warmup_work_unit_count: 0,
            measured_work_unit_count: 0,
            visual_debug_primitive_count: 0,
            timestep_present: 0,
            gravity: CaseExecutionVector3::default(),
            friction: 0.0,
            restitution: 0.0,
            replay_camera: CaseExecutionCamera::default(),
            solver_fields: 0,
            solver_values: [0; 6],
            sleep_mode: CaseExecutionToggle::Disabled,
            continuous_collision_mode: CaseExecutionToggle::Disabled,
            open_container: CaseExecutionOpenContainer::default(),
            contact_islands: CaseExecutionContactIslands::default(),
            spatial_query: CaseExecutionSpatialQuery::default(),
            ragdoll: CaseExecutionRagdoll::default(),
            large_pyramid: CaseExecutionLargePyramid::default(),
            pyramid_wall: CaseExecutionPyramidWall::default(),
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum CaseDecodeStatus
{
    Invalid,
    Valid,
}

pub struct Reader<'a>
{
    pub bytes: &'a [u8],
    pub offset: usize,
    pub status: CaseDecodeStatus,
}

pub fn read_u8(reader: &mut Reader<'_>) -> u8
{
    if reader.offset >= reader.bytes.len()
    {
        reader.status = CaseDecodeStatus::Invalid;
        return 0;
    }
    let value: u8 = reader.bytes[reader.offset];
    reader.offset += 1;
    value
}

pub fn read_u16(reader: &mut Reader<'_>) -> u16
{
    let low: u16 = read_u8(reader) as u16;
    low | ((read_u8(reader) as u16) << 8)
}

pub fn read_u32(reader: &mut Reader<'_>) -> u32
{
    let low: u32 = read_u16(reader) as u32;
    low | ((read_u16(reader) as u32) << 16)
}

pub fn read_f32(reader: &mut Reader<'_>) -> f32
{
    let value: f32 = f32::from_bits(read_u32(reader));
    if !value.is_finite()
    {
        reader.status = CaseDecodeStatus::Invalid;
    }
    value
}

pub fn read_vector3(reader: &mut Reader<'_>) -> CaseExecutionVector3
{
    CaseExecutionVector3
    {
        x: read_f32(reader), y: read_f32(reader), z: read_f32(reader)
    }
}

pub fn read_case_box(reader: &mut Reader<'_>) -> CaseExecutionBox
{
    CaseExecutionBox
    {
        center: read_vector3(reader), half_extents: read_vector3(reader)
    }
}

pub fn read_geometry(reader: &mut Reader<'_>) -> Result<CaseExecutionGeometry, i32>
{
    let mut geometry: CaseExecutionGeometry = CaseExecutionGeometry::default();
    geometry.shape = match read_u8(reader)
    {
        1 => CaseExecutionShape::Box,
        2 => CaseExecutionShape::Sphere,
        3 => CaseExecutionShape::Capsule,
        4 => CaseExecutionShape::ConvexHull,
        _ => return Err(2),
    };
    if geometry.shape == CaseExecutionShape::Box || geometry.shape == CaseExecutionShape::ConvexHull
    {
        geometry.half_extents = read_vector3(reader);
        if geometry.half_extents.x <= 0.0 || geometry.half_extents.y <= 0.0 || geometry.half_extents.z <= 0.0
        {
            return Err(2);
        }
    }
    else
    {
        geometry.radius = read_f32(reader);
        if geometry.radius <= 0.0
        {
            return Err(2);
        }
        if geometry.shape == CaseExecutionShape::Capsule
        {
            geometry.half_segment = read_f32(reader);
            geometry.axis = match read_u8(reader)
            {
                0 => CaseExecutionAxis::Y,
                1 => CaseExecutionAxis::X,
                2 => CaseExecutionAxis::Z,
                _ => return Err(2),
            };
            if geometry.half_segment <= 0.0
            {
                return Err(2);
            }
        }
    }
    Ok(geometry)
}

pub fn part_geometry(part: &CaseExecutionRagdollPart) -> CaseExecutionGeometry
{
    CaseExecutionGeometry
    {
        shape: part.shape, half_extents: part.half_extents, radius: part.radius,
        half_segment: part.half_segment, axis: part.axis,
    }
}

pub fn same_geometry(left: CaseExecutionGeometry, right: CaseExecutionGeometry) -> i32
{
    (left.shape == right.shape && left.half_extents.x == right.half_extents.x &&
        left.half_extents.y == right.half_extents.y && left.half_extents.z == right.half_extents.z &&
        left.radius == right.radius && left.half_segment == right.half_segment && left.axis == right.axis) as i32
}

pub fn read_text(reader: &mut Reader<'_>) -> CaseText
{
    let mut text: CaseText = CaseText::default();
    let size: usize = read_u16(reader) as usize;
    if size == 0 || size >= TEXT_CAPACITY || size > reader.bytes.len().saturating_sub(reader.offset)
    {
        reader.status = CaseDecodeStatus::Invalid;
        return text;
    }
    for target in text.bytes.iter_mut().take(size)
    {
        let value: u8 = read_u8(reader);
        if value == 0 || value > 0x7f
        {
            reader.status = CaseDecodeStatus::Invalid;
        }
        *target = value;
    }
    text.len = size as u8;
    text
}

pub fn fixture_kind(value: u16) -> Option<CaseFixtureKind>
{
    match value
    {
        1 => Some(CaseFixtureKind::OpenContainerFallingPile),
        2 => Some(CaseFixtureKind::BoxContactIslands),
        3 => Some(CaseFixtureKind::SpatialQueryTrace),
        4 => Some(CaseFixtureKind::RagdollStairTumble),
        5 => Some(CaseFixtureKind::LargePyramid),
        6 => Some(CaseFixtureKind::PyramidWall),
        _ => None,
    }
}

pub fn toggle(value: u8) -> Option<CaseExecutionToggle>
{
    match value
    {
        0 => Some(CaseExecutionToggle::Disabled),
        1 => Some(CaseExecutionToggle::Enabled),
        _ => None,
    }
}

pub fn decode_hex(hex: &str) -> Result<CaseExecutionSpec, i32>
{
    if hex.is_empty() || hex.len() > HEX_CAPACITY || hex.len() & 1 != 0
    {
        return Err(2);
    }
    let mut bytes: [u8; PAYLOAD_CAPACITY] = [0u8; PAYLOAD_CAPACITY];
    for (index, pair) in hex.as_bytes().chunks_exact(2).enumerate()
    {
        let nibble = |value: u8| -> Option<u8>
        {
            match value
            {
                b'0'..=b'9' => Some(value - b'0'),
                b'a'..=b'f' => Some(value - b'a' + 10),
                _ => None,
            }
        };
        let high: u8 = nibble(pair[0]).ok_or(2)?;
        let low: u8 = nibble(pair[1]).ok_or(2)?;
        bytes[index] = (high << 4) | low;
    }
    decode(&bytes[..hex.len() / 2])
}

pub fn decode(bytes: &[u8]) -> Result<CaseExecutionSpec, i32>
{
    if bytes.len() < 10 || bytes.len() > PAYLOAD_CAPACITY
    {
        return Err(2);
    }
    let mut reader: Reader<'_> = Reader
    {
        bytes, offset: 0, status: CaseDecodeStatus::Valid
    };
    if read_u8(&mut reader) != b'P' || read_u8(&mut reader) != b'A' || read_u8(&mut reader) != b'C' || read_u8(&mut reader) != b'X'
    {
        return Err(2);
    }
    let mut spec: CaseExecutionSpec = CaseExecutionSpec::default();
    spec.fixture_kind = fixture_kind(read_u16(&mut reader)).ok_or(2)?;
    if read_u32(&mut reader) as usize != bytes.len()
    {
        return Err(2);
    }
    spec.case_id = read_text(&mut reader);
    spec.fixture_semantic = read_text(&mut reader);
    spec.fixture_revision = read_u32(&mut reader);
    spec.dynamic_body_count = read_u32(&mut reader);
    spec.kinematic_body_count = read_u32(&mut reader);
    spec.static_body_count = read_u32(&mut reader);
    spec.body_count = read_u32(&mut reader);
    spec.shape_count = read_u32(&mut reader);
    spec.visual_instance_count = read_u32(&mut reader);
    spec.mesh_triangle_count = read_u32(&mut reader);
    spec.query_count = read_u32(&mut reader);
    spec.constraint_count = read_u32(&mut reader);
    spec.timestep_present = read_u8(&mut reader);
    spec.timestep_hz = read_u32(&mut reader);
    spec.warmup_work_unit_count = read_u32(&mut reader);
    spec.measured_work_unit_count = read_u32(&mut reader);
    spec.visual_debug_primitive_count = read_u32(&mut reader);
    spec.gravity = read_vector3(&mut reader);
    spec.sleep_mode = toggle(read_u8(&mut reader)).ok_or(2)?;
    spec.continuous_collision_mode = toggle(read_u8(&mut reader)).ok_or(2)?;
    spec.friction = read_f32(&mut reader);
    spec.restitution = read_f32(&mut reader);
    spec.solver_fields = read_u32(&mut reader);
    for value in &mut spec.solver_values
    {
        *value = read_u32(&mut reader);
    }
    if spec.solver_fields & !63 != 0 ||
        (spec.fixture_kind == CaseFixtureKind::SpatialQueryTrace && spec.solver_fields != 0)
    {
        return Err(2);
    }
    for field in 0..6
    {
        if spec.solver_fields & (1 << field) == 0 && spec.solver_values[field] != 0
        {
            return Err(2);
        }
    }
    spec.replay_camera.direction = read_vector3(&mut reader);
    spec.replay_camera.up = read_vector3(&mut reader);
    spec.replay_camera.minimum = read_vector3(&mut reader);
    spec.replay_camera.maximum = read_vector3(&mut reader);
    spec.replay_camera.eye = read_vector3(&mut reader);
    spec.replay_camera.target = read_vector3(&mut reader);
    spec.replay_camera.eye_offset = read_vector3(&mut reader);
    spec.replay_camera.target_offset = read_vector3(&mut reader);
    spec.replay_camera.vertical_fov_degrees = read_f32(&mut reader);
    spec.replay_camera.viewport_fill = read_f32(&mut reader);
    spec.replay_camera.near_plane = read_f32(&mut reader);
    spec.replay_camera.far_plane = read_f32(&mut reader);
    spec.replay_camera.stable_slot = read_u32(&mut reader);
    spec.replay_camera.mode = read_u32(&mut reader);
    spec.shape_preset = match read_u8(&mut reader)
    {
        0 => CaseShapePreset::Authored,
        1 => CaseShapePreset::Sphere,
        2 => CaseShapePreset::Capsule,
        3 => CaseShapePreset::ConvexHull,
        _ => return Err(2),
    };
    if spec.shape_preset == CaseShapePreset::ConvexHull
    {
        for point in &mut spec.hull_points
        {
            *point = read_vector3(&mut reader);
        }
    }
    if spec.fixture_kind != CaseFixtureKind::RagdollStairTumble
    {
        spec.selected_geometry = read_geometry(&mut reader)?;
    }

    match spec.fixture_kind
    {
        CaseFixtureKind::OpenContainerFallingPile =>
        {
            for value in &mut spec.open_container.dynamic_grid
            {
                *value = read_u32(&mut reader);
            }
            spec.open_container.dynamic_half_extents = read_vector3(&mut reader);
            spec.open_container.dynamic_spacing = read_vector3(&mut reader);
            spec.open_container.dynamic_initial_y = read_f32(&mut reader);
            spec.open_container.density = read_f32(&mut reader);
            spec.open_container.static_box_count = read_u16(&mut reader);
            if spec.open_container.static_box_count as usize > STATIC_BOX_CAPACITY
            {
                return Err(2);
            }
            for index in 0..spec.open_container.static_box_count as usize
            {
                spec.open_container.static_boxes[index] = read_case_box(&mut reader);
            }
        }
        CaseFixtureKind::BoxContactIslands =>
        {
            for value in &mut spec.contact_islands.island_grid
            {
                *value = read_u32(&mut reader);
            }
            for value in &mut spec.contact_islands.island_spacing
            {
                *value = read_f32(&mut reader);
            }
            for value in &mut spec.contact_islands.body_grid
            {
                *value = read_u32(&mut reader);
            }
            spec.contact_islands.body_half_extents = read_vector3(&mut reader);
            spec.contact_islands.body_spacing = read_vector3(&mut reader);
            spec.contact_islands.body_initial_y = read_f32(&mut reader);
            spec.contact_islands.floor_half_extents = read_vector3(&mut reader);
            spec.contact_islands.density = read_f32(&mut reader);
        }
        CaseFixtureKind::SpatialQueryTrace =>
        {
            for value in &mut spec.spatial_query.static_grid
            {
                *value = read_u32(&mut reader);
            }
            spec.spatial_query.static_half_extents = read_vector3(&mut reader);
            spec.spatial_query.static_spacing = read_vector3(&mut reader);
            spec.spatial_query.static_base_center = read_vector3(&mut reader);
            spec.spatial_query.ray_count = read_u32(&mut reader);
            spec.spatial_query.sphere_cast_count = read_u32(&mut reader);
            spec.spatial_query.overlap_count = read_u32(&mut reader);
            spec.spatial_query.query_distance = read_f32(&mut reader);
            spec.spatial_query.sphere_cast_radius = read_f32(&mut reader);
            spec.spatial_query.overlap_half_extents = read_vector3(&mut reader);
            spec.spatial_query.miss_offset = read_f32(&mut reader);
            spec.spatial_query.debug_samples_per_family = read_u32(&mut reader);
        }
        CaseFixtureKind::RagdollStairTumble =>
        {
            spec.ragdoll.linked_collision_mode = toggle(read_u8(&mut reader)).ok_or(2)?;
            spec.ragdoll.linear_damping = read_f32(&mut reader);
            spec.ragdoll.angular_damping = read_f32(&mut reader);
            spec.ragdoll.part_mass = read_f32(&mut reader);
            for value in &mut spec.ragdoll.ragdoll_grid
            {
                *value = read_u32(&mut reader);
            }
            spec.ragdoll.column_spacing = read_f32(&mut reader);
            spec.ragdoll.row_spacing = read_f32(&mut reader);
            spec.ragdoll.base_height_offset = read_f32(&mut reader);
            spec.ragdoll.pitch_degrees = read_f32(&mut reader);
            spec.ragdoll.yaw_pattern_count = read_u16(&mut reader);
            if spec.ragdoll.yaw_pattern_count == 0 || spec.ragdoll.yaw_pattern_count as usize > YAW_CAPACITY
            {
                return Err(2);
            }
            for index in 0..spec.ragdoll.yaw_pattern_count as usize
            {
                spec.ragdoll.yaw_pattern_degrees[index] = read_f32(&mut reader);
            }
            spec.ragdoll.trigger_row_speed = read_f32(&mut reader);
            spec.ragdoll.follower_row_speed = read_f32(&mut reader);
            spec.ragdoll.stair_count = read_u32(&mut reader);
            spec.ragdoll.stair_rise = read_f32(&mut reader);
            spec.ragdoll.stair_depth = read_f32(&mut reader);
            spec.ragdoll.stair_half_width = read_f32(&mut reader);
            spec.ragdoll.stair_half_height = read_f32(&mut reader);
            spec.ragdoll.stair_half_depth = read_f32(&mut reader);
            spec.ragdoll.extra_static_box_count = read_u16(&mut reader);
            if spec.ragdoll.extra_static_box_count as usize > STATIC_BOX_CAPACITY
            {
                return Err(2);
            }
            for index in 0..spec.ragdoll.extra_static_box_count as usize
            {
                spec.ragdoll.extra_static_boxes[index] = read_case_box(&mut reader);
            }
            spec.ragdoll.part_count = read_u16(&mut reader);
            if spec.ragdoll.part_count == 0 || spec.ragdoll.part_count as usize > RAGDOLL_PART_CAPACITY
            {
                return Err(2);
            }
            for index in 0..spec.ragdoll.part_count as usize
            {
                let center: CaseExecutionVector3 = read_vector3(&mut reader);
                let geometry: CaseExecutionGeometry = read_geometry(&mut reader)?;
                let part: CaseExecutionRagdollPart = CaseExecutionRagdollPart
                {
                    shape: geometry.shape, center, half_extents: geometry.half_extents,
                    radius: geometry.radius, half_segment: geometry.half_segment, axis: geometry.axis,
                };
                spec.ragdoll.parts[index] = part;
            }
            spec.ragdoll.link_count = read_u16(&mut reader);
            if spec.ragdoll.link_count as usize > RAGDOLL_LINK_CAPACITY
            {
                return Err(2);
            }
            for index in 0..spec.ragdoll.link_count as usize
            {
                let link: CaseExecutionRagdollLink = CaseExecutionRagdollLink
                {
                    parent_part: read_u16(&mut reader),
                    child_part: read_u16(&mut reader),
                    anchor: read_vector3(&mut reader),
                    parent_local_anchor: read_vector3(&mut reader),
                    child_local_anchor: read_vector3(&mut reader),
                };
                if link.parent_part >= spec.ragdoll.part_count || link.child_part >= spec.ragdoll.part_count
                {
                    reader.status = CaseDecodeStatus::Invalid;
                }
                spec.ragdoll.links[index] = link;
            }
        }
        CaseFixtureKind::PyramidWall =>
        {
            spec.pyramid_wall.row_count = read_u32(&mut reader);
            spec.pyramid_wall.half_extent = read_f32(&mut reader);
            spec.pyramid_wall.density = read_f32(&mut reader);
            spec.pyramid_wall.floor_half_extents = read_vector3(&mut reader);
            let wall: CaseExecutionPyramidWall = spec.pyramid_wall;
            let count: u64 = wall.row_count as u64 * (wall.row_count as u64 + 1) / 2;
            if wall.row_count == 0 || wall.row_count > 180 || count > 16290 ||
                wall.half_extent <= 0.0 || wall.density <= 0.0 ||
                wall.floor_half_extents.x <= 0.0 || wall.floor_half_extents.y <= 0.0 || wall.floor_half_extents.z <= 0.0 ||
                spec.dynamic_body_count as u64 != count || spec.static_body_count != 1 || spec.kinematic_body_count != 0 ||
                spec.shape_count as u64 != count + 1 || spec.visual_instance_count as u64 != count + 1 ||
                spec.mesh_triangle_count != 0 || spec.query_count != 0 || spec.constraint_count != 0 || spec.timestep_hz == 0 ||
                spec.shape_preset != CaseShapePreset::Authored ||
                spec.selected_geometry.shape != CaseExecutionShape::Box ||
                spec.selected_geometry.half_extents.x != wall.half_extent ||
                spec.selected_geometry.half_extents.y != wall.half_extent ||
                spec.selected_geometry.half_extents.z != wall.half_extent
            {
                return Err(2);
            }
        }
        CaseFixtureKind::LargePyramid =>
        {
            spec.large_pyramid.row_count = read_u32(&mut reader);
            spec.large_pyramid.box_half_extents = read_vector3(&mut reader);
            spec.large_pyramid.box_spacing = read_vector3(&mut reader);
            spec.large_pyramid.base_center = read_vector3(&mut reader);
            spec.large_pyramid.floor_half_extents = read_vector3(&mut reader);
            spec.large_pyramid.box_density = read_f32(&mut reader);
            spec.large_pyramid.projectile_count = read_u32(&mut reader);
            spec.large_pyramid.projectile_radius = read_f32(&mut reader);
            spec.large_pyramid.projectile_density = read_f32(&mut reader);
            spec.large_pyramid.projectile_initial_center = read_vector3(&mut reader);
            spec.large_pyramid.projectile_center_spacing = read_vector3(&mut reader);
            spec.large_pyramid.projectile_launch_velocity = read_vector3(&mut reader);
            spec.large_pyramid.projectile_launch_after_work_units = read_u32(&mut reader);
        }
        CaseFixtureKind::Unknown => return Err(2),
    }

    let expected_shape: CaseExecutionShape = match spec.shape_preset
    {
        CaseShapePreset::Authored => CaseExecutionShape::Box,
        CaseShapePreset::Sphere => CaseExecutionShape::Sphere,
        CaseShapePreset::Capsule => CaseExecutionShape::Capsule,
        CaseShapePreset::ConvexHull => CaseExecutionShape::ConvexHull,
    };
    if spec.fixture_kind == CaseFixtureKind::RagdollStairTumble
    {
        for part in &spec.ragdoll.parts[..spec.ragdoll.part_count as usize]
        {
            if part.shape != CaseExecutionShape::Sphere && part.shape != expected_shape
            {
                return Err(2);
            }
        }
    }
    else if spec.selected_geometry.shape != expected_shape
    {
        return Err(2);
    }

    const SEMANTICS: [[&str; 4]; 5] =
    [
        ["open_container_falling_pile", "open_container_falling_pile_sphere", "open_container_falling_pile_capsule", "open_container_falling_pile_convex_hull"],
        ["box_contact_islands_10k", "box_contact_islands_10k_sphere", "box_contact_islands_10k_capsule", "box_contact_islands_10k_convex_hull"],
        ["spatial_query_trace", "spatial_query_trace_sphere", "spatial_query_trace_capsule", "spatial_query_trace_convex_hull"],
        ["ragdoll_stair_tumble", "ragdoll_stair_tumble_sphere", "ragdoll_stair_tumble_capsule", "ragdoll_stair_tumble_convex_hull"],
        ["large_pyramid", "large_pyramid_sphere", "large_pyramid_capsule", "large_pyramid_convex_hull"],
    ];
    let expected_semantic: &str = if spec.fixture_kind == CaseFixtureKind::PyramidWall
    {
        "pyramid_wall"
    }
    else
    {
        SEMANTICS[spec.fixture_kind as usize - 1][spec.shape_preset as usize]
    };
    if reader.status != CaseDecodeStatus::Valid
        || reader.offset != bytes.len()
        || case_text(&spec.fixture_semantic) != expected_semantic
        || spec.fixture_revision == 0
        || spec.measured_work_unit_count == 0
        || spec.body_count != spec.dynamic_body_count + spec.kinematic_body_count + spec.static_body_count
        || spec.visual_instance_count == 0
        || spec.visual_instance_count > spec.body_count
        || spec.timestep_present > 1
        || (spec.timestep_present == 0) != (spec.timestep_hz == 0)
    {
        return Err(2);
    }
    Ok(spec)
}
