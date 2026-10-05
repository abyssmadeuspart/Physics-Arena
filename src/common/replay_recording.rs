use crate::case_execution_wire::{case_text, CaseExecutionVector3, CaseFixtureKind};
use crate::case_registry::{CaseView, VisualDebugPrimitive, VisualGeometry, VisualInstance,
    VisualMeshStorage, VisualStableTransform, VisualTransform};
use crate::runner_args::RunnerArgs;
use std::fs::{File, OpenOptions};
use std::io::{BufWriter, Write};
use std::path::PathBuf;

pub enum RecordingState
{
    Open,
    Failed,
}

pub struct RecordingWriter
{
    file: BufWriter<File>,
    final_path: PathBuf,
    partial_path: PathBuf,
    frame_buffer: Vec<u8>,
    transforms: Vec<VisualStableTransform>,
    debug: Vec<VisualDebugPrimitive>,
    frame_count: u64,
    frame_stride: u64,
    file_bytes: u64,
    written_frames: u64,
    state: RecordingState,
}

fn io_failure(error: std::io::Error) -> i32
{
    eprintln!("run_failed reason=recording_io error={error}");
    2
}

fn put_u32(output: &mut Vec<u8>, value: u32)
{
    output.extend_from_slice(&value.to_le_bytes());
}

fn put_u64(output: &mut Vec<u8>, value: u64)
{
    output.extend_from_slice(&value.to_le_bytes());
}

fn put_float(output: &mut Vec<u8>, value: f32)
{
    output.extend_from_slice(&value.to_le_bytes());
}

fn put_text(output: &mut Vec<u8>, value: &str) -> Result<(), i32>
{
    if value.is_empty() || value.len() >= 64 || value.as_bytes().contains(&0)
    {
        return Err(2);
    }
    let end: usize = output.len() + 64;
    output.extend_from_slice(value.as_bytes());
    output.resize(end, 0);
    Ok(())
}

fn put_vector(output: &mut Vec<u8>, value: CaseExecutionVector3)
{
    put_float(output, value.x); put_float(output, value.y); put_float(output, value.z);
}

fn put_transform(output: &mut Vec<u8>, value: VisualTransform)
{
    put_float(output, value.position_x); put_float(output, value.position_y); put_float(output, value.position_z);
    put_float(output, value.rotation_x); put_float(output, value.rotation_y);
    put_float(output, value.rotation_z); put_float(output, value.rotation_w);
}

