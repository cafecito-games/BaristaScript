enum LogLevel:
	INFO = 1
	WARNING = 2
	ERROR = 3

	func name() -> String:
		return "name"

	static func parse(p_name: String) -> int:
		return p_name.length()

enum FunctionsOnly:
	func value() -> int:
		return 1

enum ValueOnly:
	ONE = 1
	async = 2

enum Empty:
	pass

class EnumHost:
	enum Nested:
		ACTIVE = 1

		static async func load() -> int:
			return ACTIVE
