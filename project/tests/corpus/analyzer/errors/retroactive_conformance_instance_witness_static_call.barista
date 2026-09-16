# Only a `static` witness is reachable through the target type. An instance witness supplied by a
# retroactive conformance still needs a receiver, so naming it on the type is an error.
trait RtcStaticProbeable:
	abstract func probe() -> String

extend RtcStaticGadget uses RtcStaticProbeable:
	func probe() -> String:
		return "probed"


func test() -> void:
	print(RtcStaticGadget.probe())
