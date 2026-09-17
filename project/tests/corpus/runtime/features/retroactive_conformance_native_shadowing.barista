# Same trait conformed on a native base class and a derived class: witness dispatch is
# most-derived-wins, while `is`/`as` are satisfied by either declaration.
extend RefCounted uses RtcNativeShadowTrait:
	func ping() -> int:
		return 100


extend Resource uses RtcNativeShadowTrait:
	func ping() -> int:
		return 200


func test() -> void:
	var base := RefCounted.new()
	var derived := Resource.new()
	var trait_base: RtcNativeShadowTrait = base
	var trait_derived: RtcNativeShadowTrait = derived
	print(trait_base.ping())
	print(trait_derived.ping())
	print(base is RtcNativeShadowTrait)
	print(derived is RtcNativeShadowTrait)
	print((base as RtcNativeShadowTrait).ping())
	print((derived as RtcNativeShadowTrait).ping())
