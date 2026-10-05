use crate::case_execution_wire::{case_text, CaseFixtureKind};
use crate::case_registry::VisualStableTransform;
use crate::runner_args::RunnerArgs;
use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::time::Instant;

#[repr(u32)]
#[derive(Clone, Copy)]
pub enum Phase
{
    Construction = 0,
    Warmup = 1,
    Measured = 2,
}

pub struct Capture
{
    pub pipe: File,
    pub transforms: Vec<VisualStableTransform>,
    pub bytes: Vec<u8>,
    pub elapsed_ms: f64,
    pub frame_count: u32,
    pub frame_start: Instant,
}

pub fn open(args: &RunnerArgs) -> Result<Capture, i32>
{
    let execution: &crate::case_execution_wire::CaseExecutionSpec = &args.case_execution;
    let mut pipe: File = OpenOptions::new().read(true).write(true).open(&args.stack_stream).map_err(|_| 2)?;
    let mut header: [u8; 236] = [0; 236];
    header[..8].copy_from_slice(b"BPSTACK\0");
    let identities: [&str; 3] = [case_text(&execution.case_id), case_text(&execution.fixture_semantic), crate::case_registry::ENGINE_ID];
    for (index, text) in identities.iter().enumerate()
    {
        let offset: usize = 8 + 64 * index;
        header[offset..offset + text.len()].copy_from_slice(text.as_bytes());
    }
    let projectiles: u32 = if execution.fixture_kind == CaseFixtureKind::LargePyramid
    {
        execution.large_pyramid.projectile_count
    }
    else
    {
        0
    };
    let fields: [u32; 9] = [execution.fixture_kind as u32, execution.fixture_revision, execution.dynamic_body_count,
        execution.dynamic_body_count - projectiles, args.thread_count as u32, args.repeat_index as u32,
        execution.warmup_work_unit_count, execution.measured_work_unit_count, execution.timestep_hz];
    for (index, value) in fields.iter().enumerate()
    {
        header[200 + 4 * index..204 + 4 * index].copy_from_slice(&value.to_le_bytes());
    }
    exchange(&mut pipe, &header)?;
    Ok(Capture
    {
        pipe,
        transforms: vec![VisualStableTransform::default(); execution.dynamic_body_count as usize],
        bytes: vec![0; 12 + 32 * execution.dynamic_body_count as usize],
        elapsed_ms: 0.0,
        frame_count: 0,
        frame_start: Instant::now(),
    })
}

pub fn append(capture: &mut Capture, phase: Phase, segment: u32, step: u32) -> Result<(), i32>
{
    capture.bytes[..4].copy_from_slice(&(phase as u32).to_le_bytes());
    capture.bytes[4..8].copy_from_slice(&segment.to_le_bytes());
    capture.bytes[8..12].copy_from_slice(&step.to_le_bytes());
    for (index, pose) in capture.transforms.iter().enumerate()
    {
        let offset: usize = 12 + 32 * index;
        capture.bytes[offset..offset + 4].copy_from_slice(&pose.stable_slot.to_le_bytes());
        let values: [f32; 7] = [pose.transform.position_x, pose.transform.position_y, pose.transform.position_z,
            pose.transform.rotation_x, pose.transform.rotation_y, pose.transform.rotation_z, pose.transform.rotation_w];
        for (field, value) in values.iter().enumerate()
        {
            capture.bytes[offset + 4 + 4 * field..offset + 8 + 4 * field].copy_from_slice(&value.to_le_bytes());
        }
    }
    exchange(&mut capture.pipe, &capture.bytes)?;
    capture.frame_count += 1;
    capture.elapsed_ms += capture.frame_start.elapsed().as_secs_f64() * 1000.0;
    Ok(())
}

pub fn close(mut capture: Capture) -> Result<(), i32>
{
    let mut footer: [u8; 16] = [0; 16];
    footer[..4].copy_from_slice(&3u32.to_le_bytes());
    footer[4..8].copy_from_slice(&capture.frame_count.to_le_bytes());
    footer[8..16].copy_from_slice(&capture.elapsed_ms.to_le_bytes());
    exchange(&mut capture.pipe, &footer)
}

#[repr(u32)]
pub enum Reply
{
    Accepted = 0,
    Rejected = 1,
}

pub fn exchange(pipe: &mut File, bytes: &[u8]) -> Result<(), i32>
{
    pipe.write_all(bytes).map_err(|_| 2)?;
    let mut reply: [u8; 4] = [0; 4];
    pipe.read_exact(&mut reply).map_err(|_| 2)?;
    if u32::from_le_bytes(reply) != Reply::Accepted as u32
    {
        return Err(2);
    }
    Ok(())
}
