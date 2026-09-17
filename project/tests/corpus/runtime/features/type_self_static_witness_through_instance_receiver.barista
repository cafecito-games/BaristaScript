# A retroactively supplied *static* witness whose signature references `Self`, reached through an
# instance receiver rather than the target type, resolves `Self` against the receiver just as the
# `RtcAdoptionTarget.adopt(...)` form does. The witness is not owned by the target's file, so this
# call lands in the conformance fallback rather than in the class's own member functions.
@warning_ignore_start("static_called_on_instance")

extend RtcAdoptionTarget uses RtcSelfAdoptable:
	static func adopt(value: Self) -> Self:
		print("adopted value is a target: %s" % [value is RtcAdoptionTarget])
		return value


func test() -> void:
	var target := RtcAdoptionTarget.new()
	print("instance receiver adopted: %s" % [target.adopt(RtcAdoptionTarget.new()) is RtcAdoptionTarget])
	print("target handle adopted: %s" % [RtcAdoptionTarget.adopt(RtcAdoptionTarget.new()) is RtcAdoptionTarget])
