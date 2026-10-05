#include "physics_arena/process_windows.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string_view>

namespace physics_arena
{
namespace
{
template <std::size_t Capacity>
void ProcessCopy(std::array<char, Capacity>* destination, std::uint32_t* size, std::string_view source)
{
	std::fill(destination->begin(), destination->end(), '\0');
	const std::size_t count = std::min(source.size(), Capacity - 1);
	std::copy(source.begin(), source.begin() + count, destination->begin());
	*size = static_cast<std::uint32_t>(count);
}

ArenaStatus ProcessError(StatusRecord* error, ArenaStatus code, std::string_view detail)
{
	*error = {};
	ProcessCopy(&error->component, &error->componentSize, "process_windows");
	ProcessCopy(&error->status, &error->statusSize, ArenaStatusText(code));
	ProcessCopy(&error->detail, &error->detailSize, detail);
	error->code = code;
	return code;
}

ArenaStatus ProcessSystemError(StatusRecord* error, ArenaStatus code, const char* operation, DWORD systemError)
{
	std::array<char, 128> detail = {};
	const int written =
	    std::snprintf(detail.data(), detail.size(), "%s=%lu", operation, static_cast<unsigned long>(systemError));
	return written > 0 && written < static_cast<int>(detail.size())
	           ? ProcessError(error, code, std::string_view(detail.data(), static_cast<std::size_t>(written)))
			   : ProcessError(error, code, "process_system_error_capacity");
}

void CloseProcessHandle(HANDLE handle)
{
	if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
		CloseHandle(handle);
}

ArenaStatus ReadProcessPipe(HANDLE handle, std::array<char, kProcessOutputCapacity>* output, std::uint32_t* outputSize,
                            DWORD* available, StatusRecord* error, const char* peekFailure, const char* readFailure)
{
	*available = 0;
	if (!PeekNamedPipe(handle, nullptr, 0, nullptr, available, nullptr))
	{
		const DWORD pipeError = GetLastError();
		if (pipeError == ERROR_BROKEN_PIPE)
			return ArenaStatus_Ok;
		return ProcessSystemError(error, ArenaStatus_RunFailed, peekFailure, pipeError);
	}
	if (*available == 0)
		return ArenaStatus_Ok;
	DWORD read = 0;
	const DWORD capacity = static_cast<DWORD>(std::min<std::size_t>(*available, output->size() - 1));
	if (!ReadFile(handle, output->data(), capacity, &read, nullptr))
		return ProcessSystemError(error, ArenaStatus_RunFailed, readFailure, GetLastError());
	*outputSize = read;
	return ArenaStatus_Ok;
}

ProcessCleanupStatus QueryJob(HANDLE job)
{
	JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting = {};
	if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr))
		return ProcessCleanupStatus_QueryFailed;
	return accounting.ActiveProcesses == 0 ? ProcessCleanupStatus_Empty : ProcessCleanupStatus_Active;
}

ProcessCleanupStatus WaitForJobEmpty(HANDLE job, std::uint32_t timeoutMilliseconds)
{
	const std::uint64_t deadline = GetTickCount64() + timeoutMilliseconds;
	ProcessCleanupStatus status = ProcessCleanupStatus_Unknown;
	do
	{
		status = QueryJob(job);
		if (status == ProcessCleanupStatus_Empty || status == ProcessCleanupStatus_QueryFailed)
			return status;
		Sleep(10);
	} while (GetTickCount64() < deadline);
	return ProcessCleanupStatus_TimedOut;
}

ArenaStatus TerminateOwnedJob(ProcessRecord* process, std::int32_t exitCode, StatusRecord* error)
{
	if (!TerminateJobObject(static_cast<HANDLE>(process->jobHandle), static_cast<UINT>(exitCode)))
		return ProcessSystemError(error, ArenaStatus_RunFailed, "terminate_job_failed", GetLastError());
	process->cleanupStatus = WaitForJobEmpty(static_cast<HANDLE>(process->jobHandle), 2000);
	process->exitCode = exitCode;
	process->state = process->cleanupStatus == ProcessCleanupStatus_Empty ? ProcessState_Exited : ProcessState_Failed;
	if (process->cleanupStatus == ProcessCleanupStatus_Empty)
		return ArenaStatus_Ok;
	std::array<char, 96> detail = {};
	const int written = std::snprintf(detail.data(), detail.size(), "job_cleanup_failed exit_code=%d", exitCode);
	return ProcessError(error, ArenaStatus_RunFailed,
	                    written > 0 && written < static_cast<int>(detail.size())
	                        ? std::string_view(detail.data(), static_cast<std::size_t>(written))
							: std::string_view("job_cleanup_failed"));
}
} // namespace

