# Parsing a subscript index stops before a trailing `?` so a use-site nullable type-argument
# marker is captured rather than swallowed as the always-invalid `?` infix. Casts and ternaries
# bind more tightly than `?`, so ordinary indexing with `as`/`if`-`else` keeps parsing here.
func test():
	var arr := [10, 20, 30]
	var i := 1
	print(arr[i as int])
	print(arr[0 if true else 2])
	print(arr[(1 + 1)])

	var dict := { "a": 1, "b": 2 }
	print(dict["a" if false else "b"])
