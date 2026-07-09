use crate::case_registry;
use crate::rapier_box_container_pile_10k_case as rapier_case;
use std::io::{Read, Write};
use std::net::TcpStream;
use std::time::Instant;

const MAGIC: u32 = 0x42565354;
const VERSION: u16 = 1;
const MAX_PAYLOAD_BYTES: usize = 2 * 1024 * 1024;
const TEXT_CAPACITY: usize = 64;
const STATUS_OK: u32 = 0;
const STATUS_VISUAL_FAILED: u32 = 5;
const FRAME_HELLO: u16 = 1;
const FRAME_START: u16 = 2;
const FRAME_WARMUP: u16 = 3;
const FRAME_STEP: u16 = 4;
const FRAME_SHUTDOWN: u16 = 5;
const FRAME_HELLO_ACK: u16 = 6;
const FRAME_STARTED: u16 = 7;
const FRAME_SNAPSHOT: u16 = 8;
const FRAME_COMPLETED: u16 = 9;
const SNAPSHOT_HEADER_BYTES: usize = 360;
const TRANSFORM_BYTES: usize = 28;
const STATIC_BOX_BYTES: usize = 24;

pub struct SharedVisualCli {
    pub shared_visual_mode: String,
    pub connect_host: String,
    pub run_token: String,
    pub engine_id: String,
    pub case_id: String,
    pub connect_port: u16,
    pub thread_count: usize,
    pub repeat_index: usize,
    pub step_count: usize,
    pub warmup_steps: usize,
}

pub struct CaseRunConfig {
    pub thread_count: usize,
    pub repeat_index: usize,
    pub step_count: usize,
    pub warmup_steps: usize,
}

pub struct Frame {
    pub frame_type: u16,
    pub sequence: u32,
    pub payload: Vec<u8>,
}

