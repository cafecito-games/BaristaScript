# Companion to features/typed_callable_bind_gapped_arity_rpc_id.fs (issue #1673). The synthetic
# `peer_id` parameter offsets the recorded surviving arities for rpc_id() by one, but it must not
# loosen the minimum-arity check: a call below the minimum surviving arity is still rejected.
extends Node


func gapped(_p: int, _q: int, _r: String = "x", _s: int = 1) -> void:
	pass


func test() -> void:
	var bound := Callable(self, "gapped").bind(2)
	# peer_id only; zero surviving arguments is below the minimum of one. The recorded surviving
	# arity is one, which offsets to two (peer_id + one arg), so a single-argument rpc_id() call
	# must still be rejected.
	bound.rpc_id(7)
