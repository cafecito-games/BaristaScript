@warning_ignore("mixed_namespace_directory")
class_name TestExportEnumAsDictionary

enum MyEnum:
	A = 0
	B = A + 1
	C = B + 1

@export var test_1 = MyEnum
@export var test_2 = MyEnum.A
@export var test_3 := MyEnum
@export var test_4 := MyEnum.A
@export var test_5: MyEnum

func test():
	for property in get_property_list():
		if str(property.name).begins_with("test_"):
			Utils.print_property_extended_info(property)
