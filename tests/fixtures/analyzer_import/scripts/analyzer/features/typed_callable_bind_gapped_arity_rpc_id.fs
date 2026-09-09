# `Callable.rpc_id()` prepends a synthetic `peer_id: int` parameter to the surviving target arity.
# When a Callable's arity is transformed by bind(), the analyzer records the surviving arities in
# `method_extra_allowed_argument_counts` based on the target's own parameter count. The `rpc_id()`
# call must offset those recorded arities by one (for the synthetic peer_id), so a valid surviving
# arity is accepted just as it is for call(), rpc(), and call_deferred(). See issue #1673.
extends Node


# Four parameters with two trailing defaults. Binding `2` fills `q` (mirrors the existing
# typed_callable_bind_default_arity.fs behavior), leaving the target invocable at exactly one
# surviving argument (the first), with `r` and `s` defaulted. Returns void so the analyzer
# acceptance check is not coupled to a RETURN_VALUE_DISCARDED warning.
func gapped(_p: int, _q: int, _r: String = "x", _s: int = 1) -> void:
	pass


func test() -> void:
	var bound := Callable(self, "gapped").bind(2)
	# The same single surviving arity is accepted by every invocation form. The rpc()/rpc_id() calls
	# run on a Node that is not in the scene tree, so they emit a runtime error (captured in the
	# .out fixture); this test asserts only that the analyzer accepts the arity.
	bound.call(9)
	bound.rpc(9)
	# Regression for #1673: this was previously rejected with
	# "Too few arguments for 'rpc_id()' call. Expected at least 4 but received 2."
	bound.rpc_id(7, 9)