ArenaStatus StartProcess(const ProcessSpec* spec, ProcessRecord* process, StatusRecord* error)
{
	*process = {};
	*error = {};
	if (spec == nullptr || spec->executablePath[0] == L'\0' || spec->commandLine[0] == L'\0' ||
	    spec->workingDirectory[0] == L'\0')
		return ProcessError(error, ArenaStatus_InvalidArgument, "invalid_process_spec");
	SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
	HANDLE outputRead = nullptr;
	HANDLE outputWrite = nullptr;
	HANDLE errorRead = nullptr;
	HANDLE errorWrite = nullptr;
	if (!CreatePipe(&outputRead, &outputWrite, &security, 65536) ||
	    !SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0) ||
	    !CreatePipe(&errorRead, &errorWrite, &security, 65536) ||
	    !SetHandleInformation(errorRead, HANDLE_FLAG_INHERIT, 0))
	{
		CloseProcessHandle(outputRead);
		CloseProcessHandle(outputWrite);
		CloseProcessHandle(errorRead);
		CloseProcessHandle(errorWrite);
		return ProcessError(error, ArenaStatus_RunFailed, "output_pipe_create_failed");
	}
	HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
	                               FILE_ATTRIBUTE_NORMAL, nullptr);
	HANDLE job = CreateJobObjectW(nullptr, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (nullInput == INVALID_HANDLE_VALUE || job == nullptr ||
	    !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
	{
		CloseProcessHandle(outputRead);
		CloseProcessHandle(outputWrite);
		CloseProcessHandle(errorRead);
		CloseProcessHandle(errorWrite);
		CloseProcessHandle(nullInput);
		CloseProcessHandle(job);
		return ProcessError(error, ArenaStatus_RunFailed, "process_job_setup_failed");
	}
	SIZE_T attributeBytes = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeBytes);
	std::array<std::byte, 256> attributeStorage = {};
	if (attributeBytes > attributeStorage.size())
	{
		CloseProcessHandle(outputRead);
		CloseProcessHandle(outputWrite);
		CloseProcessHandle(errorRead);
		CloseProcessHandle(errorWrite);
		CloseProcessHandle(nullInput);
		CloseProcessHandle(job);
		return ProcessError(error, ArenaStatus_RunFailed, "process_attribute_capacity");
	}
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeStorage.data());
	HANDLE inheritedHandles[3] = {outputWrite, errorWrite, nullInput};
	if (!InitializeProcThreadAttributeList(attributes, 1, 0, &attributeBytes) ||
	    !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritedHandles,
	                               sizeof(inheritedHandles), nullptr, nullptr))
	{
		CloseProcessHandle(outputRead);
		CloseProcessHandle(outputWrite);
		CloseProcessHandle(errorRead);
		CloseProcessHandle(errorWrite);
		CloseProcessHandle(nullInput);
		CloseProcessHandle(job);
		return ProcessError(error, ArenaStatus_RunFailed, "process_handle_list_failed");
	}
	STARTUPINFOEXW startup = {};
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	startup.StartupInfo.wShowWindow = SW_HIDE;
	startup.StartupInfo.hStdInput = nullInput;
	startup.StartupInfo.hStdOutput = outputWrite;
	startup.StartupInfo.hStdError = errorWrite;
	startup.lpAttributeList = attributes;
	PROCESS_INFORMATION information = {};
	std::array<wchar_t, kProcessCommandCapacity> command = spec->commandLine;
	DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT;
	void* environment = nullptr;
	if (spec->environmentSize != 0)
	{
		if (spec->environmentSize > spec->environmentBlock.size() ||
		    spec->environmentBlock[spec->environmentSize - 1] != L'\0')
		{
			DeleteProcThreadAttributeList(attributes);
			CloseProcessHandle(outputRead);
			CloseProcessHandle(outputWrite);
			CloseProcessHandle(errorRead);
			CloseProcessHandle(errorWrite);
			CloseProcessHandle(nullInput);
			CloseProcessHandle(job);
			return ProcessError(error, ArenaStatus_InvalidArgument, "invalid_environment_block");
		}
		environment = const_cast<wchar_t*>(spec->environmentBlock.data());
		flags |= CREATE_UNICODE_ENVIRONMENT;
	}
	const BOOL created = CreateProcessW(spec->executablePath.data(), command.data(), nullptr, nullptr, TRUE, flags,
	                                    environment, spec->workingDirectory.data(), &startup.StartupInfo, &information);
	DeleteProcThreadAttributeList(attributes);
	CloseProcessHandle(outputWrite);
	CloseProcessHandle(errorWrite);
	CloseProcessHandle(nullInput);
	if (!created)
	{
		CloseProcessHandle(outputRead);
		CloseProcessHandle(errorRead);
		CloseProcessHandle(job);
		return ProcessSystemError(error, ArenaStatus_RunFailed, "create_process_failed", GetLastError());
	}
	if (!AssignProcessToJobObject(job, information.hProcess) ||
	    ResumeThread(information.hThread) == static_cast<DWORD>(-1))
	{
		TerminateProcess(information.hProcess, 2);
		WaitForSingleObject(information.hProcess, 2000);
		CloseProcessHandle(information.hThread);
		CloseProcessHandle(information.hProcess);
		CloseProcessHandle(outputRead);
		CloseProcessHandle(errorRead);
		CloseProcessHandle(job);
		return ProcessError(error, ArenaStatus_RunFailed, "assign_or_resume_failed");
	}
	CloseHandle(information.hThread);
	process->processHandle = information.hProcess;
	process->jobHandle = job;
	process->outputReadHandle = outputRead;
	process->errorReadHandle = errorRead;
	process->startedTick = GetTickCount64();
	process->timeoutMilliseconds = spec->timeoutMilliseconds;
	process->cancellationGraceMilliseconds = spec->cancellationGraceMilliseconds;
	process->processId = information.dwProcessId;
	process->state = ProcessState_Running;
	process->cleanupStatus = ProcessCleanupStatus_Active;
	process->cancellationRequested = PresenceStatus_Absent;
	return ArenaStatus_Ok;
}