pub fn run<I>(args: I) -> Result<(), u8>
where
    I: Iterator<Item = String>,
{
    let cli = parse_args(args)?;
    let descriptor = match case_registry::resolve(&cli.case_id) {
        Ok(descriptor) => descriptor,
        Err(_) => {
            eprintln!("invalid_argument name=case value={}", cli.case_id);
            return Err(2);
        }
    };
    if let Err(error) = rapier3d::rayon::ThreadPoolBuilder::new()
        .num_threads(cli.thread_count)
        .build_global()
    {
        eprintln!("run_failed reason=thread_pool error={error}");
        return Err(2);
    }
    if rapier3d::rayon::current_num_threads() != cli.thread_count {
        eprintln!("run_failed reason=thread_pool_mismatch");
        return Err(2);
    }

    let mut stream = match TcpStream::connect((cli.connect_host.as_str(), cli.connect_port)) {
        Ok(stream) => stream,
        Err(error) => {
            eprintln!("run_failed reason=connect error={error}");
            return Err(2);
        }
    };

    let hello = receive_frame(&mut stream, FRAME_HELLO, 1)?;
    decode_hello(&cli, &hello.payload)?;
    send_hello_ack(&mut stream, &cli, descriptor, 1)?;

    let start = receive_frame(&mut stream, FRAME_START, 2)?;
    let config = decode_start(&cli, &start.payload)?;
    let mut world = match case_registry::create_world(descriptor) {
        Ok(world) => world,
        Err(_) => {
            eprintln!("run_failed reason=create_world");
            send_completed(&mut stream, FRAME_STARTED, 2, STATUS_VISUAL_FAILED, 0, 0.0)?;
            return Err(2);
        }
    };
    send_completed(&mut stream, FRAME_STARTED, 2, STATUS_OK, 0, 0.0)?;

    let warmup = receive_frame(&mut stream, FRAME_WARMUP, 3)?;
    let warmup_steps = decode_step(&warmup.payload)?;
    let warmup_status = case_registry::run_warmup(descriptor, warmup_steps);
    send_completed(
        &mut stream,
        FRAME_COMPLETED,
        3,
        if warmup_status == 0 { STATUS_OK } else { STATUS_VISUAL_FAILED },
        0,
        0.0,
    )?;
    if warmup_status != 0 {
        eprintln!("run_failed reason=warmup status={warmup_status}");
        return Err(2);
    }

    let mut completed_steps = 0usize;
    let mut physics_elapsed_ms = 0.0f64;
    let mut transforms = vec![rapier_case::RapierTransform { position_x: 0.0, position_y: 0.0, position_z: 0.0, rotation_x: 0.0, rotation_y: 0.0, rotation_z: 0.0, rotation_w: 1.0 }; descriptor.dynamic_body_count];
    let mut boxes = vec![rapier_case::RapierStaticBox { position_x: 0.0, position_y: 0.0, position_z: 0.0, half_extent_x: 0.0, half_extent_y: 0.0, half_extent_z: 0.0 }; descriptor.static_body_count];
    let mut snapshot_payload = Vec::with_capacity(SNAPSHOT_HEADER_BYTES + descriptor.dynamic_body_count * TRANSFORM_BYTES + descriptor.static_body_count * STATIC_BOX_BYTES);
    let mut sequence = 4u32;
    loop {
        let frame = receive_any_frame(&mut stream, sequence)?;
        if frame.frame_type == FRAME_SHUTDOWN {
            send_completed(&mut stream, FRAME_COMPLETED, sequence, STATUS_OK, completed_steps as u32, physics_elapsed_ms)?;
            return Ok(());
        }
        if frame.frame_type != FRAME_STEP {
            eprintln!("run_failed reason=unexpected_frame frame_type={}", frame.frame_type);
            return Err(2);
        }
        let step_count = decode_step(&frame.payload)?;
        let start_time = Instant::now();
        case_registry::step_world(descriptor, &mut world, step_count);
        physics_elapsed_ms += start_time.elapsed().as_secs_f64() * 1000.0;
        completed_steps += step_count;
        build_snapshot_payload(descriptor, &config, &world, completed_steps, physics_elapsed_ms, &mut transforms, &mut boxes, &mut snapshot_payload)?;
        send_frame(&mut stream, FRAME_SNAPSHOT, sequence, &snapshot_payload)?;
        sequence += 1;
    }
}

pub fn parse_args<I>(args: I) -> Result<SharedVisualCli, u8>
where
    I: Iterator<Item = String>,
{
    let mut cli = SharedVisualCli {
        shared_visual_mode: String::new(),
        connect_host: "127.0.0.1".to_string(),
        run_token: String::new(),
        engine_id: String::new(),
        case_id: String::new(),
        connect_port: 0,
        thread_count: 1,
        repeat_index: 0,
        step_count: 300,
        warmup_steps: 0,
    };
    for arg in args {
        if let Some(value) = arg.strip_prefix("--producer-mode=") {
            cli.shared_visual_mode = value.to_string();
        } else if let Some(value) = arg.strip_prefix("--connect-host=") {
            cli.connect_host = value.to_string();
        } else if let Some(value) = arg.strip_prefix("--connect-port=") {
            cli.connect_port = parse_number("connect-port", value, 1)? as u16;
        } else if let Some(value) = arg.strip_prefix("--run-token=") {
            cli.run_token = value.to_string();
        } else if let Some(value) = arg.strip_prefix("--engine=") {
            cli.engine_id = value.to_string();
        } else if let Some(value) = arg.strip_prefix("--case=") {
            cli.case_id = value.to_string();
        } else if let Some(value) = arg.strip_prefix("--thread-count=") {
            cli.thread_count = parse_number("thread-count", value, 1)?;
        } else if let Some(value) = arg.strip_prefix("--repeat-index=") {
            cli.repeat_index = parse_number("repeat-index", value, 0)?;
        } else if let Some(value) = arg.strip_prefix("--step-count=") {
            cli.step_count = parse_number("step-count", value, 1)?;
        } else if let Some(value) = arg.strip_prefix("--warmup-steps=") {
            cli.warmup_steps = parse_number("warmup-steps", value, 0)?;
        } else {
            eprintln!("invalid_argument value={arg}");
            return Err(2);
        }
    }
    if cli.shared_visual_mode != "shared-visual"
        || cli.connect_host != "127.0.0.1"
        || cli.engine_id != case_registry::default_case().engine_id
        || cli.case_id.is_empty()
        || cli.run_token.is_empty()
        || cli.connect_port == 0
    {
        eprintln!("invalid_argument reason=shared_visual_mode_contract");
        return Err(2);
    }
    Ok(cli)
}

