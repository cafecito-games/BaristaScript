# A case literally named `async` is a payload case, not the start of an `async func`
# declaration; "async" is only a contextual keyword and is a modifier only when it leads
# into another modifier or `func`. Parsing succeeds; the analyzer error below is the
# expected interim state until #1262 adds tagged-union typing (see enum_payload_cases.norun.fs
# for details).
enum Message:
	async(value: int)
