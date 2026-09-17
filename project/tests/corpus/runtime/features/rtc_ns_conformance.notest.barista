# Conforms `RtcNsWidget` to `RtcNsGadgetlike` from a third file that declares no global class of its
# own, so nothing in the project names it. Consumers reach it only because it declares its conformance
# in the `rtc_ns` namespace, which they import (or are in).
namespace rtc_ns

extend RtcNsWidget uses RtcNsGadgetlike:
	func gadget() -> String:
		return "gadget:" + label
