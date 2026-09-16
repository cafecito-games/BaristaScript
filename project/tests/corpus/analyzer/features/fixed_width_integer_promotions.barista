tuple Bounds(low: uint, high: ulong)

enum Reading:
	Sample(value: uint)

func take_uint(value: uint) -> uint:
	return value

signal reported(total: ulong)

func test():
	var i: int = 1
	var u: uint = 2U
	var l: long = 3L
	var ul: ulong = 4UL

	# Every successful ordered promotion row is pinned by an annotated destination. The rejected rows
	# live in fixed_width_integer_unsafe_mix.fs.
	var int_int: int = i + i
	var uint_uint: uint = u + u
	var same_long: long = l + l
	var ulong_ulong: ulong = ul + ul
	var int_long: long = i + l
	var long_int: long = l + i
	var uint_ulong: ulong = u + ul
	var ulong_uint: ulong = ul + u

	# uint widens into ulong without a conversion, because every uint value is a ulong value.
	var widened: ulong = u

	# Every 32-bit signed integer is exactly representable as a float, so it reaches the floating side
	# without a cast. The widths the analyzer refuses to promote are in
	# fixed_width_integer_unsafe_mix.fs.
	var int_float: float = i

	# A constant may enter a narrower slot when its exact value fits.
	var narrowed_constant: uint = 4000000000UL
	var exact_float: float = 4L

	# Every typed boundary uses the same rule, so the same constant crosses each of them.
	var elements: Array[uint] = [4000000000UL]
	var passed: uint = take_uint(4000000000UL)
	reported.emit(4UL)
	var sample = Reading.Sample(4000000000UL)
	var bounds = Bounds(4000000000UL, 4UL)

	print(int_int)
	print(uint_uint)
	print(same_long)
	print(ulong_ulong)
	print(int_long)
	print(long_int)
	print(uint_ulong)
	print(ulong_uint)
	print(widened)
	print(int_float)
	print(narrowed_constant)
	print(exact_float)
	print(elements[0])
	print(passed)
	print(sample)
	print(bounds)
