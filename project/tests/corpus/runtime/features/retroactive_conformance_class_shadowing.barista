# FS-class analog of native inheritance-chain shadowing: most-derived witness wins.
extend RtcClassShadowBase uses RtcClassShadowTrait:
	func value() -> int:
		return 10


extend RtcClassShadowDerived uses RtcClassShadowTrait:
	func value() -> int:
		return 20


func test() -> void:
	var base := RtcClassShadowBase.new()
	var derived := RtcClassShadowDerived.new()
	var trait_base: RtcClassShadowTrait = base
	var trait_derived: RtcClassShadowTrait = derived
	print(trait_base.value())
	print(trait_derived.value())
	print(base is RtcClassShadowTrait)
	print(derived is RtcClassShadowTrait)
	print((base as RtcClassShadowTrait).value())
	print((derived as RtcClassShadowTrait).value())
