#include "physics_arena/host_windows.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wbemidl.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string_view>

namespace physics_arena
{
std::string_view HostTextView(const HostRecord* host, HostText text)
{
	if (host == nullptr || text.offset > host->textArenaUsed || text.size > host->textArenaUsed - text.offset)
		return {};
	return std::string_view(host->textArena.data() + text.offset, text.size);
}

namespace
{
struct WmiConnection
{
	IWbemLocator* locator;
	IWbemServices* services;
	PresenceStatus uninitializeCom;
};

template <std::size_t Capacity>
ArenaStatus HostCopy(std::array<char, Capacity>* destination, std::uint32_t* size, std::string_view source)
{
	if (source.empty() || source.size() >= Capacity)
		return ArenaStatus_InvalidResult;
	std::fill(destination->begin(), destination->end(), '\0');
	std::copy(source.begin(), source.end(), destination->begin());
	*size = static_cast<std::uint32_t>(source.size());
	return ArenaStatus_Ok;
}

ArenaStatus HostError(StatusRecord* error, ArenaStatus code, std::string_view detail)
{
	*error = {};
	HostCopy(&error->component, &error->componentSize, "host_windows");
	HostCopy(&error->status, &error->statusSize, ArenaStatusText(code));
	if (HostCopy(&error->detail, &error->detailSize, detail) != ArenaStatus_Ok)
		HostCopy(&error->detail, &error->detailSize, "host_detail_exceeded_capacity");
	error->code = code;
	return code;
}

ArenaStatus HostErrorNumber(StatusRecord* error, ArenaStatus code, const char* component, long value)
{
	char detail[96] = {};
	const int written = std::snprintf(detail, std::size(detail), "%s=%ld", component, value);
	return written > 0 && written < static_cast<int>(std::size(detail))
	           ? HostError(error, code, detail)
			   : HostError(error, code, "host_numeric_detail_capacity");
}

ArenaStatus StoreHostText(HostRecord* host, HostText* output, std::string_view source)
{
	if (source.empty() || source.size() > UINT32_MAX || host->textArenaUsed + source.size() > host->textArena.size())
		return ArenaStatus_InvalidResult;
	output->offset = host->textArenaUsed;
	output->size = static_cast<std::uint32_t>(source.size());
	std::copy(source.begin(), source.end(), host->textArena.begin() + host->textArenaUsed);
	host->textArenaUsed += output->size;
	return ArenaStatus_Ok;
}

void CloseWmi(WmiConnection* connection)
{
	if (connection->services != nullptr)
		connection->services->Release();
	if (connection->locator != nullptr)
		connection->locator->Release();
	if (connection->uninitializeCom == PresenceStatus_Present)
		CoUninitialize();
	*connection = {};
}

ArenaStatus ConnectWmi(WmiConnection* connection, StatusRecord* error)
{
	*connection = {};
	const HRESULT initialize = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(initialize) && initialize != RPC_E_CHANGED_MODE)
		return HostErrorNumber(error, ArenaStatus_ToolMissing, "com_initialize", static_cast<long>(initialize));
	connection->uninitializeCom = SUCCEEDED(initialize) ? PresenceStatus_Present : PresenceStatus_Absent;
	const HRESULT security = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
	                                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
	if (FAILED(security) && security != RPC_E_TOO_LATE)
	{
		CloseWmi(connection);
		return HostErrorNumber(error, ArenaStatus_ToolMissing, "com_security", static_cast<long>(security));
	}
	HRESULT result = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
	                                  reinterpret_cast<void**>(&connection->locator));
	if (FAILED(result))
	{
		CloseWmi(connection);
		return HostErrorNumber(error, ArenaStatus_ToolMissing, "wmi_locator", static_cast<long>(result));
	}
	BSTR nameSpace = SysAllocString(L"ROOT\\CIMV2");
	result = connection->locator->ConnectServer(nameSpace, nullptr, nullptr, nullptr, 0, nullptr, nullptr,
	                                            &connection->services);
	SysFreeString(nameSpace);
	if (FAILED(result))
	{
		CloseWmi(connection);
		return HostErrorNumber(error, ArenaStatus_ToolMissing, "wmi_connect", static_cast<long>(result));
	}
	result = CoSetProxyBlanket(connection->services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
	                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
	if (FAILED(result))
	{
		CloseWmi(connection);
		return HostErrorNumber(error, ArenaStatus_ToolMissing, "wmi_proxy", static_cast<long>(result));
	}
	return ArenaStatus_Ok;
}

ArenaStatus QueryWmi(IWbemServices* services, const wchar_t* query, IEnumWbemClassObject** enumerator,
                     StatusRecord* error)
{
	BSTR language = SysAllocString(L"WQL");
	BSTR queryText = SysAllocString(query);
	const HRESULT result = services->ExecQuery(
	    language, queryText, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, enumerator);
	SysFreeString(queryText);
	SysFreeString(language);
	return FAILED(result) ? HostErrorNumber(error, ArenaStatus_ToolMissing, "wmi_query", static_cast<long>(result))
	                      : ArenaStatus_Ok;
}

ArenaStatus NextWmi(IEnumWbemClassObject* enumerator, IWbemClassObject** object, PresenceStatus* presence,
                    StatusRecord* error)
{
	ULONG returned = 0;
	const HRESULT result = enumerator->Next(WBEM_INFINITE, 1, object, &returned);
	if (FAILED(result))
		return HostErrorNumber(error, ArenaStatus_ToolMissing, "wmi_next", static_cast<long>(result));
	*presence = returned == 0 ? PresenceStatus_Absent : PresenceStatus_Present;
	return ArenaStatus_Ok;
}

ArenaStatus WideToHost(const wchar_t* value, HostRecord* host, HostText* output, StatusRecord* error)
{
	if (value == nullptr || value[0] == L'\0')
		return HostError(error, ArenaStatus_InvalidResult, "empty_wmi_string");
	std::wstring_view text(value);
	while (!text.empty() && iswspace(text.front()) != 0)
		text.remove_prefix(1);
	while (!text.empty() && iswspace(text.back()) != 0)
		text.remove_suffix(1);
	const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
	                                         nullptr, 0, nullptr, nullptr);
	std::array<char, 1024> converted = {};
	if (required <= 0 || required >= static_cast<int>(converted.size()))
		return HostError(error, ArenaStatus_InvalidResult, "wmi_string_capacity");
	const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
	                                        converted.data(), static_cast<int>(converted.size()), nullptr, nullptr);
	if (written != required)
		return HostError(error, ArenaStatus_InvalidResult, "wmi_string_conversion");
	return StoreHostText(host, output, std::string_view(converted.data(), written));
}

