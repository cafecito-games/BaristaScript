func test():
	var typed_lookup: Dictionary[CustomHashKey, String] = {
		CustomHashKey.new(1234, "abc"): "Hello",
		CustomHashKey.new(5678, "def"): "Hola",
	}

	print(typed_lookup[CustomHashKey.new(1234, "abc")])
	print(typed_lookup.has(CustomHashKey.new(5678, "def")))
	print(typed_lookup.get(CustomHashKey.new(0, "missing"), "missing"))
	print(CustomHashKey.new(1, "same") == CustomHashKey.new(1, "same"))
	print(CustomHashKey.new(1, "same") != CustomHashKey.new(2, "same"))

	var collisions: Dictionary[CustomHashCollisionKey, String] = {
		CustomHashCollisionKey.new(1, "one"): "one",
		CustomHashCollisionKey.new(2, "two"): "two",
	}

	print(collisions[CustomHashCollisionKey.new(1, "one")])
	print(collisions[CustomHashCollisionKey.new(2, "two")])

	var identity_key := RefCounted.new()
	var other_identity_key := RefCounted.new()
	var identity_lookup: Dictionary[RefCounted, String] = {
		identity_key: "same",
	}

	print(identity_lookup.has(identity_key))
	print(identity_lookup.has(other_identity_key))
