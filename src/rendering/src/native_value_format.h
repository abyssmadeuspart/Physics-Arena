#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace benchmark_visual
{
enum NativeValueDomain
{
	NativeValueDomain_Quantity,
	NativeValueDomain_Rate,
	NativeValueDomain_Milliseconds,
};

struct NativeValueFormat
{
	NativeValueDomain domain;
	double divisor;
	const char* unitPrefix;
};

using NativeValueText = std::array<char, 512>;

NativeValueFormat SelectNativeValueFormat(NativeValueDomain domain, double maximumMagnitude);
NativeValueText FormatNativeValue(double value, NativeValueFormat format);
NativeValueText FormatNativeCount(std::uint64_t value);
NativeValueText FormatNativeRaw(double value);
NativeValueText FormatNativeRawCount(std::uint64_t value);
NativeValueText FormatNativeUnit(NativeValueFormat format, std::string_view declaredUnit);
NativeValueText FormatNativeThreadSet(const std::uint32_t* values, std::uint32_t count);
NativeValueText FormatNativeBytes(std::uint64_t bytes);
int NativePlotNumber(double value, char* output, int capacity, void* format);
}