ArenaStatus WmiString(IWbemClassObject* object, const wchar_t* property, HostRecord* host, HostText* output,
                      StatusRecord* error)
{
	VARIANT value;
	VariantInit(&value);
	const HRESULT result = object->Get(property, 0, &value, nullptr, nullptr);
	if (FAILED(result) || value.vt != VT_BSTR)
	{
		VariantClear(&value);
		return HostError(error, ArenaStatus_InvalidResult, "wmi_string_property");
	}
	const ArenaStatus status = WideToHost(value.bstrVal, host, output, error);
	VariantClear(&value);
	return status;
}

ArenaStatus WmiUnsigned64(IWbemClassObject* object, const wchar_t* property, std::uint64_t* output, StatusRecord* error)
{
	VARIANT value;
	VariantInit(&value);
	const HRESULT result = object->Get(property, 0, &value, nullptr, nullptr);
	if (FAILED(result))
	{
		VariantClear(&value);
		return HostError(error, ArenaStatus_InvalidResult, "wmi_unsigned_property");
	}
	switch (value.vt)
	{
	case VT_UI1:
		*output = value.bVal;
		break;
	case VT_UI2:
		*output = value.uiVal;
		break;
	case VT_UI4:
		*output = value.ulVal;
		break;
	case VT_UI8:
		*output = value.ullVal;
		break;
	case VT_I2:
		*output = value.iVal >= 0 ? static_cast<std::uint64_t>(value.iVal) : 0;
		break;
	case VT_I4:
		*output = value.lVal >= 0 ? static_cast<std::uint64_t>(value.lVal) : 0;
		break;
	case VT_I8:
		*output = value.llVal >= 0 ? static_cast<std::uint64_t>(value.llVal) : 0;
		break;
	case VT_BSTR:
	{
		wchar_t* end = nullptr;
		*output = std::wcstoull(value.bstrVal, &end, 10);
		if (end == value.bstrVal || *end != L'\0')
			*output = 0;
		break;
	}
	default:
		*output = 0;
		break;
	}
	VariantClear(&value);
	return *output == 0 ? HostError(error, ArenaStatus_InvalidResult, "wmi_unsigned_value") : ArenaStatus_Ok;
}

