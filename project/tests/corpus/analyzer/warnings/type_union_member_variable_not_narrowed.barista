# Flow narrowing keys on parameters, locals, iterators and binds. A member variable is a legal place
# for a set type but is never narrowed, so the `String`-only method below stays unresolved. Copying
# the member into a local first is the supported pattern, and narrows exactly as a parameter does.
type Scalar = int | String


var stored: Scalar = "member"


func length_of_stored() -> int:
	if stored is String:
		return stored.length()
	return 0


func length_of_stored_copy() -> int:
	var local := stored
	if local is String:
		return local.length()
	return 0


func test():
	prints(length_of_stored(), length_of_stored_copy())