pub fn parse_number(name: &str, value: &str, minimum: usize) -> Result<usize, u8> {
    let parsed = match value.parse::<usize>() {
        Ok(number) => number,
        Err(_) => {
            eprintln!("invalid_argument name={name} value={value}");
            return Err(2);
        }
    };
    if parsed < minimum || parsed > 1_000_000 {
        eprintln!("invalid_argument name={name} value={value}");
        return Err(2);
    }
    Ok(parsed)
}

pub fn receive_any_frame(stream: &mut TcpStream, sequence: u32) -> Result<Frame, u8> {
    let mut header = [0u8; 20];
    stream.read_exact(&mut header).map_err(|error| {
        eprintln!("run_failed reason=read_frame_header sequence={sequence} error={error}");
        2u8
    })?;
    let magic = read_u32(&header, 0);
    let version = read_u16(&header, 4);
    let frame_type = read_u16(&header, 6);
    let actual_sequence = read_u32(&header, 8);
    let payload_bytes = read_u32(&header, 12) as usize;
    let crc = read_u32(&header, 16);
    if magic != MAGIC || version != VERSION || actual_sequence != sequence || payload_bytes > MAX_PAYLOAD_BYTES || crc != header_crc(frame_type, actual_sequence, payload_bytes as u32) {
        eprintln!("run_failed reason=invalid_frame_header sequence={sequence} frame_type={frame_type} payload_bytes={payload_bytes}");
        return Err(2);
    }
    let mut payload = vec![0u8; payload_bytes];
    if payload_bytes > 0 {
        stream.read_exact(&mut payload).map_err(|error| {
            eprintln!("run_failed reason=read_frame_payload sequence={sequence} error={error}");
            2u8
        })?;
    }
    Ok(Frame { frame_type, sequence, payload })
}

pub fn receive_frame(stream: &mut TcpStream, frame_type: u16, sequence: u32) -> Result<Frame, u8> {
    let frame = receive_any_frame(stream, sequence)?;
    if frame.frame_type != frame_type {
        eprintln!("run_failed reason=unexpected_frame expected={frame_type} actual={} sequence={sequence}", frame.frame_type);
        return Err(2);
    }
    Ok(frame)
}

pub fn send_frame(stream: &mut TcpStream, frame_type: u16, sequence: u32, payload: &[u8]) -> Result<(), u8> {
    let mut header = Vec::with_capacity(20);
    write_u32(&mut header, MAGIC);
    write_u16(&mut header, VERSION);
    write_u16(&mut header, frame_type);
    write_u32(&mut header, sequence);
    write_u32(&mut header, payload.len() as u32);
    write_u32(&mut header, header_crc(frame_type, sequence, payload.len() as u32));
    stream.write_all(&header).map_err(|error| {
        eprintln!("run_failed reason=write_frame_header frame_type={frame_type} sequence={sequence} error={error}");
        2u8
    })?;
    if !payload.is_empty() {
        stream.write_all(payload).map_err(|error| {
            eprintln!("run_failed reason=write_frame_payload frame_type={frame_type} sequence={sequence} error={error}");
            2u8
        })?;
    }
    Ok(())
}

