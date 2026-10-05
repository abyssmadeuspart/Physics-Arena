use crate::case_execution_wire::{CaseExecutionSpec, CaseExecutionVector3};

pub const IDS: [&str; 10] = [
    "joint_anchor_gap_rms_m", "joint_anchor_gap_max_m", "worst_joint_id",
    "worst_joint_step", "joint_sample_count", "body_sample_count",
    "invalid_body_sample_count", "missing_body_sample_count",
    "first_invalid_body_id", "first_invalid_step",
];

#[derive(Clone, Copy, Default)]
pub struct Quality
{
    pub squared_gap_sum: f64,
    pub maximum_gap: f64,
    pub joint_samples: u64,
    pub body_samples: u64,
    pub invalid_body_samples: u64,
    pub missing_body_samples: u64,
    pub worst_joint: u64,
    pub worst_step: u64,
    pub first_invalid_body: u64,
    pub first_invalid_step: u64,
}

#[derive(Clone, Copy)]
pub struct Pose
{
    pub position: [f32; 3],
    pub rotation: [f32; 4],
}

pub fn valid(pose: Pose) -> i32
{
    if pose.position.iter().chain(pose.rotation.iter()).all(|value| value.is_finite())
        && pose.rotation.iter().any(|value| *value != 0.0)
    {
        1
    }
    else
    {
        0
    }
}

pub fn anchor(pose: Pose, local: CaseExecutionVector3) -> [f64; 3]
{
    let q: [f64; 4] = pose.rotation.map(f64::from);
    let v: [f64; 3] = [local.x as f64, local.y as f64, local.z as f64];
    let t: [f64; 3] = [2.0 * (q[1] * v[2] - q[2] * v[1]),
        2.0 * (q[2] * v[0] - q[0] * v[2]), 2.0 * (q[0] * v[1] - q[1] * v[0])];
    [pose.position[0] as f64 + v[0] + q[3] * t[0] + q[1] * t[2] - q[2] * t[1],
        pose.position[1] as f64 + v[1] + q[3] * t[1] + q[2] * t[0] - q[0] * t[2],
        pose.position[2] as f64 + v[2] + q[3] * t[2] + q[0] * t[1] - q[1] * t[0]]
}

pub fn accumulate<F: Fn(usize) -> Option<Pose>>(
    execution: &CaseExecutionSpec, pose_at: F, step: usize, quality: &mut Quality)
{
    for body in 0..execution.dynamic_body_count as usize
    {
        match pose_at(body)
        {
            Some(pose) =>
            {
                quality.body_samples += 1;
                if valid(pose) != 0
                {
                    continue;
                }
                quality.invalid_body_samples += 1;
            }
            None => quality.missing_body_samples += 1,
        }
        if quality.first_invalid_step == 0
        {
            quality.first_invalid_body = body as u64;
            quality.first_invalid_step = step as u64;
        }
    }
    let fixture: &crate::case_execution_wire::CaseExecutionRagdoll = &execution.ragdoll;
    for ragdoll in 0..(fixture.ragdoll_grid[0] * fixture.ragdoll_grid[1]) as usize
    {
        for joint in 0..fixture.link_count as usize
        {
            let link: &crate::case_execution_wire::CaseExecutionRagdollLink = &fixture.links[joint];
            let Some(parent) = pose_at(ragdoll * fixture.part_count as usize + link.parent_part as usize) else
            {
                continue;
            };
            let Some(child) = pose_at(ragdoll * fixture.part_count as usize + link.child_part as usize) else
            {
                continue;
            };
            if valid(parent) == 0 || valid(child) == 0
            {
                continue;
            }
            let a: [f64; 3] = anchor(parent, link.parent_local_anchor);
            let b: [f64; 3] = anchor(child, link.child_local_anchor);
            let square: f64 = (a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2) + (a[2] - b[2]).powi(2);
            let gap: f64 = square.sqrt();
            quality.squared_gap_sum += square;
            quality.joint_samples += 1;
            if quality.worst_step == 0 || gap > quality.maximum_gap
            {
                quality.maximum_gap = gap;
                quality.worst_joint = (ragdoll * fixture.link_count as usize + joint) as u64;
                quality.worst_step = step as u64;
            }
        }
    }
}

pub fn values(quality: &Quality) -> Result<[u64; 10], i32>
{
    if quality.joint_samples == 0
    {
        return Err(2);
    }
    Ok([(quality.squared_gap_sum / quality.joint_samples as f64).sqrt().to_bits(),
        quality.maximum_gap.to_bits(), quality.worst_joint, quality.worst_step,
        quality.joint_samples, quality.body_samples, quality.invalid_body_samples,
        quality.missing_body_samples, quality.first_invalid_body, quality.first_invalid_step])
}
