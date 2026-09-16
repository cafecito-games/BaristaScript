# A file-level tagged union may name a type nested inside a trait-conforming class that itself
# declares a member of the union's type. Resolving the union must not force the holder's whole
# interface, which would re-enter the union while it is still being resolved.
func describe(pick: UnionPayloadNestedPick) -> String:
	var described: String = ""
	match pick:
		UnionPayloadNestedPick.Inner(inner):
			described = "inner:" + str(inner.value)
		UnionPayloadNestedPick.Tier(tier):
			described = "tier:" + str(tier)
		UnionPayloadNestedPick.Text(text):
			described = "text:" + text
	return described

func test():
	var holder := UnionPayloadNestedHolder.new()
	var inner := UnionPayloadNestedHolder.Inner.new()
	inner.value = 7
	holder.pick = UnionPayloadNestedPick.Inner(inner)
	print(holder.tag())
	var current := holder.pick
	if current != null:
		print(describe(current))
	print(describe(UnionPayloadNestedPick.Tier(UnionPayloadNestedHolder.Tier.HIGH)))
	print(describe(UnionPayloadNestedPick.Text("hi")))
