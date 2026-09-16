# The engine-ancestor conformance backing
# retroactive_conformance_script_chain_rejects_conflicting_uses_deferred_load. The file that
# contradicts it only loads it from a function body, so it is not registered yet when that file's own
# `uses` clauses are checked.
extend RefCounted uses SccdKeeper[int]:
	func make() -> int:
		return 7
