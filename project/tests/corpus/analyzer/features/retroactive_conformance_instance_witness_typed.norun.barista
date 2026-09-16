# An instance (non-static) witness supplied by a retroactive conformance resolves on a value of the
# conformed type, not only through the trait surface. Calling it directly on the concrete receiver
# type-checks fully — argument and return-type checking, and named-argument canonicalization — with no
# UNSAFE_METHOD_ACCESS and no STATIC_CALLED_ON_INSTANCE. The companion target and trait live in the
# `rtc_instance_witness_*.notest.fs` files alongside this one.

extend RtcInstanceWitnessTarget uses RtcInstanceWitnessable:
	func ping(amount: int) -> int:
		return amount * 100


# Not executed (.norun): this fixture pins the analyzer diagnostics, not the dispatch (which
# `runtime/features/retroactive_conformance_instance_witness_direct_call` covers).
func no_exec_test():
	var g := RtcInstanceWitnessTarget.new()
	# Hard-typed local assigned from a direct concrete-receiver call, using a named argument.
	var n: int = g.ping(amount = 3)
	print(n)
