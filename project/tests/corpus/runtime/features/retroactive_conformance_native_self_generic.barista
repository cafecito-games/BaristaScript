# A native engine class is retroactively conformed to a trait whose static requirement returns a
# generic instantiated with `Self`. The stand-in the conformance analyzes through is not a real
# class, so the `Self` argument has to be lowered as the native target rather than as an
# identity-less script: otherwise this file runs but cannot be exported to compiled bytecode.
extend Resource uses RtcNativeSelfy:
	static func wrapped() -> JsonResult[Self]:
		return JsonResult[Self].fail("nope", "$")


func test() -> void:
	var result := Resource.wrapped()
	print(result.error.message)
	print(result.error.path)
