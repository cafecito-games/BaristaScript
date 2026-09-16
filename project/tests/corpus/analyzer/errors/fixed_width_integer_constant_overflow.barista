func test():
	# Each pair agrees on one type, so the operation is checked at that type's range instead of
	# wrapping on the carrier that happens to hold the operands.
	var unsigned_sum = 4000000000U + 4000000000U
	var unsigned_difference = 0U - 1U
	var signed_product = 9223372036854775807L * 2L
	var negated_minimum = -(-9223372036854775807L - 1L)
	print(unsigned_sum, unsigned_difference, signed_product, negated_minimum)