ArenaStatus PollProcess(ProcessRecord* process, ProcessPollRecord* poll, StatusRecord* error)
{
	*poll = {};
	*error = {};
	if (process == nullptr || process->state == ProcessState_Idle)
		return ProcessError(error, ArenaStatus_InvalidArgument, "process_not_started");
	if (process->state != ProcessState_Running)
	{
		poll->state = process->state;
		poll->exitCode = process->exitCode;
		poll->cleanupStatus = process->cleanupStatus;
		return ArenaStatus_Ok;
	}
	DWORD outputAvailable = 0;
	DWORD errorAvailable = 0;
	if (ReadProcessPipe(static_cast<HANDLE>(process->outputReadHandle), &poll->output, &poll->outputSize,
	                    &outputAvailable, error, "peek_stdout_failed", "read_stdout_failed") != ArenaStatus_Ok ||
	    ReadProcessPipe(static_cast<HANDLE>(process->errorReadHandle), &poll->errorOutput, &poll->errorOutputSize,
	                    &errorAvailable, error, "peek_stderr_failed", "read_stderr_failed") != ArenaStatus_Ok)
		return ArenaStatus_RunFailed;
	const std::uint64_t now = GetTickCount64();
	if (process->rootExited == PresenceStatus_Absent &&
	    WaitForSingleObject(static_cast<HANDLE>(process->processHandle), 0) == WAIT_OBJECT_0)
	{
		DWORD exitCode = 0;
		if (GetExitCodeProcess(static_cast<HANDLE>(process->processHandle), &exitCode) == 0)
			return ProcessSystemError(error, ArenaStatus_RunFailed, "process_exit_code_failed", GetLastError());
		process->rootExitCode = static_cast<std::int32_t>(exitCode);
		process->rootExitTick = now;
		process->rootExited = PresenceStatus_Present;
		if (outputAvailable == 0 &&
		    ReadProcessPipe(static_cast<HANDLE>(process->outputReadHandle), &poll->output, &poll->outputSize,
		                    &outputAvailable, error, "peek_stdout_failed", "read_stdout_failed") != ArenaStatus_Ok)
			return ArenaStatus_RunFailed;
		if (errorAvailable == 0 &&
		    ReadProcessPipe(static_cast<HANDLE>(process->errorReadHandle), &poll->errorOutput, &poll->errorOutputSize,
		                    &errorAvailable, error, "peek_stderr_failed", "read_stderr_failed") != ArenaStatus_Ok)
			return ArenaStatus_RunFailed;
	}
	if (process->cancellationRequested == PresenceStatus_Present &&
	    now - process->cancellationTick >= process->cancellationGraceMilliseconds)
	{
		process->termination = ProcessTermination_Cancelled;
		if (TerminateOwnedJob(process, 130, error) != ArenaStatus_Ok)
			return ArenaStatus_RunFailed;
	}
	else if (process->rootExited == PresenceStatus_Absent && process->timeoutMilliseconds != 0 &&
	         now - process->startedTick >= process->timeoutMilliseconds)
	{
		process->termination = ProcessTermination_Timeout;
		if (TerminateOwnedJob(process, 124, error) != ArenaStatus_Ok)
			return ArenaStatus_RunFailed;
	}
	else
	{
		if (process->rootExited == PresenceStatus_Present && outputAvailable == 0 && errorAvailable == 0)
		{
			process->cleanupStatus = QueryJob(static_cast<HANDLE>(process->jobHandle));
			if (process->cleanupStatus == ProcessCleanupStatus_Empty)
			{
				process->exitCode = process->rootExitCode;
				process->state = ProcessState_Exited;
			}
			else if (process->cleanupStatus == ProcessCleanupStatus_QueryFailed)
			{
				std::array<char, 96> detail = {};
				const int written = std::snprintf(detail.data(), detail.size(), "job_query_failed root_exit_code=%d",
				                                  process->rootExitCode);
				return ProcessError(error, ArenaStatus_RunFailed,
				                    written > 0 && written < static_cast<int>(detail.size())
				                        ? std::string_view(detail.data(), static_cast<std::size_t>(written))
										: std::string_view("job_query_failed"));
			}
			else if (now - process->rootExitTick >= process->cancellationGraceMilliseconds &&
			         TerminateOwnedJob(process, process->rootExitCode, error) != ArenaStatus_Ok)
				return ArenaStatus_RunFailed;
		}
	}
	poll->state = process->state;
	poll->exitCode = process->exitCode;
	poll->cleanupStatus = process->cleanupStatus;
	return ArenaStatus_Ok;
}

