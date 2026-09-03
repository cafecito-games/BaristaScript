# Trailing commas are allowed in an enum case payload field list. Parsing succeeds; the
# analyzer error below is the expected interim state until #1262 adds tagged-union typing
# (see enum_payload_cases.norun.fs for details).
enum Message:
	Move(x: int, y: int,)