pub fn send_hello_ack(stream: &mut TcpStream, cli: &SharedVisualCli, descriptor: case_registry::RapierCaseDescriptor, sequence: u32) -> Result<(), u8> {
    let mut payload = Vec::with_capacity(208);
    write_text(&mut payload, descriptor.engine_id);
    write_text(&mut payload, descriptor.case_id);
    write_text(&mut payload, "rapier3d_polygon_runner");
    write_u32(&mut payload, std::process::id());
    write_u32(&mut payload, 0);
    write_u32(&mut payload, hash_text(&cli.run_token));
    write_u32(&mut payload, STATUS_OK);
    send_frame(stream, FRAME_HELLO_ACK, sequence, &payload)
}

pub fn send_completed(stream: &mut TcpStream, frame_type: u16, sequence: u32, status: u32, completed_steps: u32, physics_elapsed_ms: f64) -> Result<(), u8> {
    let mut payload = Vec::with_capacity(16);
    write_u32(&mut payload, status);
    write_u32(&mut payload, completed_steps);
    write_f64(&mut payload, physics_elapsed_ms);
    send_frame(stream, frame_type, sequence, &payload)
}

pub fn decode_hello(cli: &SharedVisualCli, payload: &[u8]) -> Result<(), u8> {
    if payload.len() != TEXT_CAPACITY * 3 {
        eprintln!("run_failed reason=hello_payload_size bytes={}", payload.len());
        return Err(2);
    }
    if read_text(payload, 0) != cli.run_token || read_text(payload, 64) != cli.engine_id || read_text(payload, 128) != cli.case_id {
        eprintln!("run_failed reason=hello_payload_identity");
        return Err(2);
    }
    Ok(())
}

pub fn decode_start(cli: &SharedVisualCli, payload: &[u8]) -> Result<CaseRunConfig, u8> {
    if payload.len() != 156 {
        eprintln!("run_failed reason=start_payload_size bytes={}", payload.len());
        return Err(2);
    }
    if read_text(payload, 0) != cli.case_id || read_text(payload, 64) != cli.engine_id {
        eprintln!("run_failed reason=start_payload_identity");
        return Err(2);
    }
    Ok(CaseRunConfig {
        thread_count: read_u32(payload, 128) as usize,
        repeat_index: read_u32(payload, 132) as usize,
        step_count: read_u32(payload, 136) as usize,
        warmup_steps: read_u32(payload, 140) as usize,
    })
}

pub fn decode_step(payload: &[u8]) -> Result<usize, u8> {
    if payload.len() != 4 {
        eprintln!("run_failed reason=step_payload_size bytes={}", payload.len());
        return Err(2);
    }
    Ok(read_u32(payload, 0) as usize)
}

