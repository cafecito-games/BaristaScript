# The positive half of the conformance-visibility pair: this file loads
# `rtc_scoped_conformance.notest.fs`, so RtcScopedWidget satisfies RtcScopedTrait here and its static
# witness dispatches. Loading it also registers that conformance for the rest of the run, which is what
# makes `analyzer/errors/retroactive_conformance_unloaded_declaring_file` a real test rather than a
# vacuous one: the entry exists, and that file still must not see it.
const _Conformance = preload("../../analyzer/errors/rtc_scoped_conformance.notest.fs")


func test() -> void:
	var widget := RtcScopedWidget.new()
	var marked: RtcScopedTrait = widget
	print(marked != null)
	print(RtcScopedWidget.mark())