ArenaStatus RequestProcessCancellation(ProcessRecord* process, StatusRecord* error)
{
	*error = {};
	if (process == nullptr || process->state != ProcessState_Running)
		return ProcessError(error, ArenaStatus_InvalidArgument, "process_not_running");
	if (process->cancellationRequested == PresenceStatus_Absent)
	{
		process->cancellationRequested = PresenceStatus_Present;
		process->cancellationTick = GetTickCount64();
	}
	return ArenaStatus_Ok;
}

std::string_view CleanupStatusText(ProcessCleanupStatus status)
{
	switch (status)
	{
	case ProcessCleanupStatus_Active:
		return "active";
	case ProcessCleanupStatus_QueryFailed:
		return "query_failed";
	case ProcessCleanupStatus_TimedOut:
		return "timed_out";
	case ProcessCleanupStatus_Empty:
		return "empty";
	default:
		return "unknown";
	}
}

ProcessCleanupStatus DestroyProcess(ProcessRecord* process)
{
	if (process == nullptr)
		return ProcessCleanupStatus_Unknown;
	if (process->state == ProcessState_Running && process->jobHandle != nullptr)
	{
		process->cleanupStatus = TerminateJobObject(static_cast<HANDLE>(process->jobHandle), 130) != 0
		                             ? WaitForJobEmpty(static_cast<HANDLE>(process->jobHandle), 2000)
		                             : ProcessCleanupStatus_QueryFailed;
	}
	else if (process->state == ProcessState_Running)
		process->cleanupStatus = ProcessCleanupStatus_QueryFailed;
	const ProcessCleanupStatus cleanupStatus = process->cleanupStatus;
	for (void* handle :
	     {process->processHandle, process->outputReadHandle, process->errorReadHandle, process->jobHandle})
		if (handle != nullptr)
			CloseHandle(static_cast<HANDLE>(handle));
	*process = {};
	return cleanupStatus;
}

ArenaStatus FinalizeProcessOwnership(ProcessRecord* process, ArenaStatus primaryStatus, StatusRecord* error)
{
	if (error == nullptr)
		return ArenaStatus_InvalidArgument;
	const ProcessCleanupStatus cleanupStatus = DestroyProcess(process);
	if (cleanupStatus == ProcessCleanupStatus_Empty || cleanupStatus == ProcessCleanupStatus_Unknown)
		return primaryStatus;
	const std::string_view cleanupText = CleanupStatusText(cleanupStatus);
	if (primaryStatus == ArenaStatus_Ok)
	{
		std::array<char, 96> detail = {};
		const int written = std::snprintf(detail.data(), detail.size(), "process_cleanup_failed cleanup_status=%.*s",
		                                  static_cast<int>(cleanupText.size()), cleanupText.data());
		return ProcessError(error, ArenaStatus_RunFailed,
		                    written > 0 ? std::string_view(detail.data(), static_cast<std::size_t>(written))
		                                : std::string_view("process_cleanup_failed"));
	}
	std::array<char, 48> suffix = {};
	const int written = std::snprintf(suffix.data(), suffix.size(), " cleanup_status=%.*s",
	                                  static_cast<int>(cleanupText.size()), cleanupText.data());
	if (written > 0)
	{
		const std::size_t suffixSize = static_cast<std::size_t>(written);
		const std::size_t retained = std::min<std::size_t>(error->detailSize, error->detail.size() - suffixSize - 1);
		std::copy(suffix.begin(), suffix.begin() + suffixSize, error->detail.begin() + retained);
		error->detailSize = static_cast<std::uint32_t>(retained + suffixSize);
		error->detail[error->detailSize] = '\0';
	}
	return primaryStatus;
}
} // namespace physics_arena
