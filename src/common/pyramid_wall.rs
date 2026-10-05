use crate::case_execution_wire::{CaseExecutionPyramidWall, CaseExecutionVector3};
use crate::result_writer::{ObservationRow, ObservationValue};

#[derive(Clone, Copy, Default)]
pub struct PyramidWallObservation
{
    pub centre_of_mass_height: f64,
    pub lateral_rms: f64,
    pub translational_energy: f64,
    pub rotational_energy: f64,
    pub potential_energy: f64,
    pub floor_penetration: f64,
    pub escaped_bodies: u64,
    pub invalid_bodies: u64,
    pub elapsed_ms: f64,
}

pub fn position(wall: &CaseExecutionPyramidWall, mut index: u32) -> CaseExecutionVector3
{
    let mut row: u32 = 0;
    let mut width: u32 = wall.row_count;
    while index >= width
    {
        index -= width;
        width -= 1;
        row += 1;
    }
    let h: f32 = wall.half_extent;
    CaseExecutionVector3
    {
        x: (row + 1) as f32 * h + 2.0 * index as f32 * h - h * wall.row_count as f32,
        y: (2 * row + 1) as f32 * h,
        z: 0.0,
    }
}

pub fn observation_step(measured: u32, ordinal: u32) -> u32
{
    if measured < 4 || ordinal == 0
    {
        ordinal + 1
    }
    else if ordinal == 3
    {
        measured
    }
    else
    {
        (ordinal + 1).max((measured + 1) / if ordinal == 1 { 4 } else { 2 })
    }
}

pub fn observation_index(completed: u32, measured: u32) -> Option<usize>
{
    (0..4.min(measured)).find(|&ordinal| completed == observation_step(measured, ordinal)).map(|ordinal| ordinal as usize)
}

pub fn accumulate(wall: &CaseExecutionPyramidWall, index: u32, p: [f32; 3], q: [f32; 4],
    linear: [f32; 3], angular: [f32; 3], mass: f64, rotational_energy: f64,
    sleep_matches: i32, gravity: CaseExecutionVector3, sample: &mut PyramidWallObservation)
{
    if p.iter().chain(q.iter()).chain(linear.iter()).chain(angular.iter()).any(|value| !value.is_finite()) ||
        !mass.is_finite() || !rotational_energy.is_finite()
    {
        sample.invalid_bodies += 1;
        return;
    }
    if sleep_matches == 0 || mass <= 0.0
    {
        sample.invalid_bodies += 1;
    }
    let initial: CaseExecutionVector3 = position(wall, index);
    let dx: f64 = p[0] as f64 - initial.x as f64;
    let dz: f64 = p[2] as f64 - initial.z as f64;
    sample.centre_of_mass_height += p[1] as f64;
    sample.lateral_rms += dx * dx + dz * dz;
    sample.translational_energy += 0.5 * mass * linear.iter().map(|&v| v as f64 * v as f64).sum::<f64>();
    sample.rotational_energy += rotational_energy;
    sample.potential_energy -= mass * (gravity.x as f64 * p[0] as f64 + gravity.y as f64 * p[1] as f64 + gravity.z as f64 * p[2] as f64);
    let [x, y, z, w]: [f64; 4] = q.map(f64::from);
    let h: f64 = wall.half_extent as f64;
    let sx: f64 = h * ((1.0 - 2.0 * (y*y + z*z)).abs() + (2.0 * (x*y-z*w)).abs() + (2.0 * (x*z+y*w)).abs());
    let sy: f64 = h * ((2.0 * (x*y+z*w)).abs() + (1.0 - 2.0 * (x*x+z*z)).abs() + (2.0 * (y*z-x*w)).abs());
    let sz: f64 = h * ((2.0 * (x*z-y*w)).abs() + (2.0 * (y*z+x*w)).abs() + (1.0 - 2.0 * (x*x+y*y)).abs());
    sample.floor_penetration = sample.floor_penetration.max(sy - p[1] as f64);
    if (p[0] as f64).abs() + sx > wall.floor_half_extents.x as f64 ||
        (p[2] as f64).abs() + sz > wall.floor_half_extents.z as f64
    {
        sample.escaped_bodies += 1;
    }
}

pub fn finish(count: u32, sample: &mut PyramidWallObservation)
{
    sample.centre_of_mass_height /= count as f64;
    sample.lateral_rms = (sample.lateral_rms / count as f64).sqrt();
}

pub fn observation_rows(measured: u32, samples: &[PyramidWallObservation; 4], initial_energy: f64) -> Vec<ObservationRow>
{
    const IDS: [&str; 9] = ["centre_of_mass_height", "lateral_rms", "translational_energy", "rotational_energy",
        "potential_energy", "floor_penetration", "escaped_body_count", "invalid_body_count", "observation_elapsed_ms"];
    let count: u32 = measured.min(4);
    let mut rows: Vec<ObservationRow> = Vec::with_capacity((9 * count + 1) as usize);
    for field in 0..9
    {
        if field == 8
        {
            rows.push(ObservationRow
            {
                metric_id: "initial_potential_energy", phase_id: "construction", sample_index: 0,
                value: ObservationValue::Float64(initial_energy.to_bits()),
            });
        }
        for ordinal in 0..count
        {
            let sample: PyramidWallObservation = samples[ordinal as usize];
            let values: [f64; 9] = [sample.centre_of_mass_height, sample.lateral_rms, sample.translational_energy,
                sample.rotational_energy, sample.potential_energy, sample.floor_penetration, 0.0, 0.0, sample.elapsed_ms];
            rows.push(ObservationRow
            {
                metric_id: IDS[field], phase_id: "observation", sample_index: observation_step(measured, ordinal),
                value: match field
                {
                    6 => ObservationValue::Uint64(sample.escaped_bodies),
                    7 => ObservationValue::Uint64(sample.invalid_bodies),
                    _ => ObservationValue::Float64(values[field].to_bits()),
                },
            });
        }
    }
    rows
}
