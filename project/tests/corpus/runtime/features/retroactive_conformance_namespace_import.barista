# `import rtc_ns` is the only edge to the file declaring `extend RtcNsWidget uses RtcNsGadgetlike`:
# there is no `preload` here, the declaring file exports no global class, and both the target and the
# trait live outside `rtc_ns` so naming them cannot pull it in either. Importing the namespace both
# type-checks the conformance (trait-typed assignment, a trait-typed parameter, and `is`) and keeps
# the declaring file loaded, so the instance witness dispatches at run time instead of missing.
import rtc_ns


func via_param(gadget: RtcNsGadgetlike) -> String:
	return gadget.gadget()


func test() -> void:
	var widget := RtcNsWidget.new()
	print(widget is RtcNsGadgetlike)
	var as_gadget: RtcNsGadgetlike = widget
	print(as_gadget.gadget())
	print(via_param(widget))
	print(widget.gadget())
