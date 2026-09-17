# Cross-file generic tagged unions construct, match, and keep independent specializations at runtime.
import generic_union_fixture

const Provider = preload("../../analyzer/features/generic_tagged_union_global_values.notest.fs")

func describe_int_string(value: GlobalResult[int, String]) -> String:
	match value:
		GlobalResult[int, String].Ok(var number):
			return "ok %d" % number
		GlobalResult[int, String].Err(var message):
			return "err %d" % message.length()
	return "unreachable"

func test():
	print(describe_int_string(GlobalResult[int, String].Ok(3)))
	print(describe_int_string(GlobalResult[int, String].Err("four")))
	print(describe_int_string(generic_union_fixture.GlobalResult[int, String].Ok(5)))
	print(describe_int_string(generic_union_fixture.GlobalResult[int, String].Err("six")))
	print(GlobalResult[int, String].Ok(7))
	print(GlobalResult[int, String].Ok(7) == generic_union_fixture.GlobalResult[int, String].Ok(7))
	print(Provider.GlobalResult)
