# Normalization stores alternatives non-nullable, so removal has to ignore the `?` and look only at
# the type the test names -- otherwise `is not String?` could not remove the `String` alternative at
# all. Null itself still survives: a failed type test is not a null check, because the native, script
# and typed-container test opcodes answer false for null whatever the `?` said. The member lookup
# below reports the surviving type, still nullable.
type MaybeScalar = int? | String


func classify(value: MaybeScalar) -> void:
	if value is not String?:
		var probe := value.no_such_member
		print(probe)
