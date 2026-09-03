# A tagged-union declaration with a payload-less case and payload-carrying cases.
# Tags are ordinal by declaration order, so no case may declare an explicit value; the
# analyzer error below is the expected interim state until #1262 adds tagged-union typing
# (`Enum values must have an explicit integer value.` currently fires unconditionally for
# every case, payload or not, since the analyzer has not been taught the new shape yet).
enum Message:
	Quit
	Move(x: int, y: int)
	Write(text: String)
