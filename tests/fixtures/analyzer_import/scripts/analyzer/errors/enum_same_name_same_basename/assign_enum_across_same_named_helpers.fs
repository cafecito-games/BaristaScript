extends RefCounted

const HelperA = preload("./a/helper.notest.fs")
const HelperB = preload("./b/helper.notest.fs")

func test():
	var wrong: HelperB.Result = HelperA.Result.OK
	print(wrong)
