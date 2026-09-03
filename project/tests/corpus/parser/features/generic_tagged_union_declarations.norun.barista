# A named tagged union may declare type parameters, with or without a bound, between its
# name and the `:`. Referring to those parameters from a payload field type needs the
# analyzer to bring them into scope, which lands with use-site specialization, so the
# payload types here stay concrete and only the declaration shape is exercised.
enum Result[T, E: Resource]:
	Ok(value: int)
	Err(error: String)


enum Option[T]:
	None
	Some(value: int)


enum Pair[K, V,]:
	Both(first: int, second: String)
