use crate::case_registry;
use crate::case_execution_wire::{self, CaseExecutionSpec};

#[derive(Clone, Copy)]
pub enum RecordingMode
{
    Off,
    On,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum VerificationMode
{
    On,
    Off,
}

pub struct RunnerArgs
{
    pub case_registration: &'static case_registry::CaseRegistration,
    pub case_execution: CaseExecutionSpec,
    pub output_path: String,
    pub stack_stream: String,
    pub recording_path: String,
    pub recording_mode: RecordingMode,
    pub verification_mode: VerificationMode,
    pub step_timing_output_path: Option<String>,
    pub thread_count: usize,
    pub step_count: usize,
    pub warmup_steps: usize,
    pub repeat_index: usize,
}

pub fn parse_args<I>(args: I) -> Result<RunnerArgs, i32>
where
    I: Iterator<Item = String>,
{
    let mut case_contract: Option<CaseExecutionSpec> = None;
    let mut recording_path: Option<String> = None;
    let mut stack_stream: Option<String> = None;
    let mut output_path: String = "polygon_results.csv".to_string();
    let mut step_timing_output_path: Option<String> = None;
    let mut thread_count: usize = 1;
    let mut repeat_index: usize = 0;
    let mut verification_mode: Option<VerificationMode> = None;

    for arg in args
    {
        if let Some(value) = arg.strip_prefix("--case-contract=")
        {
            if case_contract.is_some()
            {
                eprintln!("invalid_argument name=case-contract reason=duplicate");
                return Err(2);
            }
            case_contract = Some(case_execution_wire::decode_hex(value)?);
        }
        else if let Some(value) = arg.strip_prefix("--stack-stream=")
        {
            if stack_stream.is_some() || !value.starts_with(r"\\.\pipe\") || value.len() <= 9
            {
                return Err(2);
            }
            stack_stream = Some(value.to_string());
        }
        else if let Some(value) = arg.strip_prefix("--verify=")
        {
            if verification_mode.is_some()
            {
                return Err(2);
            }
            verification_mode = Some(match value
            {
                "on" => VerificationMode::On,
                "off" => VerificationMode::Off,
                _ => return Err(2),
            });
        }
        else if let Some(value) = arg.strip_prefix("--thread-count=")
        {
            thread_count = parse_number("thread-count", value, 1)?;
        }
        else if let Some(value) = arg.strip_prefix("--repeat-index=")
        {
            repeat_index = parse_number("repeat-index", value, 0)?;
        }
        else if let Some(value) = arg.strip_prefix("--output=")
        {
            output_path = value.to_string();
        }
        else if let Some(value) = arg.strip_prefix("--recording-output=")
        {
            if value.is_empty() || recording_path.is_some()
            {
                return Err(2);
            }
            recording_path = Some(value.to_string());
        }
        else if let Some(value) = arg.strip_prefix("--step-timing-output=")
        {
            if value.is_empty()
            {
                eprintln!("invalid_argument name=step-timing-output value={value}");
                return Err(2);
            }
            step_timing_output_path = Some(value.to_string());
        }
        else
        {
            eprintln!("invalid_argument value={arg}");
            return Err(2);
        }
    }

    let (recording_mode, recording_path): (RecordingMode, String) = match recording_path
    {
        Some(path) => (RecordingMode::On, path),
        None => (RecordingMode::Off, String::new()),
    };
    let case_execution: CaseExecutionSpec = case_contract.ok_or_else(||
    {
        eprintln!("invalid_argument name=case-contract reason=missing");
        2
    })?;
    let verification_mode: VerificationMode = verification_mode.unwrap_or(VerificationMode::On);
    let requires_stream: usize = match case_execution.fixture_kind
    {
        case_execution_wire::CaseFixtureKind::OpenContainerFallingPile | case_execution_wire::CaseFixtureKind::BoxContactIslands |
        case_execution_wire::CaseFixtureKind::LargePyramid | case_execution_wire::CaseFixtureKind::PyramidWall => match verification_mode
        {
            VerificationMode::On => 1,
            VerificationMode::Off => 0,
        },
        _ => 0,
    };
    if (requires_stream == 1) != stack_stream.is_some()
    {
        return Err(2);
    }
    let case_registration: &'static case_registry::CaseRegistration = case_registry::resolve(&case_execution)?;
    Ok(RunnerArgs
    {
        case_registration,
        case_execution,
        output_path,
        stack_stream: stack_stream.unwrap_or_default(),
        recording_path,
        recording_mode,
        verification_mode,
        step_timing_output_path,
        thread_count,
        step_count: case_execution.measured_work_unit_count as usize,
        warmup_steps: case_execution.warmup_work_unit_count as usize,
        repeat_index,
    })
}

pub fn parse_number(name: &str, value: &str, minimum: usize) -> Result<usize, i32>
{
    let parsed: usize = match value.parse::<usize>()
    {
        Ok(number) => number,
        Err(_) =>
        {
            eprintln!("invalid_argument name={name} value={value}");
            return Err(2);
        }
    };
    if parsed < minimum || parsed > 1_000_000
    {
        eprintln!("invalid_argument name={name} value={value}");
        return Err(2);
    }
    Ok(parsed)
}
