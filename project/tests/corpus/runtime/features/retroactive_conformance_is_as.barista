# `is` / `as` against a retroactively-conformed trait. `RtcCoin` does not declare `RtcGlint`; the
# `extend` below supplies `shine()` externally. Regression for the `has_script_trait` fix: the trait
# membership query must report `true` for a retroactively-conformed value (both through the target's
# static type and through a widened `Object`), and the `as` cast must succeed and dispatch through the
# witness.
extend RtcCoin uses RtcGlint:
	func shine() -> int:
		return rank * 5


func test() -> void:
	var coin := RtcCoin.new()
	print(coin is RtcGlint)
	var glint := coin as RtcGlint
	print(glint.shine())
	var as_object: Object = coin
	print(as_object is RtcGlint)
