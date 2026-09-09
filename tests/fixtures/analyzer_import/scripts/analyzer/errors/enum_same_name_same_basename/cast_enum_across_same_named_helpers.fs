extends RefCounted

const HelperA = preload("./a/helper.notest.fs")
const HelperB = preload("./b/helper.notest.fs")

func test():
	var wrong := HelperA.Result as HelperB.Result
	print(wrong)
