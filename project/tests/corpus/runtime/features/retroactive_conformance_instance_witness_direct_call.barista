# An instance witness supplied by a retroactive conformance dispatches to the same function whether the
# value is used through the trait type or directly on the concrete conformed type. The analyzer now
# resolves the direct form, so both calls type-check and print identical results. A `Self`-returning
# witness, called directly on the concrete receiver, types as that receiver. The companion target and
# trait live in the `rtc_instance_direct_*.notest.fs` files alongside this one.

extend RtcInstanceDirectGadget uses RtcInstanceDirectable:
	func ping() -> int:
		return power * 2

	func cloneish() -> Self:
		return self


func via_trait(p: RtcInstanceDirectable) -> int:
	return p.ping()


func test() -> void:
	var g := RtcInstanceDirectGadget.new()
	var p: RtcInstanceDirectable = g
	# Trait-typed and direct (concrete-receiver) forms dispatch the same witness.
	print(p.ping())
	print(g.ping())
	print(via_trait(g))
	# A Self-returning witness, called directly on the concrete receiver, types as that receiver.
	var g2: RtcInstanceDirectGadget = g.cloneish()
	print(g2 is RtcInstanceDirectGadget)
	print(g2.ping())
