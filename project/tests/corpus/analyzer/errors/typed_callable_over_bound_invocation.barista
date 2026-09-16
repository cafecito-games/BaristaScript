# bind()/bindv() are variadic, so over-binding a fixed-arity typed callable (more bound arguments
# than the target accepts) is not itself an error. The resulting callable can never be invoked
# successfully, though, so the analyzer flags any invocation of it: call(), callv(), call_deferred(),
# and rpc()/rpc_id() are all rejected. The marker survives further bind()/unbind() chaining, so an
# invocation after additional binding is flagged too.
extends Node


func zero_arg() -> int:
	return 1


func test() -> void:
	var over_bound := Callable(self, "zero_arg").bind(1)
	over_bound.call()
	over_bound.callv([])
	over_bound.call_deferred()
	over_bound.rpc()
	over_bound.rpc_id(1)

	# Chaining more binds keeps the callable uninvocable, so the call is still flagged.
	Callable(self, "zero_arg").bind(1).unbind(2).call()
	Callable(self, "zero_arg").bindv([1, 2]).call()
