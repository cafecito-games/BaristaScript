# A whole-file `enum_name` declaration may also carry payload cases. Parsing succeeds; the
# analyzer error below is the expected interim state until #1262 adds tagged-union typing
# (see enum_payload_cases.norun.fs for details).
enum_name Message:
	Quit
	Move(x: int, y: int)
	Write(text: String)
