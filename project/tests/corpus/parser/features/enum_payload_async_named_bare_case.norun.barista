# A bare payload-less case literally named `async` (alongside a payload-bearing case, so
# the enum is a tagged union and no case may declare a value) is not mistaken for the start
# of an `async func` declaration either. Parsing succeeds; the analyzer error below is the
# expected interim state until #1262 adds tagged-union typing (see enum_payload_cases.norun.fs
# for details).
enum Message:
	Move(x: int, y: int)
	async
