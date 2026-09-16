# A static witness's parameters are validated at the call site like any other statically resolved
# signature, so passing a String where the witness declares an int is an error.
trait RtcStaticScaler:
	abstract static func scale(factor: int) -> int

extend RtcStaticGadget uses RtcStaticScaler:
	static func scale(factor: int) -> int:
		return factor * 2


func test() -> void:
	print(RtcStaticGadget.scale("two"))
