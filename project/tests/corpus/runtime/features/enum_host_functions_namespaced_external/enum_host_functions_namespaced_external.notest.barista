namespace issue_1123.enum_integration

enum_name IntegratedStatus:
	UNKNOWN = 0
	READY = 10
	DONE = 20

	func or_else(fallback: Self) -> Self:
		return fallback if self == UNKNOWN else self

	func label(prefix: String = "") -> String:
		return prefix + ("ready" if self == READY else "done")

	static func parse(text: String) -> Self:
		return READY if text == "ready" else DONE
