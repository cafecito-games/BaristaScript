# A file's own namespace is implicitly imported, so a sibling in `rtc_ns2` reaches the conformance
# `rtc_ns2_conformance.notest.fs` declares without importing anything and without a `preload` — for
# `is` and for dispatching the instance witness at run time.
namespace rtc_ns2


func test() -> void:
	var widget := RtcNs2Widget.new()
	print(widget is RtcNs2Gadgetlike)
	print(widget.gadget())