ArenaStatus ProcessorRecord(IWbemServices* services, HostRecord* host, StatusRecord* error)
{
	IEnumWbemClassObject* enumerator = nullptr;
	if (QueryWmi(
	        services,
	        L"SELECT Name,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed,CurrentClockSpeed FROM Win32_Processor",
	        &enumerator, error) != ArenaStatus_Ok)
		return ArenaStatus_ToolMissing;
	IWbemClassObject* object = nullptr;
	PresenceStatus presence = PresenceStatus_Absent;
	ArenaStatus status = NextWmi(enumerator, &object, &presence, error);
	if (status == ArenaStatus_Ok && presence == PresenceStatus_Present)
	{
		std::uint64_t value = 0;
		status = WmiString(object, L"Name", host, &host->cpuModel, error);
		if (status == ArenaStatus_Ok)
			status = WmiUnsigned64(object, L"NumberOfCores", &value, error);
		host->physicalCoreCount = static_cast<std::uint32_t>(value);
		if (status == ArenaStatus_Ok)
			status = WmiUnsigned64(object, L"NumberOfLogicalProcessors", &value, error);
		host->logicalThreadCount = static_cast<std::uint32_t>(value);
		if (status == ArenaStatus_Ok)
			status = WmiUnsigned64(object, L"MaxClockSpeed", &value, error);
		host->maxClockMhz = static_cast<std::uint32_t>(value);
		if (status == ArenaStatus_Ok)
			status = WmiUnsigned64(object, L"CurrentClockSpeed", &value, error);
		host->currentClockMhz = static_cast<std::uint32_t>(value);
		object->Release();
	}
	else if (status == ArenaStatus_Ok)
		status = HostError(error, ArenaStatus_InvalidResult, "processor_missing");
	enumerator->Release();
	return status;
}

ArenaStatus NormalizeCpuModel(HostRecord* host)
{
	const std::string_view model = HostTextView(host, host->cpuModel);
	std::array<char, 1024> compact = {};
	std::uint32_t size = 0;
	for (std::size_t index = 0; index < model.size();)
	{
		std::size_t skipped = 0;
		for (const std::string_view token : {std::string_view("13th Gen "), std::string_view("(R)"),
		                                     std::string_view("(TM)"), std::string_view(" CPU")})
			if (model.substr(index).starts_with(token))
			{
				skipped = token.size();
				break;
			}
		if (skipped != 0)
		{
			index += skipped;
			continue;
		}
		const char character = model[index++];
		if (character == ' ' && size != 0 && compact[size - 1] == ' ')
			continue;
		if (size >= compact.size())
			return ArenaStatus_InvalidResult;
		compact[size++] = character;
	}
	while (size != 0 && compact[size - 1] == ' ')
		--size;
	return StoreHostText(host, &host->cpuModel, std::string_view(compact.data(), size));
}

const char* MemoryTypeName(std::uint32_t type)
{
	switch (type)
	{
	case 20:
		return "DDR";
	case 21:
		return "DDR2";
	case 24:
		return "DDR3";
	case 26:
		return "DDR4";
	case 34:
		return "DDR5";
	default:
		return "Unknown";
	}
}