pub fn begin_recording(args: &RunnerArgs, state: &CaseView) -> Result<RecordingWriter, i32>
{
    let execution = &args.case_execution;
    let final_path: PathBuf = PathBuf::from(&args.recording_path);
    if final_path.extension().and_then(|value| value.to_str()) != Some("bpr") || final_path.exists() ||
        args.step_count == 0 || args.step_count > 100000 || execution.visual_instance_count > 16291 ||
        execution.dynamic_body_count > 16290 || execution.visual_debug_primitive_count > 768
    {
        return Err(2);
    }
    let mut partial_path = final_path.as_os_str().to_owned();
    partial_path.push(".partial");
    let partial_path = PathBuf::from(partial_path);
    let mut geometries: Vec<VisualGeometry> = vec![VisualGeometry::default(); 64];
    let mut instances: Vec<VisualInstance> = vec![VisualInstance::default(); execution.visual_instance_count as usize];
    let mut mesh = VisualMeshStorage::default();
    let (geometry_count, instance_count): (usize, usize) =
        (args.case_registration.build_visual_scene)(state, &mut geometries, &mut mesh, &mut instances)?;
    if geometry_count == 0 || geometry_count > 64 || instance_count == 0 || instance_count > instances.len() ||
        execution.dynamic_body_count as usize > instance_count || mesh.vertices.len() > 8192 ||
        mesh.indices.len() > 49152 || mesh.edges.len() > 24576
    {
        return Err(2);
    }
    let scene_bytes: u64 = 120 + geometry_count as u64 * 40 + instance_count as u64 * 44 +
        mesh.vertices.len() as u64 * 12 + mesh.indices.len() as u64 * 4 + mesh.edges.len() as u64 * 8;
    let frames_offset: u64 = 384 + scene_bytes;
    let frame_stride: u64 = 8 + execution.dynamic_body_count as u64 * 32 + execution.visual_debug_primitive_count as u64 * 40;
    let frame_count: u64 = args.step_count as u64 + 1;
    let trailer_offset: u64 = frames_offset.checked_add(frame_stride.checked_mul(frame_count).ok_or(2)?).ok_or(2)?;
    let file_bytes: u64 = trailer_offset.checked_add(24).ok_or(2)?;
    let kind: u32 = if execution.fixture_kind == CaseFixtureKind::SpatialQueryTrace
    {
        2
    }
    else
    {
        1
    };
    let timestep: f64 = if kind == 2
    {
        0.0
    }
    else
    {
        1.0 / execution.timestep_hz as f64
    };
    if !timestep.is_finite()
    {
        return Err(2);
    }
    let mut encoded: Vec<u8> = Vec::with_capacity(frames_offset as usize);
    encoded.extend_from_slice(b"BPREPLAY");
    put_u32(&mut encoded, 1); put_u32(&mut encoded, 384);
    put_text(&mut encoded, case_text(&execution.case_id))?;
    put_text(&mut encoded, args.case_registration.descriptor.engine_id)?;
    put_text(&mut encoded, case_text(&execution.fixture_semantic))?;
    put_text(&mut encoded, &execution.fixture_revision.to_string())?;
    for value in [args.thread_count as u32, args.repeat_index as u32, args.step_count as u32,
        args.warmup_steps as u32, execution.body_count, execution.shape_count, kind]
    {
        put_u32(&mut encoded, value);
    }
    encoded.extend_from_slice(&timestep.to_le_bytes());
    for value in [geometry_count as u32, instance_count as u32, execution.dynamic_body_count,
        execution.visual_debug_primitive_count, mesh.vertices.len() as u32,
        mesh.indices.len() as u32, mesh.edges.len() as u32]
    {
        put_u32(&mut encoded, value);
    }
    for value in [384, scene_bytes, frames_offset, frame_stride, frame_count, trailer_offset]
    {
        put_u64(&mut encoded, value);
    }
    let camera = &execution.replay_camera;
    for value in [camera.direction, camera.up, camera.minimum, camera.maximum, camera.eye, camera.target,
        camera.eye_offset, camera.target_offset]
    {
        put_vector(&mut encoded, value);
    }
    for value in [camera.vertical_fov_degrees, camera.viewport_fill, camera.near_plane, camera.far_plane]
    {
        put_float(&mut encoded, value);
    }
    put_u32(&mut encoded, camera.stable_slot); put_u32(&mut encoded, camera.mode);
    for geometry in geometries.iter().take(geometry_count)
    {
        put_u32(&mut encoded, geometry.kind);
        put_float(&mut encoded, geometry.parameter_x); put_float(&mut encoded, geometry.parameter_y); put_float(&mut encoded, geometry.parameter_z);
        for value in [geometry.vertex_offset, geometry.vertex_count, geometry.index_offset,
            geometry.index_count, geometry.edge_offset, geometry.edge_count]
        {
            put_u32(&mut encoded, value);
        }
    }
    for instance in instances.iter().take(instance_count)
    {
        put_u32(&mut encoded, instance.geometry_index); put_u32(&mut encoded, instance.stable_slot);
        put_u32(&mut encoded, 0); put_u32(&mut encoded, instance.transform_slot);
        put_transform(&mut encoded, instance.initial_transform);
    }
    for vertex in &mesh.vertices
    {
        for component in vertex
        {
            put_float(&mut encoded, *component);
        }
    }
    for index in &mesh.indices
    {
        put_u32(&mut encoded, *index);
    }
    for edge in &mesh.edges
    {
        put_u32(&mut encoded, edge[0]); put_u32(&mut encoded, edge[1]);
    }
    if encoded.len() != frames_offset as usize
    {
        return Err(2);
    }
    let file: File = OpenOptions::new().write(true).create_new(true).open(&partial_path).map_err(io_failure)?;
    let mut writer = RecordingWriter
    {
        file: BufWriter::with_capacity(65536, file), final_path, partial_path,
        frame_buffer: Vec::with_capacity(frame_stride as usize),
        transforms: vec![VisualStableTransform::default(); execution.dynamic_body_count as usize],
        debug: vec![VisualDebugPrimitive::default(); execution.visual_debug_primitive_count as usize],
        frame_count, frame_stride, file_bytes, written_frames: 0, state: RecordingState::Open,
    };
    writer.file.write_all(&encoded).map_err(io_failure)?;
    append_frame(&mut writer, args, state, 0)?;
    Ok(writer)
}