pub fn build_snapshot_payload(
    descriptor: case_registry::RapierCaseDescriptor,
    config: &CaseRunConfig,
    world: &rapier_case::RapierWorld,
    completed_steps: usize,
    physics_elapsed_ms: f64,
    transforms: &mut [rapier_case::RapierTransform],
    boxes: &mut [rapier_case::RapierStaticBox],
    payload: &mut Vec<u8>,
) -> Result<(), u8> {
    case_registry::sample_transforms(descriptor, world, transforms).map_err(|_| {
        eprintln!("run_failed reason=sample_transforms");
        2u8
    })?;
    case_registry::copy_static_boxes(descriptor, boxes).map_err(|_| {
        eprintln!("run_failed reason=copy_static_boxes");
        2u8
    })?;
    let payload_bytes = SNAPSHOT_HEADER_BYTES + descriptor.dynamic_body_count * TRANSFORM_BYTES + descriptor.static_body_count * STATIC_BOX_BYTES;
    payload.clear();
    payload.reserve(payload_bytes);
    write_text(payload, descriptor.case_id);
    write_text(payload, descriptor.engine_id);
    write_text(payload, descriptor.fixture_semantic);
    write_text(payload, descriptor.fixture_version);
    write_u32(payload, config.thread_count as u32);
    write_u32(payload, config.repeat_index as u32);
    write_u32(payload, config.step_count as u32);
    write_u32(payload, config.warmup_steps as u32);
    write_u32(payload, descriptor.body_count as u32);
    write_u32(payload, descriptor.body_count as u32);
    write_u32(payload, descriptor.static_body_count as u32);
    write_u32(payload, 0);
    write_f64(payload, physics_elapsed_ms);
    write_f64(payload, 0.0);
    write_f64(payload, 0.0);
    write_f64(payload, 0.0);
    write_f64(payload, 0.0);
    write_u32(payload, descriptor.dynamic_body_count as u32);
    write_u32(payload, descriptor.static_body_count as u32);
    write_u32(payload, completed_steps as u32);
    write_u32(payload, STATUS_OK);
    write_u32(payload, 0);
    write_f32(payload, descriptor.dynamic_half_extent);
    write_f32(payload, descriptor.dynamic_half_extent);
    write_f32(payload, descriptor.dynamic_half_extent);
    for transform in transforms.iter().take(descriptor.dynamic_body_count) {
        write_f32(payload, transform.position_x);
        write_f32(payload, transform.position_y);
        write_f32(payload, transform.position_z);
        write_f32(payload, transform.rotation_x);
        write_f32(payload, transform.rotation_y);
        write_f32(payload, transform.rotation_z);
        write_f32(payload, transform.rotation_w);
    }
    for static_box in boxes.iter().take(descriptor.static_body_count) {
        write_f32(payload, static_box.position_x);
        write_f32(payload, static_box.position_y);
        write_f32(payload, static_box.position_z);
        write_f32(payload, static_box.half_extent_x);
        write_f32(payload, static_box.half_extent_y);
        write_f32(payload, static_box.half_extent_z);
    }
    Ok(())
}

pub fn header_crc(frame_type: u16, sequence: u32, payload_bytes: u32) -> u32 {
    MAGIC ^ ((VERSION as u32) << 16) ^ frame_type as u32 ^ sequence ^ payload_bytes ^ 0x9e3779b9
}

pub fn hash_text(value: &str) -> u32 {
    let mut hash = 2166136261u32;
    for byte in value.as_bytes() {
        hash ^= *byte as u32;
        hash = hash.wrapping_mul(16777619);
    }
    hash
}

pub fn write_text(buffer: &mut Vec<u8>, value: &str) {
    let bytes = value.as_bytes();
    let count = bytes.len().min(TEXT_CAPACITY - 1);
    buffer.extend_from_slice(&bytes[..count]);
    buffer.resize(buffer.len() + TEXT_CAPACITY - count, 0);
}

pub fn read_text(payload: &[u8], offset: usize) -> String {
    let bytes = &payload[offset..offset + TEXT_CAPACITY];
    let end = bytes.iter().position(|byte| *byte == 0).unwrap_or(TEXT_CAPACITY);
    String::from_utf8_lossy(&bytes[..end]).to_string()
}

pub fn read_u16(payload: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes([payload[offset], payload[offset + 1]])
}

pub fn read_u32(payload: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([payload[offset], payload[offset + 1], payload[offset + 2], payload[offset + 3]])
}

pub fn write_u16(buffer: &mut Vec<u8>, value: u16) {
    buffer.extend_from_slice(&value.to_le_bytes());
}

pub fn write_u32(buffer: &mut Vec<u8>, value: u32) {
    buffer.extend_from_slice(&value.to_le_bytes());
}

pub fn write_f32(buffer: &mut Vec<u8>, value: f32) {
    buffer.extend_from_slice(&value.to_le_bytes());
}

pub fn write_f64(buffer: &mut Vec<u8>, value: f64) {
    buffer.extend_from_slice(&value.to_le_bytes());
}
