# The unsigned carrier behaves exactly like the signed one: every `uint` value is also a `ulong`
# value, so a failed `is ulong` rules out `uint` too. Only `float` survives here, and the member
# lookup below reports that narrowed type.
type Unsigned = uint | ulong | float


func classify(value: Unsigned) -> void:
	if value is not ulong:
		var probe := value.no_such_member
		print(probe)
