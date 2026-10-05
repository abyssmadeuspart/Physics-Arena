#include "box3d_ray_tracing_results.h"

#include <string_view>
#include <iomanip>
#include <limits>

namespace box3d_benchmark
{
using namespace benchmark_ray;

void WriteRayPhase(std::ostream& output, const Box3DRunRequest& request, std::uint32_t view, std::uint32_t suite,
                   Phase phase, Api api, const char* status, std::uint64_t queries, const Validation& validation,
                   double queryMs, double updateMs, double conditioningMs, double validationMs, double setupMs,
                   double suiteMs, std::uint64_t bufferBytes)
{
	output << "box3d," << request.threadCount << ',' << request.repeatIndex << ',' << view << ',' << suite << ','
	       << PhaseName(phase) << ',' << ApiName(api) << ',' << status;
	if (std::string_view(status) == "unsupported")
	{
		output << ",,,,,,,,,,,\n";
		return;
	}
	output << ',' << queries << ',' << validation.hitRays << ',' << queryMs << ',' << updateMs << ',' << conditioningMs
	       << ',' << validationMs << ',' << validation.errors << ',' << validation.written << ',' << setupMs << ','
	       << suiteMs << ',' << bufferBytes << '\n';
}

Status WriteBox3DRayFailures(std::ofstream& output, const CorpusPhase& corpus, const std::vector<Output>& outputs,
                           const std::vector<Hit>& hits, const Validation& validation)
{
	output << std::setprecision(std::numeric_limits<double>::max_digits10);
	for (std::uint32_t failure = 0; failure < validation.failingCount; ++failure)
	{
		const std::uint32_t index = validation.failingRays[failure];
		const Ray& ray = corpus.rays[index];
		const Output result = outputs[index];
		const Range range = corpus.expected[index];
		output << "view=" << corpus.view << " phase=" << PhaseName(corpus.phase) << " api=ordinary ray=" << index
		       << " pixel=" << ray.pixel << " origin=" << ray.origin.x << ',' << ray.origin.y << ',' << ray.origin.z
		       << " translation=" << ray.translation.x << ',' << ray.translation.y << ',' << ray.translation.z
		       << " length=" << ray.length << " mask=" << ray.mask << " written=" << result.written
		       << " status=" << result.status << " expected_first=" << range.first << " expected_count=" << range.count
		       << " actual_first=" << result.first << " actual_count=" << result.count << '\n';
		if (result.first > hits.size() || result.count > hits.size() - result.first)
		{
			output << "actual_range=invalid\n";
			continue;
		}
		for (std::uint32_t event = 0; event < result.count; ++event)
		{
			const Hit& actual = hits[result.first + event];
			output << "actual_event=" << event << " collider=" << actual.collider << " source=" << actual.source
			       << " distance=" << actual.distance << " normal=" << actual.normal.x << ',' << actual.normal.y
			       << ',' << actual.normal.z << " flags=" << actual.flags << '\n';
		}
	}
	return output ? Status_Ok : Status_Io;
}

Status FlushBox3DRaySuite(std::ostream& output, const Box3DRunRequest& request, std::uint32_t view, std::uint32_t suite,
                          const Box3DRayPhaseResult* rows, std::size_t count, double setupMs, double suiteMs)
{
	for (std::size_t index = 0; index < count; ++index)
	{
		const Box3DRayPhaseResult& row = rows[index];
		WriteRayPhase(output, request, view, suite, row.phase, row.api, row.status, row.queries, row.validation,
		              row.queryMs, row.updateMs, row.conditioningMs, row.validationMs, setupMs, suiteMs,
		              row.bufferBytes);
	}
	return output ? Status_Ok : Status_Io;
}
}
