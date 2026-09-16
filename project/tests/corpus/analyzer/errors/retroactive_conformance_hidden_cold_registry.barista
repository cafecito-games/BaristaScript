# The conformance registry only holds what the running process happened to analyze, so this call used
# to be rejected only when some other file had already pulled `rtc_cold_conformance.notest.fs` in.
# The fixture runner clears the registry before every fixture, so this file is analyzed cold: the
# error below can only come from the project-wide declaration index, not from analysis order.
func test() -> void:
	var widget := RtcColdWidget.new()
	print(widget.cold_gadget())
