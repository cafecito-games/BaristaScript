func test():
	# Loading a script that extends a final base must fail at load time. This path
	# bypasses the analyzer pass that already rejects the same code statically, so
	# the load-time guard is what refuses the dynamically loaded extension here.
	var derived: Resource = load("res://runtime/features/final_derived.notest.fs")
	prints("derived loaded:", derived != null)
