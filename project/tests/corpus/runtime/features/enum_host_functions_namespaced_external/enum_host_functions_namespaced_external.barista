import issue_1123.enum_integration

func test() -> void:
	var ready: IntegratedStatus = IntegratedStatus.parse("ready")
	var done: IntegratedStatus = IntegratedStatus.parse("other")
	var recovered: IntegratedStatus = IntegratedStatus.UNKNOWN.or_else(ready)
	print(ready.label("status:"))
	print(done.label())
	print(recovered.label("fallback:"))
	print(IntegratedStatus.keys())