ArenaStatus MemoryRecords(IWbemServices* services, HostRecord* host, StatusRecord* error)
{
	IEnumWbemClassObject* enumerator = nullptr;
	if (QueryWmi(
	        services,
	        L"SELECT Capacity,ConfiguredClockSpeed,Speed,SMBIOSMemoryType,Manufacturer,PartNumber,DeviceLocator FROM Win32_PhysicalMemory",
	        &enumerator, error) != ArenaStatus_Ok)
		return ArenaStatus_ToolMissing;
	ArenaStatus status = ArenaStatus_Ok;
	for (;;)
	{
		IWbemClassObject* object = nullptr;
		PresenceStatus presence = PresenceStatus_Absent;
		status = NextWmi(enumerator, &object, &presence, error);
		if (status != ArenaStatus_Ok || presence == PresenceStatus_Absent)
			break;
		if (host->memoryModuleCount >= kMemoryModuleCapacity)
		{
			object->Release();
			status = HostError(error, ArenaStatus_InvalidResult, "memory_module_capacity");
			break;
		}
		MemoryModuleRecord& module = host->memoryModules[host->memoryModuleCount];
		std::uint64_t value = 0;
		status = WmiUnsigned64(object, L"Capacity", &value, error);
		module.capacityGb = static_cast<std::uint32_t>((value + (1ULL << 29)) >> 30);
		if (status == ArenaStatus_Ok)
			status = WmiUnsigned64(object, L"ConfiguredClockSpeed", &value, error);
		module.configuredClockMhz = static_cast<std::uint32_t>(value);
		if (status == ArenaStatus_Ok)
			status = WmiUnsigned64(object, L"Speed", &value, error);
		module.speedMhz = static_cast<std::uint32_t>(value);
		if (status == ArenaStatus_Ok)
			status = WmiUnsigned64(object, L"SMBIOSMemoryType", &value, error);
		if (status == ArenaStatus_Ok)
			status = StoreHostText(host, &module.type, MemoryTypeName(static_cast<std::uint32_t>(value)));
		if (status == ArenaStatus_Ok)
			status = WmiString(object, L"Manufacturer", host, &module.manufacturer, error);
		if (status == ArenaStatus_Ok)
			status = WmiString(object, L"PartNumber", host, &module.partNumber, error);
		if (status == ArenaStatus_Ok)
			status = WmiString(object, L"DeviceLocator", host, &module.slot, error);
		object->Release();
		if (status != ArenaStatus_Ok)
			break;
		host->totalMemoryGb += module.capacityGb;
		if (host->configuredMemoryClockMhz == 0)
			host->configuredMemoryClockMhz = module.configuredClockMhz;
		if (host->memoryType.size == 0)
			host->memoryType = module.type;
		host->memoryModuleCount += 1;
	}
	enumerator->Release();
	return status;
}

ArenaStatus SingleStringRecord(IWbemServices* services, const wchar_t* query, const wchar_t* const* properties,
                               HostRecord* host, HostText* const* outputs, std::size_t count, StatusRecord* error)
{
	IEnumWbemClassObject* enumerator = nullptr;
	if (QueryWmi(services, query, &enumerator, error) != ArenaStatus_Ok)
		return ArenaStatus_ToolMissing;
	IWbemClassObject* object = nullptr;
	PresenceStatus presence = PresenceStatus_Absent;
	ArenaStatus status = NextWmi(enumerator, &object, &presence, error);
	if (status == ArenaStatus_Ok && presence == PresenceStatus_Present)
	{
		for (std::size_t index = 0; index < count && status == ArenaStatus_Ok; ++index)
			status = WmiString(object, properties[index], host, outputs[index], error);
		object->Release();
	}
	else if (status == ArenaStatus_Ok)
		status = HostError(error, ArenaStatus_InvalidResult, "wmi_record_missing");
	enumerator->Release();
	return status;
}

ArenaStatus ComputerSystemMemory(IWbemServices* services, HostRecord* host, StatusRecord* error)
{
	IEnumWbemClassObject* enumerator = nullptr;
	if (QueryWmi(services, L"SELECT TotalPhysicalMemory FROM Win32_ComputerSystem", &enumerator, error) !=
	    ArenaStatus_Ok)
		return ArenaStatus_ToolMissing;
	IWbemClassObject* object = nullptr;
	PresenceStatus presence = PresenceStatus_Absent;
	ArenaStatus status = NextWmi(enumerator, &object, &presence, error);
	if (status == ArenaStatus_Ok && presence == PresenceStatus_Present)
	{
		std::uint64_t bytes = 0;
		status = WmiUnsigned64(object, L"TotalPhysicalMemory", &bytes, error);
		if (status == ArenaStatus_Ok)
			host->totalMemoryGb = static_cast<std::uint32_t>((bytes + (1ULL << 29)) >> 30);
		object->Release();
	}
	else if (status == ArenaStatus_Ok)
		status = HostError(error, ArenaStatus_InvalidResult, "computer_system_missing");
	enumerator->Release();
	return status;
}

