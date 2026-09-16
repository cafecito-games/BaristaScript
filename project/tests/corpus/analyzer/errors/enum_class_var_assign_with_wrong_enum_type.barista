enum MyEnum:
	ENUM_VALUE_1 = 0
	ENUM_VALUE_2 = ENUM_VALUE_1 + 1
enum MyOtherEnum:
	OTHER_ENUM_VALUE_1 = 0
	OTHER_ENUM_VALUE_2 = OTHER_ENUM_VALUE_1 + 1

# Different enum types can't be assigned without casting.
var class_var: MyEnum = MyEnum.ENUM_VALUE_1

func test():
	print(class_var)
	class_var = MyOtherEnum.OTHER_ENUM_VALUE_2
	print(class_var)
