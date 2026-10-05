package common

import "core:encoding/endian"

Writer :: struct
{
	bytes: []u8,
	offset: int,
	status: Status,
}

write_u8 :: proc(writer: ^Writer, value: u8)
{
	if writer.offset == len(writer.bytes)
	{
		writer.status = .Capacity
		return
	}
	writer.bytes[writer.offset] = value
	writer.offset += 1
}

write_u16 :: proc(writer: ^Writer, value: u16)
{
	if !endian.put_u16(writer.bytes[writer.offset:], .Little, value)
	{
		writer.status = .Capacity
		return
	}
	writer.offset += 2
}

write_u32 :: proc(writer: ^Writer, value: u32)
{
	if !endian.put_u32(writer.bytes[writer.offset:], .Little, value)
	{
		writer.status = .Capacity
		return
	}
	writer.offset += 4
}

write_u64 :: proc(writer: ^Writer, value: u64)
{
	if !endian.put_u64(writer.bytes[writer.offset:], .Little, value)
	{
		writer.status = .Capacity
		return
	}
	writer.offset += 8
}

write_f32 :: proc(writer: ^Writer, value: f32)
{
	write_u32(writer, transmute(u32)value)
}

write_f64 :: proc(writer: ^Writer, value: f64)
{
	write_u64(writer, transmute(u64)value)
}

write_vector :: proc(writer: ^Writer, value: Vector3)
{
	write_f32(writer, value.x)
	write_f32(writer, value.y)
	write_f32(writer, value.z)
}

write_fixed_text :: proc(writer: ^Writer, value: string, width: int)
{
	if len(value) >= width || width > len(writer.bytes) - writer.offset
	{
		writer.status = .Capacity
		return
	}
	for index in 0..<width
	{
		writer.bytes[writer.offset + index] = 0
	}
	copy(writer.bytes[writer.offset:writer.offset + len(value)], transmute([]u8)value)
	writer.offset += width
}

read_fixed_text :: proc(reader: ^Reader, width: int) -> string
{
	if width > len(reader.bytes) - reader.offset
	{
		reader.status = .Invalid
		return ""
	}
	bytes: []u8 = reader.bytes[reader.offset:reader.offset + width]
	reader.offset += width
	for value, index in bytes
	{
		if value == 0
		{
			return string(bytes[:index])
		}
	}
	reader.status = .Invalid
	return ""
}
