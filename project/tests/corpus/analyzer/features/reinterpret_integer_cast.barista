func test():
	# `as!` reinterprets a value's bit pattern between equal-width integer types without a range check.
	# Constant folding produces the same pattern the runtime opcode does, so these fold to exact values.
	print(-1 as! ulong)
	print(0xFF00000000000000UL as! long)
	print(4294967295U as! int)

	# An unpinned integer counts as 64-bit, so reaching a 32-bit target means narrowing first with a
	# checked `as int`, then reinterpreting at the now-explicit 32-bit width.
	print((-1 as int) as! uint)

	# An unpinned literal reinterprets directly into a 64-bit target, its natural width.
	print(255 as! ulong)

	# The sign-bit byte assembled with an unsigned shift, then reinterpreted as signed. The shift count
	# shares the left operand's carrier, and `as!` binds looser than `<<`, so no inner parentheses are
	# needed around the shift.
	print((255 as ulong) << 56UL as! long)
