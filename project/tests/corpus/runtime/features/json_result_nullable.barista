func test() -> void:
	var null_success := JsonResult[String?].ok(null)
	print(null_success.is_ok())
	print(null_success.value == null)
	print(null_success.error == null)

	var value_success := JsonResult[int].ok(7)
	print(value_success.is_ok())
	print(value_success.value)

	var failure := JsonResult[String?].fail("bad", "$.value")
	print(failure.is_ok())
	print(failure.error.message)
	print(failure.error.path)

	var nested := JsonResult[String?].nested(
			JsonDecodeError.create("bad", "$.value"), "outer")
	print(nested.is_ok())
	print(nested.error.path)

	var empty := JsonResult[String?].new()
	print(empty.is_ok())
	empty.value = "assigned directly"
	print(empty.is_ok())

	failure.error = null
	print(failure.is_ok())

	null_success.error = JsonDecodeError.create("late error", "$")
	print(null_success.is_ok())
