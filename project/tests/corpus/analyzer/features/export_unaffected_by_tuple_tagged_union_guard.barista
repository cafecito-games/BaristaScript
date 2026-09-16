@warning_ignore("mixed_namespace_directory")
class_name TestExportUnaffectedByTupleTaggedUnionGuard

enum PlainEnum:
	A = 0
	B = 1
	C = 2

@export var plain_enum: PlainEnum = PlainEnum.A
@export var numbers: Array[int] = [1, 2, 3]

func test():
	for property in get_property_list():
		if str(property.name).begins_with("plain_enum") or str(property.name).begins_with("numbers"):
			Utils.print_property_extended_info(property)
