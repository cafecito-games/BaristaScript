# Companion to typed_callable_bind_gapped_arity_rpc_id.fs for the unbind() producer (issue #1673
# AC 2). The unbind() transformer shifts the surviving arities recorded in
# `method_extra_allowed_argument_counts`; rpc_id() must still offset them by one for the synthetic
# peer_id. The issue body verifies this exact reproduction path.
extends Node


# Four parameters with two trailing defaults. bind(2) fills `q`; unbind(1) then drops one trailing
# argument, producing a surviving arity shape whose recorded extra counts flow through the same
# corrected consumer as the bind-only path.
func gapped(_p: int, _q: int, _r: String = "x", _s: int = 1) -> void:
	pass


func test() -> void:
	var bound := Callable(self, "gapped").bind(2).unbind(1)
	# Regression for #1673 (unbind producer): this was previously rejected with
	# "Too few arguments for 'rpc_id()' call. Expected at least 5 but received 3."
	# The rpc() / rpc_id() calls run on a Node not in the scene tree, so they emit a runtime error
	# captured in the .out fixture; this test asserts only analyzer acceptance of the arity.
	bound.rpc(9, 5)
	bound.rpc_id(7, 9, 5)
