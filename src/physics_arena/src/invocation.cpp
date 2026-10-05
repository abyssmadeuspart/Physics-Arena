#include "physics_arena/invocation.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <string_view>

namespace physics_arena
{
namespace
{
template <std::size_t Capacity>
ArenaStatus InvocationCopy(std::array<char, Capacity>* destination, std::uint32_t* size, std::string_view source)
{
	if (source.empty() || source.size() >= Capacity)
		return ArenaStatus_InvalidResult;
	std::fill(destination->begin(), destination->end(), '\0');
	std::copy(source.begin(), source.end(), destination->begin());
	*size = static_cast<std::uint32_t>(source.size());
	return ArenaStatus_Ok;
}

ArenaStatus InvocationError(StatusRecord* error, ArenaStatus code, std::string_view detail)
{
	*error = {};
	InvocationCopy(&error->component, &error->componentSize, "invocation");
	InvocationCopy(&error->status, &error->statusSize, ArenaStatusText(code));
	if (InvocationCopy(&error->detail, &error->detailSize, detail) != ArenaStatus_Ok)
		InvocationCopy(&error->detail, &error->detailSize, "invocation_detail_exceeded_capacity");
	error->code = code;
	return code;
}

void UtcTimestamp(EventTimestamp* output)
{
	SYSTEMTIME time = {};
	GetSystemTime(&time);
	char text[32] = {};
	std::snprintf(text, std::size(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", time.wYear, time.wMonth, time.wDay,
	              time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
	InvocationCopy(&output->data, &output->size, text);
}

void InvocationId(std::array<wchar_t, 64>* output)
{
	SYSTEMTIME time = {};
	GetLocalTime(&time);
	std::swprintf(output->data(), output->size(), L"%04u-%02u-%02u_%02u%02u%02u_%lu", time.wYear, time.wMonth,
	              time.wDay, time.wHour, time.wMinute, time.wSecond, static_cast<unsigned long>(GetCurrentProcessId()));
}

template <std::size_t Capacity>
ArenaStatus AppendWide(std::array<wchar_t, Capacity>* output, std::uint32_t* size, std::wstring_view value)
{
	if (*size + value.size() >= Capacity)
		return ArenaStatus_InvalidResult;
	std::copy(value.begin(), value.end(), output->begin() + *size);
	*size += static_cast<std::uint32_t>(value.size());
	(*output)[*size] = L'\0';
	return ArenaStatus_Ok;
}

template <std::size_t Capacity>
ArenaStatus WideToUtf8(const wchar_t* path, std::array<char, Capacity>* output, std::uint32_t* size)
{
	const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, output->data(),
	                                        static_cast<int>(output->size()), nullptr, nullptr);
	if (written <= 1 || written > static_cast<int>(output->size()))
		return ArenaStatus_InvalidResult;
	*size = static_cast<std::uint32_t>(written - 1);
	return ArenaStatus_Ok;
}

template <std::size_t Capacity>
ArenaStatus AppendBytes(std::array<char, Capacity>* output, std::uint32_t* size, std::string_view value)
{
	if (*size + value.size() >= Capacity)
		return ArenaStatus_InvalidResult;
	std::copy(value.begin(), value.end(), output->begin() + *size);
	*size += static_cast<std::uint32_t>(value.size());
	return ArenaStatus_Ok;
}

template <std::size_t Capacity, typename Text>
ArenaStatus AppendCsvField(std::array<char, Capacity>* output, std::uint32_t* size, const Text& value)
{
	if (AppendBytes(output, size, "\"") != ArenaStatus_Ok)
		return ArenaStatus_InvalidResult;
	for (std::uint32_t index = 0; index < value.size; ++index)
	{
		const char character = value.data[index];
		if (character == '"' && AppendBytes(output, size, "\"") != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
		const char normalized = character == '\r' || character == '\n' ? ' ' : character;
		if (AppendBytes(output, size, std::string_view(&normalized, 1)) != ArenaStatus_Ok)
			return ArenaStatus_InvalidResult;
	}
	return AppendBytes(output, size, "\"");
}

template <typename Text> void PushEventText(EventTransport* transport, std::uint64_t* position, const Text& text)
{
	for (std::uint32_t index = 0; index < text.size; ++index)
	{
		transport->textArena[*position % transport->textArena.size()] = text.data[index];
		*position += 1;
	}
}

template <typename Text>
void PopEventText(const EventTransport* transport, std::uint64_t* position, std::uint32_t size, Text* text)
{
	for (std::uint32_t index = 0; index < size; ++index)
	{
		text->data[index] = transport->textArena[*position % transport->textArena.size()];
		*position += 1;
	}
	text->size = size;
}

ArenaStatus WriteBytes(HANDLE handle, const char* data, std::uint32_t size, StatusRecord* error, const char* failure)
{
	DWORD written = 0;
	return WriteFile(handle, data, size, &written, nullptr) && written == size
	           ? ArenaStatus_Ok
			   : InvocationError(error, ArenaStatus_RunFailed, failure);
}

ArenaStatus WriteInvocationRow(const InvocationContext& context, const InvocationEvent& event, StatusRecord* error)
{
	std::array<char, 2048> logRow = {};
	std::uint32_t logSize = 0;
	if (AppendCsvField(&logRow, &logSize, event.component) != ArenaStatus_Ok ||
	    AppendBytes(&logRow, &logSize, ",") != ArenaStatus_Ok ||
	    AppendCsvField(&logRow, &logSize, event.status) != ArenaStatus_Ok ||
	    AppendBytes(&logRow, &logSize, ",") != ArenaStatus_Ok ||
	    AppendCsvField(&logRow, &logSize, event.detail) != ArenaStatus_Ok ||
	    AppendBytes(&logRow, &logSize, "\n") != ArenaStatus_Ok)
		return InvocationError(error, ArenaStatus_InvalidResult, "bench_log_row_capacity");
	if (WriteBytes(static_cast<HANDLE>(context.logHandle), logRow.data(), logSize, error, "bench_log_write_failed") !=
	    ArenaStatus_Ok)
		return ArenaStatus_RunFailed;
	std::array<char, 2304> eventRow = {};
	std::uint32_t eventSize = 0;
	char sequence[32] = {};
	std::snprintf(sequence, std::size(sequence), "%llu", static_cast<unsigned long long>(event.sequence));
	if (AppendCsvField(&eventRow, &eventSize, event.timestampUtc) != ArenaStatus_Ok ||
	    AppendBytes(&eventRow, &eventSize, ",") != ArenaStatus_Ok ||
	    AppendBytes(&eventRow, &eventSize, sequence) != ArenaStatus_Ok ||
	    AppendBytes(&eventRow, &eventSize, ",") != ArenaStatus_Ok ||
	    AppendCsvField(&eventRow, &eventSize, event.component) != ArenaStatus_Ok ||
	    AppendBytes(&eventRow, &eventSize, ",") != ArenaStatus_Ok ||
	    AppendCsvField(&eventRow, &eventSize, event.status) != ArenaStatus_Ok ||
	    AppendBytes(&eventRow, &eventSize, ",") != ArenaStatus_Ok ||
	    AppendCsvField(&eventRow, &eventSize, event.detail) != ArenaStatus_Ok ||
	    AppendBytes(&eventRow, &eventSize, "\n") != ArenaStatus_Ok)
		return InvocationError(error, ArenaStatus_InvalidResult, "events_row_capacity");
	return WriteBytes(static_cast<HANDLE>(context.eventsHandle), eventRow.data(), eventSize, error,
	                  "events_write_failed");
}
}

void InitializeEventTransport(EventTransport* transport)
{
	transport->writeIndex.store(0, std::memory_order_relaxed);
	transport->readIndex.store(0, std::memory_order_relaxed);
	transport->textWritePosition.store(0, std::memory_order_relaxed);
	transport->textReadPosition.store(0, std::memory_order_relaxed);
}

ArenaStatus PushEvent(EventTransport* transport, const InvocationEvent* event)
{
	const std::uint32_t write = transport->writeIndex.load(std::memory_order_relaxed);
	const std::uint32_t next = (write + 1) % static_cast<std::uint32_t>(kEventTransportCapacity);
	if (next == transport->readIndex.load(std::memory_order_acquire))
		return ArenaStatus_InvalidResult;
	const std::uint64_t textSize = static_cast<std::uint64_t>(event->timestampUtc.size) + event->component.size +
	                               event->status.size + event->detail.size;
	std::uint64_t position = transport->textWritePosition.load(std::memory_order_relaxed);
	const std::uint64_t readPosition = transport->textReadPosition.load(std::memory_order_acquire);
	if (textSize > transport->textArena.size() - (position - readPosition))
		return ArenaStatus_InvalidResult;
	EventTransportRecord& record = transport->events[write];
	record = {};
	record.sequence = event->sequence;
	record.textOffset = static_cast<std::uint32_t>(position % transport->textArena.size());
	record.timestampSize = event->timestampUtc.size;
	record.componentSize = event->component.size;
	record.statusSize = event->status.size;
	record.detailSize = event->detail.size;
	PushEventText(transport, &position, event->timestampUtc);
	PushEventText(transport, &position, event->component);
	PushEventText(transport, &position, event->status);
	PushEventText(transport, &position, event->detail);
	transport->textWritePosition.store(position, std::memory_order_relaxed);
	transport->writeIndex.store(next, std::memory_order_release);
	return ArenaStatus_Ok;
}

ArenaStatus PopEvent(EventTransport* transport, InvocationEvent* event)
{
	const std::uint32_t read = transport->readIndex.load(std::memory_order_relaxed);
	if (read == transport->writeIndex.load(std::memory_order_acquire))
		return ArenaStatus_InvalidResult;
	const EventTransportRecord& record = transport->events[read];
	if (record.timestampSize > event->timestampUtc.data.size() || record.componentSize > event->component.data.size() ||
	    record.statusSize > event->status.data.size() || record.detailSize > event->detail.data.size())
		return ArenaStatus_InvalidResult;
	*event = {};
	event->sequence = record.sequence;
	std::uint64_t position = transport->textReadPosition.load(std::memory_order_relaxed);
	if (position % transport->textArena.size() != record.textOffset)
		return ArenaStatus_InvalidResult;
	PopEventText(transport, &position, record.timestampSize, &event->timestampUtc);
	PopEventText(transport, &position, record.componentSize, &event->component);
	PopEventText(transport, &position, record.statusSize, &event->status);
	PopEventText(transport, &position, record.detailSize, &event->detail);
	transport->textReadPosition.store(position, std::memory_order_relaxed);
	transport->readIndex.store((read + 1) % static_cast<std::uint32_t>(kEventTransportCapacity),
	                           std::memory_order_release);
	return ArenaStatus_Ok;
}

ArenaStatus StartInvocation(const wchar_t* repositoryRoot, const char* command, InvocationContext* context,
                            StatusRecord* error)
{
	*context = {};
	*error = {};
	if (repositoryRoot == nullptr || command == nullptr || command[0] == '\0')
		return InvocationError(error, ArenaStatus_InvalidArgument, "invalid_start_arguments");
	constexpr std::size_t widePathCapacity = 4096;
	std::array<wchar_t, widePathCapacity> logs = {};
	std::uint32_t logsSize = 0;
	if (AppendWide(&logs, &logsSize, repositoryRoot) != ArenaStatus_Ok ||
	    (logsSize != 0 && logs[logsSize - 1] != L'/' && logs[logsSize - 1] != L'\\' &&
	     AppendWide(&logs, &logsSize, L"\\") != ArenaStatus_Ok) ||
	    AppendWide(&logs, &logsSize, L"logs") != ArenaStatus_Ok)
		return InvocationError(error, ArenaStatus_InvalidResult, "invocation_path_capacity");
	if (CreateDirectoryW(logs.data(), nullptr) == 0 && GetLastError() != ERROR_ALREADY_EXISTS)
		return InvocationError(error, ArenaStatus_RunFailed, "invocation_logs_create_failed");
	std::array<wchar_t, 64> baseId = {};
	InvocationId(&baseId);
	std::array<wchar_t, 80> selectedId = {};
	std::array<wchar_t, widePathCapacity> directory = {};
	std::uint32_t suffix = 0;
	for (;;)
	{
		if (suffix == 0)
		{
			const std::size_t baseSize = std::wcslen(baseId.data());
			std::copy(baseId.begin(), baseId.begin() + baseSize + 1, selectedId.begin());
		}
		else
			std::swprintf(selectedId.data(), selectedId.size(), L"%ls_%u", baseId.data(), suffix + 1);
		directory = {};
		std::uint32_t directorySize = 0;
		if (AppendWide(&directory, &directorySize, std::wstring_view(logs.data(), logsSize)) != ArenaStatus_Ok ||
		    AppendWide(&directory, &directorySize, L"\\") != ArenaStatus_Ok ||
		    AppendWide(&directory, &directorySize, selectedId.data()) != ArenaStatus_Ok)
			return InvocationError(error, ArenaStatus_InvalidResult, "invocation_path_capacity");
		if (GetFileAttributesW(directory.data()) == INVALID_FILE_ATTRIBUTES)
			break;
		if (++suffix == UINT32_MAX)
			return InvocationError(error, ArenaStatus_RunFailed, "invocation_directory_exhausted");
	}
	if (CreateDirectoryW(directory.data(), nullptr) == 0)
		return InvocationError(error, ArenaStatus_RunFailed, "invocation_directory_create_failed");
	std::array<wchar_t, widePathCapacity> logPath = {};
	std::array<wchar_t, widePathCapacity> eventsPath = {};
	std::uint32_t logPathSize = 0;
	std::uint32_t eventsPathSize = 0;
	if (AppendWide(&logPath, &logPathSize, directory.data()) != ArenaStatus_Ok ||
	    AppendWide(&logPath, &logPathSize, L"\\bench.log") != ArenaStatus_Ok ||
	    AppendWide(&eventsPath, &eventsPathSize, directory.data()) != ArenaStatus_Ok ||
	    AppendWide(&eventsPath, &eventsPathSize, L"\\events.csv") != ArenaStatus_Ok)
		return InvocationError(error, ArenaStatus_InvalidResult, "invocation_path_capacity");
	HANDLE logHandle = CreateFileW(logPath.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
	                               FILE_ATTRIBUTE_NORMAL, nullptr);
	HANDLE eventsHandle = CreateFileW(eventsPath.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
	                                  FILE_ATTRIBUTE_NORMAL, nullptr);
	if (logHandle == INVALID_HANDLE_VALUE || eventsHandle == INVALID_HANDLE_VALUE)
	{
		if (logHandle != INVALID_HANDLE_VALUE)
			CloseHandle(logHandle);
		if (eventsHandle != INVALID_HANDLE_VALUE)
			CloseHandle(eventsHandle);
		return InvocationError(error, ArenaStatus_RunFailed, "invocation_files_create_failed");
	}
	constexpr char eventHeader[] = "timestamp_utc,sequence,component,status,detail\n";
	DWORD headerWritten = 0;
	if (!WriteFile(eventsHandle, eventHeader, static_cast<DWORD>(sizeof(eventHeader) - 1), &headerWritten, nullptr) ||
	    headerWritten != sizeof(eventHeader) - 1)
	{
		CloseHandle(logHandle);
		CloseHandle(eventsHandle);
		return InvocationError(error, ArenaStatus_RunFailed, "events_header_write_failed");
	}
	std::array<wchar_t, widePathCapacity> relativeLog = {};
	std::uint32_t relativeLogSize = 0;
	if (AppendWide(&relativeLog, &relativeLogSize, L"logs/") != ArenaStatus_Ok ||
	    AppendWide(&relativeLog, &relativeLogSize, selectedId.data()) != ArenaStatus_Ok ||
	    AppendWide(&relativeLog, &relativeLogSize, L"/bench.log") != ArenaStatus_Ok ||
	    WideToUtf8(directory.data(), &context->directoryPath.data, &context->directoryPath.size) != ArenaStatus_Ok ||
	    WideToUtf8(logPath.data(), &context->logPath.data, &context->logPath.size) != ArenaStatus_Ok ||
	    WideToUtf8(eventsPath.data(), &context->eventsPath.data, &context->eventsPath.size) != ArenaStatus_Ok ||
	    WideToUtf8(relativeLog.data(), &context->relativeLogPath.data, &context->relativeLogPath.size) !=
	        ArenaStatus_Ok)
	{
		CloseHandle(logHandle);
		CloseHandle(eventsHandle);
		return InvocationError(error, ArenaStatus_InvalidResult, "invocation_path_capacity");
	}
	context->logHandle = logHandle;
	context->eventsHandle = eventsHandle;
	context->status = AvailabilityStatus_Available;
	InvocationEvent event = {};
	const ArenaStatus startStatus = AppendInvocationEvent(context, "bench_start", "ok", command, &event, error);
	if (startStatus != ArenaStatus_Ok)
		DestroyInvocation(context);
	return startStatus;
}

ArenaStatus AppendInvocationEvent(InvocationContext* context, const char* component, const char* status,
                                  const char* detail, InvocationEvent* event, StatusRecord* error)
{
	*event = {};
	*error = {};
	if (context->status != AvailabilityStatus_Available || component == nullptr || status == nullptr ||
	    detail == nullptr)
		return InvocationError(error, ArenaStatus_InvalidArgument, "invalid_append_arguments");
	UtcTimestamp(&event->timestampUtc);
	if (InvocationCopy(&event->component.data, &event->component.size, component) != ArenaStatus_Ok ||
	    InvocationCopy(&event->status.data, &event->status.size, status) != ArenaStatus_Ok ||
	    InvocationCopy(&event->detail.data, &event->detail.size, detail) != ArenaStatus_Ok)
		return InvocationError(error, ArenaStatus_InvalidResult, "invocation_event_capacity");
	event->sequence = ++context->sequence;
	return WriteInvocationRow(*context, *event, error);
}

ArenaStatus FinishInvocation(InvocationContext* context, std::int32_t exitCode, InvocationEvent* event,
                             StatusRecord* error)
{
	const char* status = exitCode == 0 ? "ok" : ((exitCode == 130 || exitCode == 143) ? "interrupted" : "run_failed");
	std::array<char, kFixedTextCapacity> detail = {};
	const int written =
	    std::snprintf(detail.data(), detail.size(), "exit_code=%d log=%.*s", exitCode,
		              static_cast<int>(context->relativeLogPath.size), context->relativeLogPath.data.data());
	if (written <= 0 || written >= static_cast<int>(detail.size()))
		return InvocationError(error, ArenaStatus_InvalidResult, "invocation_exit_detail_capacity");
	const ArenaStatus appendStatus = AppendInvocationEvent(context, "bench_exit", status, detail.data(), event, error);
	if (appendStatus == ArenaStatus_Ok)
	{
		context->status = AvailabilityStatus_Unavailable;
		DestroyInvocation(context);
	}
	return appendStatus;
}

void DestroyInvocation(InvocationContext* context)
{
	if (context == nullptr)
		return;
	if (context->logHandle != nullptr)
		CloseHandle(static_cast<HANDLE>(context->logHandle));
	if (context->eventsHandle != nullptr)
		CloseHandle(static_cast<HANDLE>(context->eventsHandle));
	context->logHandle = nullptr;
	context->eventsHandle = nullptr;
}
}
