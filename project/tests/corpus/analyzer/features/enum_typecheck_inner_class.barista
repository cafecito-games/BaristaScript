@warning_ignore("mixed_namespace_directory")
class_name EnumTypecheckOuterClass

enum MyEnum:
	V0 = 0
	V1 = V0 + 1
	V2 = V1 + 1

class InnerClass:
	enum InnerEnum:
		V0 = 0
		V2 = V0 + 1
		V1 = V2 + 1

	static func test_inner_from_inner():
		print("Inner - Inner")
		var e1: InnerEnum
		var e2: InnerClass.InnerEnum
		var e3: EnumTypecheckOuterClass.InnerClass.InnerEnum

		@warning_ignore("unassigned_variable")
		print("Self ", e1, e2, e3)
		e1 = InnerEnum.V1
		e2 = InnerEnum.V1
		e3 = InnerEnum.V1
		print("MyEnum ", e1, e2, e3)
		e1 = InnerClass.InnerEnum.V1
		e2 = InnerClass.InnerEnum.V1
		e3 = InnerClass.InnerEnum.V1
		print("Inner.MyEnum ", e1, e2, e3)
		e1 = EnumTypecheckOuterClass.InnerClass.InnerEnum.V1
		e2 = EnumTypecheckOuterClass.InnerClass.InnerEnum.V1
		e3 = EnumTypecheckOuterClass.InnerClass.InnerEnum.V1
		print("Outer.Inner.MyEnum ", e1, e2, e3)

		e1 = e2
		e1 = e3
		e2 = e1
		e2 = e3
		e3 = e1
		e3 = e2

		print()

	static func test_outer_from_inner():
		print("Inner - Outer")
		var e: EnumTypecheckOuterClass.MyEnum

		e = EnumTypecheckOuterClass.MyEnum.V1
		print("Outer.MyEnum ", e)

		print()

func test_outer_from_outer():
	print("Outer - Outer")
	var e1: MyEnum
	var e2: EnumTypecheckOuterClass.MyEnum

	@warning_ignore("unassigned_variable")
	print("Self ", e1, e2)
	e1 = MyEnum.V1
	e2 = MyEnum.V1
	print("Outer ", e1, e2)
	e1 = EnumTypecheckOuterClass.MyEnum.V1
	e2 = EnumTypecheckOuterClass.MyEnum.V1
	print("Outer.MyEnum ", e1, e2)

	e1 = e2
	e2 = e1

	print()

func test_inner_from_outer():
	print("Outer - Inner")
	var e1: InnerClass.InnerEnum
	var e2: EnumTypecheckOuterClass.InnerClass.InnerEnum

	@warning_ignore("unassigned_variable")
	print("Inner ", e1, e2)
	e1 = InnerClass.InnerEnum.V1
	e2 = InnerClass.InnerEnum.V1
	print("Outer.Inner ", e1, e2)
	e1 = EnumTypecheckOuterClass.InnerClass.InnerEnum.V1
	e2 = EnumTypecheckOuterClass.InnerClass.InnerEnum.V1
	print("Outer.Inner.MyEnum ", e1, e2)

	e1 = e2
	e2 = e1

	print()

func test():
	test_outer_from_outer()
	test_inner_from_outer()
	InnerClass.test_outer_from_inner()
	InnerClass.test_inner_from_inner()
