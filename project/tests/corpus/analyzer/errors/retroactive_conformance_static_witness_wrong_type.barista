# A static witness supplied by a retroactive conformance carries its declared signature to the call
# site, so its `String` result cannot initialize an `int`. Without the signature the call would type as
# Variant and the mismatch would go unreported.
trait RtcStaticLabelMaker:
	abstract static func make_label() -> String

extend RtcStaticGadget uses RtcStaticLabelMaker:
	static func make_label() -> String:
		return "made"


func test() -> void:
	var count: int = RtcStaticGadget.make_label()
	print(count)
