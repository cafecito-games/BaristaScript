enum Status:
	UNKNOWN = 0
	READY = 1
	DONE = 2

	func is_ready() -> bool:
		return self == READY

	func replace_unknown(fallback: Status) -> Status:
		return fallback if self == UNKNOWN else self

	func announce() -> void:
		print("announce:", self)

	func describe(prefix: String = "") -> String:
		if self.is_ready():
			return prefix + "ready"
		return prefix + Status.label(self)

	static func label(value: Status) -> String:
		return "unknown" if value == UNKNOWN else "done"

	static func parse(text: String) -> Self:
		return Status.from_ready(text == "ready")

	static func from_ready(ready: bool) -> Self:
		return READY if ready else DONE


func test() -> void:
	var ready: Status = Status.parse("ready")
	var done: Status = Status.parse("other")
	var unknown: Status = Status.UNKNOWN
	ready.announce()
	print(ready.describe("state:"))
	print(done.describe())
	print(unknown.replace_unknown(ready).describe("fallback:"))
	print(Status.keys())
