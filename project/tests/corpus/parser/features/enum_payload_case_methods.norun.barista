# Enum methods remain supported on a tagged union; inside a method `self` is the case
# value. Parsing succeeds; the analyzer error below is the expected interim state until
# #1262 adds tagged-union typing (see enum_payload_cases.norun.fs for details).
enum Message:
	Quit
	Move(x: int, y: int)

	func describe() -> String:
		return "message"