ArenaStatus MotherboardRecord(IWbemServices* services, HostRecord* host, StatusRecord* error)
{
	HostText manufacturer = {};
	HostText product = {};
	const wchar_t* properties[] = {L"Manufacturer", L"Product"};
	HostText* outputs[] = {&manufacturer, &product};
	ArenaStatus status = SingleStringRecord(services, L"SELECT Manufacturer,Product FROM Win32_BaseBoard", properties,
	                                        host, outputs, std::size(properties), error);
	if (status != ArenaStatus_Ok)
		return status;
	const std::string_view manufacturerText = HostTextView(host, manufacturer);
	const std::string_view productText = HostTextView(host, product);
	int productStartsWithManufacturer = manufacturerText.size() <= productText.size() ? 1 : 0;
	for (std::size_t index = 0; index < manufacturerText.size() && productStartsWithManufacturer != 0; ++index)
		if (std::tolower(static_cast<unsigned char>(manufacturerText[index])) !=
		    std::tolower(static_cast<unsigned char>(productText[index])))
			productStartsWithManufacturer = 0;
	if (productStartsWithManufacturer != 0)
		return StoreHostText(host, &host->motherboardModel, productText);
	std::array<char, 1024> combined = {};
	if (manufacturerText.size() + productText.size() + 1 > combined.size())
		return ArenaStatus_InvalidResult;
	std::copy(manufacturerText.begin(), manufacturerText.end(), combined.begin());
	combined[manufacturerText.size()] = ' ';
	std::copy(productText.begin(), productText.end(), combined.begin() + manufacturerText.size() + 1);
	return StoreHostText(host, &host->motherboardModel,
	                     std::string_view(combined.data(), manufacturerText.size() + productText.size() + 1));
}

ArenaStatus NormalizeBiosDate(HostRecord* host)
{
	const std::string_view value = HostTextView(host, host->biosDate);
	if (value.size() < 8 || !std::all_of(value.begin(), value.begin() + 8,
	                                     [](char character)
	                                     {
		                                     return character >= '0' && character <= '9';
	                                     }))
		return ArenaStatus_Ok;
	char date[10] = {value[0], value[1], value[2], value[3], '-', value[4], value[5], '-', value[6], value[7]};
	return StoreHostText(host, &host->biosDate, std::string_view(date, std::size(date)));
}

ArenaStatus TopologyRecord(HostRecord* host, StatusRecord* error)
{
	DWORD bytes = 0;
	GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
	if (bytes == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		return HostError(error, ArenaStatus_ToolMissing, "processor_topology_size");
	std::array<std::byte, 65536> storage = {};
	if (bytes > storage.size())
		return HostError(error, ArenaStatus_InvalidResult, "processor_topology_capacity");
	PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX information =
	    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data());
	if (!GetLogicalProcessorInformationEx(RelationProcessorCore, information, &bytes))
		return HostError(error, ArenaStatus_ToolMissing, "processor_topology_query");
	std::array<std::uint32_t, 256> classCounts = {};
	std::uint32_t physical = 0;
	std::uint32_t logical = 0;
	std::byte* cursor = storage.data();
	const std::byte* end = storage.data() + bytes;
	while (cursor < end)
	{
		PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX item =
		    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(cursor);
		if (item->Relationship != RelationProcessorCore || item->Size == 0 || cursor + item->Size > end)
			return HostError(error, ArenaStatus_InvalidResult, "processor_topology_record");
		physical += 1;
		classCounts[item->Processor.EfficiencyClass] += 1;
		for (WORD group = 0; group < item->Processor.GroupCount; ++group)
			logical += std::popcount(static_cast<std::uint64_t>(item->Processor.GroupMask[group].Mask));
		cursor += item->Size;
	}
	if (physical == 0 || logical == 0)
		return HostError(error, ArenaStatus_InvalidResult, "processor_topology_empty");
	host->physicalCoreCount = physical;
	host->logicalThreadCount = logical;
	std::uint32_t firstClass = UINT32_MAX;
	std::uint32_t lastClass = 0;
	std::uint32_t classKinds = 0;
	for (std::uint32_t index = 0; index < classCounts.size(); ++index)
		if (classCounts[index] != 0)
		{
			firstClass = std::min(firstClass, index);
			lastClass = std::max(lastClass, index);
			classKinds += 1;
		}
	if (classKinds > 1)
	{
		host->efficiencyCoreCount = classCounts[firstClass];
		host->performanceCoreCount = classCounts[lastClass];
		char topology[64] = {};
		std::snprintf(topology, std::size(topology), "%uP+%uE / %uT", host->performanceCoreCount,
		              host->efficiencyCoreCount, host->logicalThreadCount);
		StoreHostText(host, &host->cpuTopology, topology);
	}
	else
	{
		char topology[64] = {};
		std::snprintf(topology, std::size(topology), "%uC / %uT", host->physicalCoreCount, host->logicalThreadCount);
		StoreHostText(host, &host->cpuTopology, topology);
	}
	host->cpuCountSource = HostMetadataSource_ProcessorTopology;
	return ArenaStatus_Ok;
}
} // namespace

