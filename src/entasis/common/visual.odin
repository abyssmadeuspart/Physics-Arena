package common

write_transform :: proc(writer: ^Writer, position: Vector3, rotation: Quaternion)
{
	write_vector(writer, position)
	write_f32(writer, rotation.x)
	write_f32(writer, rotation.y)
	write_f32(writer, rotation.z)
	write_f32(writer, rotation.w)
}
