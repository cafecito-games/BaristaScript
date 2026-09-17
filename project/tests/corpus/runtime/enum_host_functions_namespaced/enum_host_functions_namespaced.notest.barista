namespace qualified_enum.repro

enum_name Status:
	READY = 1
	BUSY = 2

	func label() -> String:
		return "ready" if self == READY else "busy"

	static func parse(text: String) -> Self:
		return READY if text == "ready" else BUSY
