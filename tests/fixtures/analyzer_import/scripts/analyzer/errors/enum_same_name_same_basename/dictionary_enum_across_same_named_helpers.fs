const HelperA = preload("./a/helper.notest.fs")
const HelperB = preload("./b/helper.notest.fs")

func test() -> void:
	var wrong: Dictionary[String, HelperB.Result] = {"k": HelperA.Result.OK}
	var wrong_key: Dictionary[HelperB.Result, String] = {HelperA.Result.OK: "k"}
	print(wrong, wrong_key)
