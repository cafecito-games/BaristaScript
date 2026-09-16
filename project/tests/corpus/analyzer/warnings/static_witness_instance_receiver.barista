# A `static` witness supplied by a retroactive conformance is resolvable through an instance receiver,
# not only through the target type: the runtime falls back to the conformance registry once the
# receiver's own member functions miss, so analysis has to resolve the same witness the dispatch will
# reach. The only warning the instance form earns is the one for naming a static through an instance.

extend RtcWarningAdoptionTarget uses RtcSelfAdoptable:
	static func adopt(value: Self) -> Self:
		return value


# Not executed: this fixture exists to pin the diagnostics, not the dispatch (which
# `runtime/features/type_self_static_witness_through_instance_receiver` covers).
func no_exec_test():
	var target := RtcWarningAdoptionTarget.new()
	var adopted := target.adopt(RtcWarningAdoptionTarget.new())
	print(adopted)

	# The type-name form was always resolvable and stays warning-free.
	print(RtcWarningAdoptionTarget.adopt(RtcWarningAdoptionTarget.new()))


func test():
	pass
