# Regression for issue #1673 (AC 5): the synthetic `peer_id` parameter prepended for rpc_id() is
# still type-checked as `int` at argument index 1, and the per-argument index is not shifted by the
# extra-arity offset. Passing a non-int first argument must report `argument 1` and `int`.
extends Node


func gapped(_p: int, _q: int, _r: String = "x", _s: int = 1) -> void:
	pass


func test() -> void:
	var bound := Callable(self, "gapped").bind(2)
	# peer_id is the first argument and must be int; a String literal must be rejected at argument 1.
	bound.rpc_id("not_an_int", 9)
