@warning_ignore("mixed_namespace_directory")
class_name EnumFunctionTypecheckOuterClass

enum MyEnum:
	V0 = 0
	V1 = V0 + 1
	V2 = V1 + 1

class InnerClass:
	enum InnerEnum:
		V0 = 0
		V2 = V0 + 1
		V1 = V2 + 1

	func inner_inner_no_class(e: InnerEnum) -> InnerEnum:
		print(e)
		return e

	func inner_inner_class(e: InnerClass.InnerEnum) -> InnerClass.InnerEnum:
		print(e)
		return e

	func inner_inner_class_class(e: EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum) -> EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum:
		print(e)
		return e

	func inner_outer(e: EnumFunctionTypecheckOuterClass.MyEnum) -> EnumFunctionTypecheckOuterClass.MyEnum:
		print(e)
		return e

	func test():
		var _d
		print("Inner")

		var o := EnumFunctionTypecheckOuterClass.new()

		_d = o.outer_outer_no_class(EnumFunctionTypecheckOuterClass.MyEnum.V1)
		print()
		_d = o.outer_outer_class(EnumFunctionTypecheckOuterClass.MyEnum.V1)
		print()
		_d = o.outer_inner_class(InnerEnum.V1)
		_d = o.outer_inner_class(InnerClass.InnerEnum.V1)
		_d = o.outer_inner_class(EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum.V1)
		print()
		_d = o.outer_inner_class_class(InnerEnum.V1)
		_d = o.outer_inner_class_class(InnerClass.InnerEnum.V1)
		_d = o.outer_inner_class_class(EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum.V1)
		print()
		print()


		_d = inner_inner_no_class(InnerEnum.V1)
		_d = inner_inner_no_class(InnerClass.InnerEnum.V1)
		_d = inner_inner_no_class(EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum.V1)
		print()
		_d = inner_inner_class(InnerEnum.V1)
		_d = inner_inner_class(InnerClass.InnerEnum.V1)
		_d = inner_inner_class(EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum.V1)
		print()
		_d = inner_inner_class_class(InnerEnum.V1)
		_d = inner_inner_class_class(InnerClass.InnerEnum.V1)
		_d = inner_inner_class_class(EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum.V1)
		print()
		_d = inner_outer(EnumFunctionTypecheckOuterClass.MyEnum.V1)
		print()
		print()


func outer_outer_no_class(e: MyEnum) -> MyEnum:
	print(e)
	return e

func outer_outer_class(e: EnumFunctionTypecheckOuterClass.MyEnum) -> EnumFunctionTypecheckOuterClass.MyEnum:
	print(e)
	return e

func outer_inner_class(e: InnerClass.InnerEnum) -> InnerClass.InnerEnum:
	print(e)
	return e

func outer_inner_class_class(e: EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum) -> EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum:
	print(e)
	return e

func test():
	var _d
	print("Outer")

	_d = outer_outer_no_class(MyEnum.V1)
	_d = outer_outer_no_class(EnumFunctionTypecheckOuterClass.MyEnum.V1)
	print()
	_d = outer_outer_class(MyEnum.V1)
	_d = outer_outer_class(EnumFunctionTypecheckOuterClass.MyEnum.V1)
	print()
	_d = outer_inner_class(InnerClass.InnerEnum.V1)
	_d = outer_inner_class(EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum.V1)
	print()
	_d = outer_inner_class_class(InnerClass.InnerEnum.V1)
	_d = outer_inner_class_class(EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum.V1)
	print()
	print()

	var i := EnumFunctionTypecheckOuterClass.InnerClass.new()

	_d = i.inner_inner_no_class(InnerClass.InnerEnum.V1)
	_d = i.inner_inner_no_class(EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum.V1)
	print()
	_d = i.inner_inner_class(InnerClass.InnerEnum.V1)
	_d = i.inner_inner_class(EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum.V1)
	print()
	_d = i.inner_inner_class_class(InnerClass.InnerEnum.V1)
	_d = i.inner_inner_class_class(EnumFunctionTypecheckOuterClass.InnerClass.InnerEnum.V1)
	print()
	_d = i.inner_outer(MyEnum.V1)
	_d = i.inner_outer(EnumFunctionTypecheckOuterClass.MyEnum.V1)
	print()
	print()

	i.test()
