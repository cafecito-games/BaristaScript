@warning_ignore("mixed_namespace_directory")
class_name EnumAccessOuterClass

class InnerClass:
	enum InnerEnum:
		V0 = 0
		V2 = V0 + 1
		V1 = V2 + 1

	static func print_enums():
		print("Inner - Inner")
		print(InnerEnum.V0, InnerEnum.V1, InnerEnum.V2)
		print(InnerClass.InnerEnum.V0, InnerClass.InnerEnum.V1, InnerClass.InnerEnum.V2)
		print(EnumAccessOuterClass.InnerClass.InnerEnum.V0, EnumAccessOuterClass.InnerClass.InnerEnum.V1, EnumAccessOuterClass.InnerClass.InnerEnum.V2)

		print("Inner - Outer")
		print(EnumAccessOuterClass.MyEnum.V0, EnumAccessOuterClass.MyEnum.V1, EnumAccessOuterClass.MyEnum.V2)


enum MyEnum:
	V0 = 0
	V1 = V0 + 1
	V2 = V1 + 1

func print_enums():
	print("Outer - Outer")
	print(MyEnum.V0, MyEnum.V1, MyEnum.V2)
	print(EnumAccessOuterClass.MyEnum.V0, EnumAccessOuterClass.MyEnum.V1, EnumAccessOuterClass.MyEnum.V2)

	print("Outer - Inner")
	print(InnerClass.InnerEnum.V0, InnerClass.InnerEnum.V1, InnerClass.InnerEnum.V2)
	print(EnumAccessOuterClass.InnerClass.InnerEnum.V0, EnumAccessOuterClass.InnerClass.InnerEnum.V1, EnumAccessOuterClass.InnerClass.InnerEnum.V2)

func test():
	print_enums()
	InnerClass.print_enums()
