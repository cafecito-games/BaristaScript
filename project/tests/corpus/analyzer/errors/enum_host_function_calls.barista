enum Status:
	READY = 1
	DONE = 2

	func label() -> String:
		return "ready" if self == READY else "done"

	static func parse(text: String) -> Self:
		return READY if text == "ready" else DONE


func test() -> void:
	var value: Status = Status.DONE
	var literal_label: String = Status.READY.label()
	var typed_label: String = value.label()
	var parsed: Status = Status.parse("ready")
	var instance_callable: Callable[[], String] = value.label
	var static_callable: Callable[[String], Status] = Status.parse
	var keys: Array = Status.keys()
	print(literal_label, typed_label, parsed, instance_callable, static_callable, keys)
	var _analyzer_only_stop: EnumCallAnalyzerSentinel
