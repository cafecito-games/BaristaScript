trait StaticFallback:
	static func claimed() -> String:
		return "trait"


class OrdinaryBase:
	func claimed() -> int:
		return 1


abstract class Receiver extends OrdinaryBase uses StaticFallback:
	pass


func probe() -> Callable[[], String]:
	return Receiver.claimed
