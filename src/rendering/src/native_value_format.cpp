#include "native_value_format.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace benchmark_visual
{
NativeValueFormat SelectNativeValueFormat(NativeValueDomain domain, double maximumMagnitude)
{
	if (domain == NativeValueDomain_Milliseconds)
	{
		if (maximumMagnitude >= 1000)
			return {domain, 1000, "s"};
		if (maximumMagnitude > 0 && maximumMagnitude < 0.001)
			return {domain, 0.000001, "ns"};
		if (maximumMagnitude > 0 && maximumMagnitude < 1)
			return {domain, 0.001, "us"};
		return {domain, 1, "ms"};
	}
	if (domain == NativeValueDomain_Rate)
	{
		if (maximumMagnitude >= 1e9)
			return {domain, 1e9, "G"};
		if (maximumMagnitude >= 1e6)
			return {domain, 1e6, "M"};
		if (maximumMagnitude >= 1e3)
			return {domain, 1e3, "k"};
	}
	return {domain, 1, ""};
}

NativeValueText GroupNativeDigits(std::string_view number)
{
	NativeValueText text = {};
	const std::size_t sign = number.starts_with('-') ? 1 : 0;
	const std::size_t decimal = number.find('.');
	const std::size_t integerEnd = decimal == std::string_view::npos ? number.size() : decimal;
	std::size_t output = 0;
	for (std::size_t index = 0; index < number.size(); ++index)
	{
		if (index > sign && index < integerEnd && (integerEnd - index) % 3 == 0)
			text[output++] = ',';
		text[output++] = number[index];
	}
	return text;
}

NativeValueText FormatNativeValue(double value, NativeValueFormat format)
{
	NativeValueText text = {};
	const double scaled = value / format.divisor;
	const double magnitude = std::abs(scaled);
	const int quantity = format.domain == NativeValueDomain_Quantity;
	const double resolution = quantity != 0 ? 0.000001 : 0.0001;
	if (magnitude > 0 && magnitude < resolution)
	{
		std::snprintf(text.data(), text.size(), "%s%s", scaled < 0 ? ">-" : "<", quantity != 0 ? "0.000001" : "0.0001");
		return text;
	}
	const int precision = quantity != 0 ? (magnitude > 0 && magnitude < 0.0005 ? 6 : 3) :
	    (magnitude > 0 && magnitude < 0.005 ? 4 : 2);
	const std::to_chars_result written = std::to_chars(text.data(), text.data() + text.size() - 1,
	    scaled == 0 ? 0 : scaled, std::chars_format::fixed, precision);
	std::size_t size = static_cast<std::size_t>(written.ptr - text.data());
	if (quantity != 0)
	{
		while (size != 0 && text[size - 1] == '0')
			--size;
		if (size != 0 && text[size - 1] == '.')
			--size;
	}
	return GroupNativeDigits(std::string_view(text.data(), size));
}

NativeValueText FormatNativeRaw(double value)
{
	NativeValueText text = {};
	std::to_chars(text.data(), text.data() + text.size() - 1, value, std::chars_format::general, std::numeric_limits<double>::max_digits10);
	return text;
}

NativeValueText FormatNativeRawCount(std::uint64_t value)
{
	NativeValueText text = {};
	std::to_chars(text.data(), text.data() + text.size() - 1, value);
	return text;
}

NativeValueText FormatNativeCount(std::uint64_t value)
{
	const NativeValueText raw = FormatNativeRawCount(value);
	return GroupNativeDigits(raw.data());
}

NativeValueText FormatNativeUnit(NativeValueFormat format, std::string_view declaredUnit)
{
	NativeValueText text = {};
	if (format.domain == NativeValueDomain_Milliseconds)
		std::snprintf(text.data(), text.size(), "%s", format.unitPrefix);
	else
		std::snprintf(text.data(), text.size(), "%s%s%.*s", format.unitPrefix, format.unitPrefix[0] == '\0' ? "" : " ",
		    static_cast<int>(declaredUnit.size()), declaredUnit.data());
	return text;
}

NativeValueText FormatNativeThreadSet(const std::uint32_t* values, std::uint32_t count)
{
	NativeValueText text = {};
	std::size_t used = 0;
	for (std::uint32_t index = 0; index < count; ++index)
	{
		const std::uint32_t first = values[index];
		while (index + 1 < count && values[index + 1] == values[index] + 1)
			++index;
		const int written = first == values[index] ?
		    std::snprintf(text.data() + used, text.size() - used, "%s%u", used == 0 ? "" : ", ", first) :
		    std::snprintf(text.data() + used, text.size() - used, "%s%u-%u", used == 0 ? "" : ", ", first, values[index]);
		used += static_cast<std::size_t>(written);
	}
	return text;
}

NativeValueText FormatNativeBytes(std::uint64_t bytes)
{
	constexpr std::array<const char*, 7> units = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
	double value = static_cast<double>(bytes);
	std::size_t unit = 0;
	while (value >= 1023.995 && unit + 1 < units.size())
	{
		value /= 1024;
		++unit;
	}
	NativeValueText text = {};
	if (unit == 0)
		std::snprintf(text.data(), text.size(), "%s B", FormatNativeCount(bytes).data());
	else
		std::snprintf(text.data(), text.size(), "%.2f %s", value, units[unit]);
	return text;
}

int NativePlotNumber(double value, char* output, int capacity, void* format)
{
	const NativeValueText text = FormatNativeValue(value, *static_cast<const NativeValueFormat*>(format));
	const int size = (std::min)(static_cast<int>(std::strlen(text.data())), capacity - 1);
	std::copy_n(text.data(), size, output);
	output[size] = '\0';
	return size;
}
}
