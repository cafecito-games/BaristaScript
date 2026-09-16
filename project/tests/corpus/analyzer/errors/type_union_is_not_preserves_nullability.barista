# A failed type test says nothing about whether the value is null, so removal never narrows a
# nullable set to a non-null one. The member lookup below reports the surviving type, still nullable.
type MaybeScalar = int? | String


func classify(value: MaybeScalar) -> void:
	if value is not String:
		var probe := value.no_such_member
		print(probe)
