$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$processorTopologyStatus = 'unavailable'
$processorTopology = @()
try {
Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class ProcessorTopologyProbe
{
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetLogicalProcessorInformationEx(int relationshipType, IntPtr buffer, ref int returnedLength);

    public static List<Dictionary<string, object>> Query()
    {
        int length = 0;
        GetLogicalProcessorInformationEx(0, IntPtr.Zero, ref length);
        List<Dictionary<string, object>> records = new List<Dictionary<string, object>>();
        if (length <= 0)
        {
            return records;
        }

        IntPtr buffer = Marshal.AllocHGlobal(length);
        try
        {
            if (!GetLogicalProcessorInformationEx(0, buffer, ref length))
            {
                return records;
            }

            byte[] bytes = new byte[length];
            Marshal.Copy(buffer, bytes, 0, length);
            int offset = 0;
            int affinitySize = IntPtr.Size == 8 ? 16 : 12;
            while (offset + 32 <= length)
            {
                int relationship = BitConverter.ToInt32(bytes, offset);
                int size = BitConverter.ToInt32(bytes, offset + 4);
                if (size <= 0 || offset + size > length)
                {
                    break;
                }

                if (relationship == 0)
                {
                    int flags = bytes[offset + 8];
                    int efficiencyClass = bytes[offset + 9];
                    int groupCount = BitConverter.ToUInt16(bytes, offset + 30);
                    int logicalProcessors = 0;
                    int groupOffset = offset + 32;
                    for (int groupIndex = 0; groupIndex < groupCount && groupOffset + affinitySize <= offset + size; groupIndex++)
                    {
                        ulong mask = IntPtr.Size == 8 ? BitConverter.ToUInt64(bytes, groupOffset) : BitConverter.ToUInt32(bytes, groupOffset);
                        logicalProcessors += PopCount(mask);
                        groupOffset += affinitySize;
                    }

                    Dictionary<string, object> record = new Dictionary<string, object>();
                    record["efficiency_class"] = efficiencyClass;
                    record["logical_processors"] = logicalProcessors;
                    record["smt_flag"] = flags & 1;
                    record["group_count"] = groupCount;
                    records.Add(record);
                }

                offset += size;
            }
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }
        return records;
    }

    public static int PopCount(ulong value)
    {
        int count = 0;
        while (value != 0)
        {
            value &= value - 1;
            count++;
        }
        return count;
    }
}
"@
  $processorTopology = @([ProcessorTopologyProbe]::Query())
  if ($processorTopology.Count -gt 0) {
    $processorTopologyStatus = 'ok'
  }
} catch {
  $processorTopologyStatus = 'unavailable'
}
$processor = Get-CimInstance Win32_Processor | Select-Object -First 1 Name, NumberOfCores, NumberOfLogicalProcessors, MaxClockSpeed, CurrentClockSpeed
$memory = Get-CimInstance Win32_PhysicalMemory | Select-Object Capacity, Speed, ConfiguredClockSpeed, Manufacturer, PartNumber, DeviceLocator, SMBIOSMemoryType
$os = Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, BuildNumber, OSArchitecture
$computerSystem = Get-CimInstance Win32_ComputerSystem | Select-Object TotalPhysicalMemory
$baseboard = Get-CimInstance Win32_BaseBoard | Select-Object Manufacturer, Product
$bios = Get-CimInstance Win32_BIOS | Select-Object SMBIOSBIOSVersion, ReleaseDate
[ordered]@{
  processor = $processor
  processor_topology_status = $processorTopologyStatus
  processor_topology = @($processorTopology)
  memory = @($memory)
  operating_system = $os
  computer_system = $computerSystem
  baseboard = $baseboard
  bios = $bios
} | ConvertTo-Json -Depth 6 -Compress
