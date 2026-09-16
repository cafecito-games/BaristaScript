# A `Self` written in a retroactive-conformance witness names the conformance target, not the
# receiver's leaf class: the witness is attached to one target type and its signature is resolved
# against that type, so the parameter below is a `TypeSelfWitnessReceiverCell` position rather than a
# receiver contract. The foreign-receiver identity rule therefore does not apply, and passing the
# calling frame's `self` is admitted by ordinary subtype reasoning -- which is also what the run time
# checks. This pins that reading so a later change to how a witness lowers `Self` has to revisit it.
extend TypeSelfWitnessReceiverCell uses TypeSelfWitnessReceiverTakes:
	func take(_other: Self) -> void:
		pass


class Caller extends TypeSelfWitnessReceiverCell:
	func route(other: TypeSelfWitnessReceiverCell) -> void:
		other.take(self)


func test() -> void:
	Caller.new().route(TypeSelfWitnessReceiverCell.new())