ArenaStatus ValidateHostRecord(const HostRecord* host, StatusRecord* error)
{
	*error = {};
	if (host == nullptr)
		return HostError(error, ArenaStatus_InvalidArgument, "host_null");
	if (host->cpuModel.size == 0)
		return HostError(error, ArenaStatus_InvalidResult, "missing_host_field=cpu.model");
	if (host->physicalCoreCount == 0 || host->logicalThreadCount == 0)
		return HostError(error, ArenaStatus_InvalidResult, "invalid_host_field=cpu.logical_threads");
	if (host->totalMemoryGb == 0 || host->configuredMemoryClockMhz == 0 || host->memoryModuleCount == 0)
		return HostError(error, ArenaStatus_InvalidResult, "invalid_host_field=memory");
	if (host->osName.size == 0 || host->osVersion.size == 0)
		return HostError(error, ArenaStatus_InvalidResult, "missing_host_field=os");
	if (host->cpuCountSource != HostMetadataSource_ProcessorTopology ||
	    host->cpuModelSource != HostMetadataSource_WindowsCim || host->memorySource != HostMetadataSource_WindowsCim ||
	    host->operatingSystemSource != HostMetadataSource_WindowsCim)
		return HostError(error, ArenaStatus_InvalidResult, "invalid_host_metadata_source");
	return ArenaStatus_Ok;
}

ArenaStatus CollectWindowsHost(HostRecord* host, StatusRecord* error)
{
	*host = {};
	*error = {};
	WmiConnection connection = {};
	ArenaStatus status = ConnectWmi(&connection, error);
	if (status != ArenaStatus_Ok)
		return status;
	status = ProcessorRecord(connection.services, host, error);
	if (status == ArenaStatus_Ok)
		status = NormalizeCpuModel(host);
	if (status == ArenaStatus_Ok)
		status = MemoryRecords(connection.services, host, error);
	if (status == ArenaStatus_Ok)
		status = ComputerSystemMemory(connection.services, host, error);
	const wchar_t* osProperties[] = {L"Caption", L"Version", L"BuildNumber", L"OSArchitecture"};
	HostText* osOutputs[] = {&host->osName, &host->osVersion, &host->osBuild, &host->osArchitecture};
	if (status == ArenaStatus_Ok)
		status = SingleStringRecord(connection.services,
		                            L"SELECT Caption,Version,BuildNumber,OSArchitecture FROM Win32_OperatingSystem",
		                            osProperties, host, osOutputs, std::size(osProperties), error);
	if (status == ArenaStatus_Ok)
		status = MotherboardRecord(connection.services, host, error);
	const wchar_t* biosProperties[] = {L"SMBIOSBIOSVersion", L"ReleaseDate"};
	HostText* biosOutputs[] = {&host->biosVersion, &host->biosDate};
	if (status == ArenaStatus_Ok)
		status = SingleStringRecord(connection.services, L"SELECT SMBIOSBIOSVersion,ReleaseDate FROM Win32_BIOS",
		                            biosProperties, host, biosOutputs, std::size(biosProperties), error);
	if (status == ArenaStatus_Ok)
		status = NormalizeBiosDate(host);
	CloseWmi(&connection);
	if (status != ArenaStatus_Ok)
	{
		*host = {};
		return status;
	}
	host->cpuModelSource = HostMetadataSource_WindowsCim;
	host->memorySource = HostMetadataSource_WindowsCim;
	host->operatingSystemSource = HostMetadataSource_WindowsCim;
	host->motherboardSource = HostMetadataSource_WindowsCim;
	status = TopologyRecord(host, error);
	if (status != ArenaStatus_Ok || ValidateHostRecord(host, error) != ArenaStatus_Ok)
	{
		*host = {};
		return status == ArenaStatus_Ok ? ArenaStatus_InvalidResult : status;
	}
	return ArenaStatus_Ok;
}
} // namespace physics_arena
