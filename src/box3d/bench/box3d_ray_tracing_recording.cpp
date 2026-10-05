#include "box3d_ray_tracing_recording.h"

#include <cstring>

namespace box3d_benchmark
{
using namespace benchmark_ray;

Status BeginBox3DRayRecording(Box3DRayRecording* recording, const Box3DRunRequest& request)
{
	if (request.recordingMode == benchmark_replay::RecordingMode_Off)
		return Status_Ok;
	recording->path = request.recordingPath;
	recording->partial = recording->path.string() + ".partial";
	if (std::filesystem::exists(recording->partial))
		return Status_Io;
	recording->output.open(recording->partial, std::ios::binary | std::ios::trunc);
	recording->header = {0x31485452,
	                     1,
	                     static_cast<std::uint32_t>(request.threadCount),
	                     static_cast<std::uint32_t>(request.repeatIndex),
	                     request.caseExecution.rayTracing.width,
	                     request.caseExecution.rayTracing.height,
	                     0,
	                     0};
	std::memcpy(reinterpret_cast<char*>(recording->header.data()) + 32, "box3d", 5);
	recording->output.write(reinterpret_cast<const char*>(recording->header.data()), sizeof(recording->header));
	return recording->output ? Status_Ok : Status_Io;
}

Status AppendBox3DRayRecording(Box3DRayRecording* recording, const CorpusPhase& corpus,
                               const std::vector<Output>& outputs, const std::vector<Hit>& hits, std::uint32_t suite)
{
	const Phase phase = corpus.phase;
	if (!recording->output.is_open() || suite != corpus.view ||
	    (phase != Phase_Primary && phase != Phase_Shadow && phase != Phase_Reflection && phase != Phase_Ambient &&
	     phase != Phase_Updated))
		return Status_Ok;
	const std::array<std::uint32_t, 8> header = {corpus.view,
	                                             phase,
	                                             Api_Ordinary,
	                                             static_cast<std::uint32_t>(outputs.size()),
	                                             static_cast<std::uint32_t>(hits.size()),
	                                             static_cast<std::uint32_t>(outputs.size()),
	                                             0,
	                                             0};
	recording->output.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
	recording->output.write(reinterpret_cast<const char*>(outputs.data()),
	                        static_cast<std::streamsize>(outputs.size() * sizeof(Output)));
	recording->output.write(reinterpret_cast<const char*>(hits.data()),
	                        static_cast<std::streamsize>(hits.size() * sizeof(Hit)));
	recording->header[6] += 1;
	return recording->output ? Status_Ok : Status_Io;
}

Status CompleteBox3DRayRecording(Box3DRayRecording* recording)
{
	if (!recording->output.is_open())
		return Status_Ok;
	recording->header[7] = 1;
	recording->output.seekp(0);
	recording->output.write(reinterpret_cast<const char*>(recording->header.data()), sizeof(recording->header));
	recording->output.close();
	if (!recording->output)
		return Status_Io;
	std::error_code error;
	std::filesystem::rename(recording->partial, recording->path, error);
	return error ? Status_Io : Status_Ok;
}

}
