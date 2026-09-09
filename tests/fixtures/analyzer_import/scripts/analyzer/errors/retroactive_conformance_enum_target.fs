# Enum targets are not supported for retroactive conformance.
enum RtcEnumTarget:
	ONE = 0
	TWO = ONE + 1

extend RtcEnumTarget uses RtcEnumTrait:
	func describe() -> String:
		return "enum"
