# Same-carrier numeric alternatives cannot be told apart in general: every `int` value passes
# `is long` too. Testing the wider alternative first therefore consumes the narrower arm, and once
# removal is downward-closed the surviving set no longer holds anything the later test could match,
# so the chain is rejected instead of silently never running. Narrowest-first is the fix.
#
# The survivor set here is the multi-member `String | float`, which is only one of the three shapes
# the rule has to cover; `type_union_subsumed_type_test_chain_single_survivor` collapses it to one
# member and `type_union_subsumed_type_test_chain_exhausted` empties it entirely.
type Scalar = int | long | float | String


func widest_first(value: Scalar) -> String:
	if value is long:
		return "long"
	elif value is int:
		return "int"
	elif value is float:
		return "float"
	return "String"
