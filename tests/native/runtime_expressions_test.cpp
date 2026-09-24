/**************************************************************************/
/*  runtime_expressions_test.cpp                                          */
/*                                                                        */
/*  Expression, statement, control-flow and iterator execution.           */
/*  Each case compiles a script, runs one function on a live instance and */
/*  asserts the value it produced or the diagnostic it published, because */
/*  those two are the only things a program can observe about them.       */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "runtime_helpers.h"
#include "test_require.h"

#include "runtime_function_access.h"

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

using namespace barista_script;
using namespace barista_script::native_tests;

namespace {

/**
 * Compiles `p_source` and runs its `test()` on a fresh instance.
 *
 * Every case here is about what a running function produces, so the boilerplate of compiling,
 * attaching and calling is written once. A compilation that fails returns `nullptr` through
 * `r_script`'s validity, which the caller checks before reading the result.
 */
Variant run_test_function(const String &p_source, const String &p_path, Ref<BaristaScript> &r_script) {
	r_script = compile_script(p_source, p_path);
	if (r_script.is_null() || !r_script->_can_instantiate()) {
		return Variant();
	}
	Ref<RefCounted> owner;
	owner.instantiate();
	owner->set_script(r_script);
	return owner->call("test");
}

} // namespace

TEST_SUITE("runtime_expressions") {
	TEST_CASE("a match picks the first branch whose pattern holds") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tmatch 2:\n"
				"\t\t1:\n"
				"\t\t\treturn \"one\"\n"
				"\t\t2:\n"
				"\t\t\treturn \"two\"\n"
				"\t\t_:\n"
				"\t\t\treturn \"other\"\n"
				"\treturn \"fell through\"\n",
				"res://runtime_expressions/match_literal.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(result == Variant("two"));
	}

	TEST_CASE("a match with no matching branch and no wildcard falls through") {
		// The documented behaviour (docs/GRAMMAR.md): a `match` is a chain of conditionals with no
		// implicit default, so an unmatched subject runs nothing. This is asserted rather than left
		// to chance, because "nothing happened" and "something undefined happened" look alike.
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		const Variant result = run_test_function(
				"func test():\n"
				"\tvar taken = \"none\"\n"
				"\tmatch 9:\n"
				"\t\t1:\n"
				"\t\t\ttaken = \"one\"\n"
				"\t\t2:\n"
				"\t\t\ttaken = \"two\"\n"
				"\treturn taken\n",
				"res://runtime_expressions/match_no_arm.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(result == Variant("none"));
		CHECK_MESSAGE(errors.errors().is_empty(), readable(errors.joined()));
	}

	TEST_CASE("a match whose only branch is a wildcard always matches") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tmatch 41:\n"
				"\t\t_:\n"
				"\t\t\treturn 42\n"
				"\treturn 0\n",
				"res://runtime_expressions/match_wildcard.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(result == Variant(42));
	}

	TEST_CASE("a match binds, tests arrays and dictionaries, and honours a guard") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func classify(value):\n"
				"\tmatch value:\n"
				"\t\t[1, var second]:\n"
				"\t\t\treturn \"pair:\" + str(second)\n"
				"\t\t{\"kind\": \"note\"}:\n"
				"\t\t\treturn \"note\"\n"
				"\t\tvar other when typeof(other) == TYPE_INT and other == 7:\n"
				"\t\t\treturn \"seven\"\n"
				"\t\t_:\n"
				"\t\t\treturn \"other\"\n"
				"\treturn \"unreachable\"\n"
				"\n"
				"func test():\n"
				"\tvar seen = []\n"
				"\tseen.append(classify([1, 5]))\n"
				"\tseen.append(classify({\"kind\": \"note\"}))\n"
				"\tseen.append(classify(7))\n"
				"\tseen.append(classify(8))\n"
				"\tseen.append(classify([2, 5]))\n"
				"\treturn seen\n",
				"res://runtime_expressions/match_patterns.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Array seen = result;
		BS_TEST_REQUIRE(seen.size() == 5);
		CHECK(seen[0] == Variant("pair:5"));
		CHECK(seen[1] == Variant("note"));
		CHECK(seen[2] == Variant("seven"));
		CHECK(seen[3] == Variant("other"));
		CHECK(seen[4] == Variant("other"));
	}

	TEST_CASE("a match treats a String and a StringName alike") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tmatch &\"abc\":\n"
				"\t\t\"abc\":\n"
				"\t\t\treturn true\n"
				"\t\t_:\n"
				"\t\t\treturn false\n"
				"\treturn false\n",
				"res://runtime_expressions/match_stringname.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(result == Variant(true));
	}

	TEST_CASE("a tagged-union case pattern is refused by name until its family lands") {
		// The frozen refusal is the contract: a pattern the runtime cannot test must stop the
		// compilation, never silently fail to match and hand control to a later branch.
		const Ref<BaristaScript> script = compile_script(
				"enum Message:\n"
				"\tcase Move(x: int)\n"
				"\n"
				"func test():\n"
				"\tmatch Message.Move(1):\n"
				"\t\tMessage.Move(var x):\n"
				"\t\t\treturn x\n"
				"\treturn 0\n",
				"res://runtime_expressions/match_case.barista");
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_FALSE(script->_can_instantiate());
		CHECK_FALSE(script->get_compile_error().is_empty());
	}

	TEST_CASE("an array literal produces a fresh, writable array each time") {
		// The analyzer folds `[]` so a `const` can hold it, and it seals every folded container.
		// Sharing that one sealed instance between evaluations would alias two locals and refuse
		// every write; a literal must therefore be rebuilt, not pooled.
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tvar first := []\n"
				"\tvar second := []\n"
				"\tfirst.append(1)\n"
				"\treturn [first.size(), second.size(), first.is_read_only()]\n",
				"res://runtime_expressions/fresh_array.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Array sizes = result;
		BS_TEST_REQUIRE(sizes.size() == 3);
		CHECK(sizes[0] == Variant(1));
		CHECK(sizes[1] == Variant(0));
		CHECK(sizes[2] == Variant(false));
	}

	TEST_CASE("a declared constant container stays sealed") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"const NUMBERS: Array = [0]\n"
				"\n"
				"func test():\n"
				"\treturn NUMBERS.is_read_only()\n",
				"res://runtime_expressions/sealed_constant.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(result == Variant(true));
	}

	TEST_CASE("an unassigned local reads back as null, not as a reused value") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tif true:\n"
				"\t\t@warning_ignore(\"unused_variable\")\n"
				"\t\tvar first = 1\n"
				"\tif true:\n"
				"\t\tvar second\n"
				"\t\t@warning_ignore(\"unassigned_variable\")\n"
				"\t\treturn second == null\n"
				"\treturn false\n",
				"res://runtime_expressions/uninitialized_local.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(result == Variant(true));
	}

	TEST_CASE("a loop body's locals stop referencing their values between iterations") {
		// A slot that keeps the last iteration's object keeps that object alive, which
		// `get_reference_count()` reports. The count must not grow with the iteration count.
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tvar shared := RefCounted.new()\n"
				"\tvar counts := []\n"
				"\tfor _index in range(3):\n"
				"\t\tcounts.append(shared.get_reference_count())\n"
				"\t\t@warning_ignore(\"unused_variable\")\n"
				"\t\tvar held := shared\n"
				"\t\tcontinue\n"
				"\treturn counts\n",
				"res://runtime_expressions/loop_locals.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Array counts = result;
		BS_TEST_REQUIRE(counts.size() == 3);
		CHECK(counts[0] == counts[1]);
		CHECK(counts[1] == counts[2]);
	}

	TEST_CASE("a range loop takes non-integer bounds and its boundaries hold") {
		// The empty range and the single-element range are the two ends the loop form has to get
		// right; a float bound is converted, because the integer range is the loop's contract.
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func collect(from, to, step) -> Array:\n"
				"\tvar seen := []\n"
				"\tfor value in range(from, to, step):\n"
				"\t\tseen.append(value)\n"
				"\treturn seen\n"
				"\n"
				"func test():\n"
				"\treturn [collect(0, 0, 1), collect(0, 1, 1), collect(0.2, 3.7, 1), collect(3, 0, -1)]\n",
				"res://runtime_expressions/range_bounds.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Array runs = result;
		BS_TEST_REQUIRE(runs.size() == 4);
		CHECK(((Array)runs[0]).is_empty());
		CHECK(((Array)runs[1]).size() == 1);
		CHECK(((Array)runs[1])[0] == Variant(0));
		CHECK(((Array)runs[2]).size() == 3);
		CHECK(((Array)runs[2])[0] == Variant(0));
		CHECK(((Array)runs[2])[2] == Variant(2));
		CHECK(((Array)runs[3]).size() == 3);
		CHECK(((Array)runs[3])[0] == Variant(3));
	}

	TEST_CASE("a range loop yields integers whatever its bounds were written as") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tfor value in range(1.2, 5.2, 2.2):\n"
				"\t\tif typeof(value) != TYPE_INT:\n"
				"\t\t\treturn false\n"
				"\treturn true\n",
				"res://runtime_expressions/range_int_iterator.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(result == Variant(true));
	}

	TEST_CASE("a range whose span does not fit an integer is refused, not overflowed") {
		// `to - from` here is larger than any `int64_t`, so computing the element count signed would
		// be undefined behaviour on the way to the refusal the caller is owed.
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"func test():\n"
				"\tvar low: int = -9223372036854775807 - 1\n"
				"\tvar high: int = 9223372036854775807\n"
				"\treturn range(low, high)\n",
				"res://runtime_expressions/range_span_overflow.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK_MESSAGE(errors.has_error_containing("Range too big."), readable(errors.joined()));
	}

	TEST_CASE("a range loop whose advance leaves the integer range still ends") {
		// The step past the last element runs off the end of `int64_t`. A wrapped counter compares as
		// still inside the range, so without noticing the wrap this loop would never stop.
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tvar seen := 0\n"
				"\tvar huge: int = 9223372036854775807\n"
				"\tfor value in range(0, huge, huge):\n"
				"\t\tseen += 1\n"
				"\treturn seen\n",
				"res://runtime_expressions/range_advance_overflow.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(result == Variant(1));
	}

	TEST_CASE("breaking out of a nested block still releases that block's locals") {
		// The clear a block writes after its last statement is exactly what a `break` jumps over, so
		// the jump has to clear every scope it is leaving. Without that, the nested `if`'s local keeps
		// the object alive for the rest of the frame.
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tvar shared := RefCounted.new()\n"
				"\tvar before := shared.get_reference_count()\n"
				"\twhile true:\n"
				"\t\tif true:\n"
				"\t\t\t@warning_ignore(\"unused_variable\")\n"
				"\t\t\tvar held := shared\n"
				"\t\t\tbreak\n"
				"\treturn [before, shared.get_reference_count()]\n",
				"res://runtime_expressions/break_nested_locals.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Array counts = result;
		BS_TEST_REQUIRE(counts.size() == 2);
		CHECK(counts[0] == counts[1]);
	}

	TEST_CASE("a match gives up its subject when the statement ends") {
		// The subject is copied into a hidden local so no pattern re-evaluates it. That local is the
		// only thing in the frame still holding the value once the statement is over.
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tvar shared := RefCounted.new()\n"
				"\tvar before := shared.get_reference_count()\n"
				"\tmatch shared:\n"
				"\t\t_:\n"
				"\t\t\tpass\n"
				"\treturn [before, shared.get_reference_count()]\n",
				"res://runtime_expressions/match_releases_subject.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Array counts = result;
		BS_TEST_REQUIRE(counts.size() == 2);
		CHECK(counts[0] == counts[1]);
	}

	TEST_CASE("a range loop with a zero step is refused rather than run forever") {
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"func test():\n"
				"\tfor value in range(0, 3, 0):\n"
				"\t\tprint(value)\n",
				"res://runtime_expressions/range_zero_step.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK_MESSAGE(errors.has_error_containing("step cannot be zero"), readable(errors.joined()));
	}

	TEST_CASE("iterating a value that is not a sequence reports and does not hang") {
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"func test():\n"
				"\tvar subject = null\n"
				"\tfor element in subject:\n"
				"\t\tprint(element)\n",
				"res://runtime_expressions/iterate_null.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK_MESSAGE(errors.has_error_containing("Cannot iterate"), readable(errors.joined()));
	}

	TEST_CASE("an empty sequence runs the loop body no times") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tvar runs := 0\n"
				"\tfor element in []:\n"
				"\t\truns += 1\n"
				"\treturn runs\n",
				"res://runtime_expressions/iterate_empty.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(result == Variant(0));
	}

	TEST_CASE("a native class answers new and its static methods") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tvar made := Object.new()\n"
				"\tvar name := made.get_class()\n"
				"\tmade.free()\n"
				"\treturn [name, FileAccess.file_exists(\"res://definitely_absent.txt\")]\n",
				"res://runtime_expressions/native_static.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Array parts = result;
		BS_TEST_REQUIRE(parts.size() == 2);
		CHECK(parts[0] == Variant("Object"));
		CHECK(parts[1] == Variant(false));
	}

	TEST_CASE("instantiating an abstract engine class reports instead of returning nothing") {
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"func test():\n"
				"\treturn Node3DGizmo.new()\n",
				"res://runtime_expressions/native_abstract.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		if (script->_can_instantiate()) {
			CHECK_MESSAGE(errors.has_error_containing("Cannot instantiate"), readable(errors.joined()));
		}
	}

	TEST_CASE("a builtin type answers its static methods") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\treturn Color.from_rgba8(255, 0, 0, 255).r8\n",
				"res://runtime_expressions/builtin_static.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(result == Variant(255));
	}

	TEST_CASE("the language's own utilities run, including at their boundaries") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\treturn [len(\"abc\"), len([]), len({1: 2}), char(65), ord(\"A\"), type_exists(&\"Node\")]\n",
				"res://runtime_expressions/language_utilities.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Array values = result;
		BS_TEST_REQUIRE(values.size() == 6);
		CHECK(values[0] == Variant(3));
		CHECK(values[1] == Variant(0));
		CHECK(values[2] == Variant(1));
		CHECK(values[3] == Variant("A"));
		CHECK(values[4] == Variant(65));
		CHECK(values[5] == Variant(true));
	}

	TEST_CASE("a language utility given a value it cannot serve says which value") {
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"func test():\n"
				"\tvar subject = Color()\n"
				"\treturn len(subject)\n",
				"res://runtime_expressions/language_utility_error.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK_MESSAGE(errors.has_error_containing("can't provide a length"), readable(errors.joined()));
	}

	TEST_CASE("an engine utility with a fixed signature runs and converts its arguments") {
		// `typeof` and `floor` are not variadic, so each argument has to be materialized in the
		// carrier the signature declares before the engine's pointer call can be made at all.
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\treturn [typeof(1) == TYPE_INT, floor(2.7), floor(3), var_to_str(1)]\n",
				"res://runtime_expressions/engine_utilities.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Array values = result;
		BS_TEST_REQUIRE(values.size() == 4);
		CHECK(values[0] == Variant(true));
		CHECK(values[1] == Variant(2.0));
		// `floor` is declared to return a Variant and keeps the carrier it was given, so an
		// integer argument comes back an integer rather than being widened on the way out.
		CHECK(values[2] == Variant(3));
		CHECK(values[3] == Variant("1"));
	}

	TEST_CASE("dividing an integer by zero names the divisor, not the operand types") {
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"func test():\n"
				"\tvar zero: int = 0\n"
				"\treturn 1 / zero\n",
				"res://runtime_expressions/divide_by_zero.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK_MESSAGE(errors.has_error_containing(R"(The "/" operator on "int" has a divisor of zero.)"),
				readable(errors.joined()));
	}

	TEST_CASE("an operator with operands it has no meaning for names both of them") {
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"func test():\n"
				"\tvar left = \"text\"\n"
				"\tvar right = []\n"
				"\treturn left + right\n",
				"res://runtime_expressions/bad_operands.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK_MESSAGE(errors.has_error_containing("Invalid operands 'String' and 'Array' in operator '+'."),
				readable(errors.joined()));
	}

	TEST_CASE("reading past the end of an array reports a range fault, not an unknown key") {
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"func test():\n"
				"\tvar numbers := [1, 2, 3]\n"
				"\treturn numbers[4]\n",
				"res://runtime_expressions/array_out_of_range.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK_MESSAGE(errors.has_error_containing("Out of bounds get index '4' (on base: 'Array')"),
				readable(errors.joined()));
	}

	TEST_CASE("reading a property off null names the property and the base") {
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"func test():\n"
				"\tvar subject: String? = null\n"
				"\treturn subject.length\n",
				"res://runtime_expressions/property_on_null.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK_MESSAGE(errors.has_error_containing("Invalid access to property or key 'length' on a base object of type 'Nil'."),
				readable(errors.joined()));
	}

	TEST_CASE("writing into a sealed container reports the seal, not the key") {
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"const TABLE := {0: [0]}\n"
				"\n"
				"func test():\n"
				"\tvar row := TABLE[0]\n"
				"\tvar key: int = 0\n"
				"\trow[key] = 1\n",
				"res://runtime_expressions/sealed_write.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK_MESSAGE(errors.has_error_containing("Invalid assignment on read-only value (on base: 'Array')."),
				readable(errors.joined()));
	}

	TEST_CASE("asking for the result of a method that returns nothing is reported") {
		Ref<BaristaScript> script;
		const RuntimeErrorScope errors;
		run_test_function(
				"func test():\n"
				"\tvar subject = []\n"
				"\treturn subject.reverse()\n",
				"res://runtime_expressions/void_return.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK_MESSAGE(errors.has_error_containing(R"(Trying to get a return value of a method that returns "void")"),
				readable(errors.joined()));
	}

	TEST_CASE("a subscript, an attribute and a ternary evaluate to their operands") {
		Ref<BaristaScript> script;
		const Variant result = run_test_function(
				"func test():\n"
				"\tvar table := {\"point\": Vector2(3, 4)}\n"
				"\tvar point = table[\"point\"]\n"
				"\tvar length = point.length()\n"
				"\treturn [point.x, length, 1 if length > 4 else 0]\n",
				"res://runtime_expressions/subscript_attribute_ternary.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		const Array values = result;
		BS_TEST_REQUIRE(values.size() == 3);
		CHECK(values[0] == Variant(3.0));
		CHECK(values[1] == Variant(5.0));
		CHECK(values[2] == Variant(1));
	}

	TEST_CASE("assert lets a true condition through and stops a false one") {
		Ref<BaristaScript> script;
		const Variant passing = run_test_function(
				"func test():\n"
				"\tassert(1 + 1 == 2)\n"
				"\treturn \"continued\"\n",
				"res://runtime_expressions/assert_true.barista", script);
		BS_TEST_REQUIRE(script.is_valid());
		CHECK_MESSAGE(script->get_compile_error().is_empty(), readable(script->get_compile_error()));
		CHECK(passing == Variant("continued"));

		Ref<BaristaScript> failing_script;
		const RuntimeErrorScope errors;
		const Variant failing = run_test_function(
				"func test():\n"
				"\tassert(1 + 1 == 3)\n"
				"\treturn \"continued\"\n",
				"res://runtime_expressions/assert_false.barista", failing_script);
		BS_TEST_REQUIRE(failing_script.is_valid());
		CHECK_MESSAGE(failing_script->get_compile_error().is_empty(), readable(failing_script->get_compile_error()));
		// A failed assertion abandons the frame, so the statement after it never runs.
		CHECK(failing != Variant("continued"));
		CHECK_FALSE(errors.errors().is_empty());
	}

	TEST_CASE("compiling the same source twice produces the same code") {
		// Idempotency is what makes a compiled function comparable at all: a pooled temporary or a
		// constant index that depended on iteration order would make two compilations of one source
		// disagree, and no diff of generated code would mean anything.
		const String source =
				"func test():\n"
				"\tvar total := 0\n"
				"\tfor value in range(4):\n"
				"\t\tmatch value:\n"
				"\t\t\t0:\n"
				"\t\t\t\tcontinue\n"
				"\t\t\tvar other:\n"
				"\t\t\t\ttotal += other\n"
				"\treturn total\n";
		Ref<BaristaScript> first;
		Ref<BaristaScript> second;
		const Variant first_result = run_test_function(source, "res://runtime_expressions/idempotent_a.barista", first);
		const Variant second_result = run_test_function(source, "res://runtime_expressions/idempotent_b.barista", second);
		BS_TEST_REQUIRE(first.is_valid() && second.is_valid());
		CHECK_MESSAGE(first->get_compile_error().is_empty(), readable(first->get_compile_error()));
		CHECK(first_result == Variant(6));
		CHECK(first_result == second_result);

		BSFunction *first_function = first->find_function(SNAME("test"));
		BSFunction *second_function = second->find_function(SNAME("test"));
		BS_TEST_REQUIRE(first_function != nullptr && second_function != nullptr);
		CHECK(BSFunctionTestAccess::code_of(first_function) == BSFunctionTestAccess::code_of(second_function));
		CHECK(BSFunctionTestAccess::constant_count(first_function) ==
				BSFunctionTestAccess::constant_count(second_function));
	}
}
