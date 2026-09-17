# Conforms `RtcNs2Widget` to `RtcNs2Gadgetlike` from a file that declares no global class of its own.
# `retroactive_conformance_namespace_sibling` reaches it only by being in `rtc_ns2` itself.
namespace rtc_ns2

extend RtcNs2Widget uses RtcNs2Gadgetlike:
	func gadget() -> String:
		return "gadget:" + label