pub fn append_frame(writer: &mut RecordingWriter, args: &RunnerArgs, state: &CaseView, ordinal: u64) -> Result<(), i32>
{
    if !matches!(writer.state, RecordingState::Open) || ordinal != writer.written_frames || ordinal >= writer.frame_count
    {
        writer.state = RecordingState::Failed;
        return Err(2);
    }
    writer.state = RecordingState::Failed;
    (args.case_registration.sample_visual_transforms)(state, &mut writer.transforms)?;
    (args.case_registration.build_visual_debug_primitives)(state, &mut writer.debug)?;
    let output = &mut writer.frame_buffer;
    output.clear();
    put_u64(output, ordinal);
    for transform in &writer.transforms
    {
        put_u32(output, transform.stable_slot); put_transform(output, transform.transform);
    }
    for value in &writer.debug
    {
        put_u32(output, value.kind); put_u32(output, value.material_index);
        put_float(output, value.origin_or_center_x); put_float(output, value.origin_or_center_y); put_float(output, value.origin_or_center_z);
        put_float(output, value.end_or_half_extents_x); put_float(output, value.end_or_half_extents_y); put_float(output, value.end_or_half_extents_z);
        put_float(output, value.radius); put_u32(output, value.reserved);
    }
    if output.len() != writer.frame_stride as usize
    {
        return Err(2);
    }
    writer.file.write_all(output).map_err(io_failure)?;
    writer.written_frames += 1;
    writer.state = RecordingState::Open;
    Ok(())
}

pub fn complete_recording(mut writer: RecordingWriter) -> Result<(), i32>
{
    if !matches!(writer.state, RecordingState::Open) || writer.written_frames != writer.frame_count
    {
        return Err(2);
    }
    let mut trailer = Vec::with_capacity(24);
    trailer.extend_from_slice(b"BPRDONE1");
    put_u64(&mut trailer, writer.written_frames); put_u64(&mut trailer, writer.file_bytes);
    writer.file.write_all(&trailer).map_err(io_failure)?;
    writer.file.flush().map_err(io_failure)?;
    let file: File = writer.file.into_inner().map_err(|error| io_failure(error.into_error()))?;
    close_file(file).map_err(io_failure)?;
    if writer.final_path.exists()
    {
        return Err(2);
    }
    std::fs::rename(&writer.partial_path, &writer.final_path).map_err(io_failure)
}

#[cfg(windows)]
fn close_file(file: File) -> std::io::Result<()>
{
    use std::os::windows::io::IntoRawHandle;
    #[link(name = "kernel32")]
    unsafe extern "system"
    {
        fn CloseHandle(handle: *mut std::ffi::c_void) -> i32;
    }
    let handle = file.into_raw_handle();
    if unsafe
    {
        CloseHandle(handle)
    } == 0
    {
        Err(std::io::Error::last_os_error())
    }
    else
    {
        Ok(())
    }
}

#[cfg(unix)]
fn close_file(file: File) -> std::io::Result<()>
{
    use std::os::fd::IntoRawFd;
    unsafe extern "C"
    {
        fn close(fd: i32) -> i32;
    }
    let descriptor = file.into_raw_fd();
    if unsafe
    {
        close(descriptor)
    } == 0
    {
        Ok(())
    }
    else
    {
        Err(std::io::Error::last_os_error())
    }
}
