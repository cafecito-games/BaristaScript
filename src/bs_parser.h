/**************************************************************************/
/*  bs_parser.h                                                           */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include <climits>

/**
 * Hard fork of Foundry's parser (`modules/foundry_script/fs_parser.h` @
 * c9d5e35e9c7f5e481dc0639d5af639cabaaea7b6), renamed `FS*` -> `BS*` and reached through the
 * platform seam instead of Godot's `core/` headers.
 *
 * The port's divergences from upstream are recorded next to the code that carries them. In summary:
 *
 *   - D1 (docs/GRAMMAR.md section 0.2, section 7.1) deletes `NumericType` and every descriptor hung
 *     on it. A slot's `Variant` carrier is the whole of its numeric type, `Number` is the closed
 *     union of `int` and `float`, and `uint`/`ulong`/`long` are reserved spellings the type
 *     positions reject rather than accept as user type names.
 *   - Upstream's `FSLanguage`/`FSAnalyzer` coupling is reduced to two seams: the interned-string
 *     table, which now lives on `BaristaScriptLanguage`, and `BSParserHost`, the narrow interface
 *     M3 implements for the namespace-conformance index and the bootstrap-path check.
 *   - The parse-tree halves of the cache (`FSParserRef`, `FSCache::get_parser`) do not exist yet, so
 *     `get_depended_parser_for()`/`get_depended_parsers()` are not ported; M3 reinstates them
 *     against `BSCache`.
 */

#include "bs_cache.h"
#include "bs_platform.h"
#include "bs_tokenizer.h"

#ifdef DEBUG_ENABLED
#include "bs_warning.h"
#endif // DEBUG_ENABLED

// `PropertyInfo` can only name an instance type, so a `Type[T]` slot spells its class-handle layer into
// the property class name. These two functions are the only place that marker grammar is written or
// read; see `BSParser::DataType::to_property_info` and `BSAnalyzer::type_from_property`.
namespace barista_script {

/**
 * The narrow seam between the parser and everything the analyzer milestone owns.
 *
 * Upstream reaches `FSLanguage::get_singleton()` and `FSAnalyzer` directly from five sites in
 * `fs_parser.cpp` (@ c9d5e35: :1206, :1229, :3000, :3369, :3811). Three of them want interned
 * names, which now live on `BaristaScriptLanguage` (`get_interned_strings()`); the other two want a
 * project-wide index and a bootstrap-root check that only M3 can answer. Rather than let the parser
 * name the analyzer -- which would make the port depend on a class that does not exist -- both are
 * asked of this interface.
 *
 * The default answers are the ones a parser with no host must give, and each is the safe direction:
 * an unknown namespace declares no conformance files, so nothing is invented; and with no bootstrap
 * root configured every path is inside it, so nothing is silently filtered out of a dependency list.
 * M3 replaces the instance; it never edits these defaults.
 */
class BSParserHost {
public:
	/** The files declaring retroactive conformances in `p_namespace`. Empty when nothing indexes them. */
	virtual Vector<String> get_conformance_files_in_namespace(const String &p_namespace) const { return Vector<String>(); }

	/**
	 * Whether `p_path` lies inside the dependency root a build-task bootstrap compiles in. Upstream
	 * spells this `FSAnalyzer::is_bootstrap_path_allowed()` (fs_parser.cpp:1229 @ c9d5e35).
	 */
	virtual bool is_bootstrap_path_allowed(const String &p_path) const { return true; }

	virtual ~BSParserHost() {}

	/** The host every parser asks. Never null: an unset host is the default-answering instance. */
	static BSParserHost *get_singleton();
	/** Installs a host, returning the previous one so a caller can restore it. */
	static BSParserHost *set_singleton(BSParserHost *p_host);
};

String bs_encode_type_handle_property_class_name(const StringName &p_represented_class);
bool bs_decode_type_handle_property_class_name(const String &p_class_name, String &r_represented_class);

class BSParser {
	struct AnnotationInfo;

public:
	// Forward-declare all parser nodes, to avoid ordering issues.
	struct AnnotationDeclarationNode;
	struct AnnotationNode;
	struct ArrayNode;
	struct AssertNode;
	struct AssignableNode;
	struct AssignmentNode;
	struct AwaitNode;
	struct BinaryOpNode;
	struct BreakNode;
	struct BreakpointNode;
	struct CallNode;
	struct CastNode;
	struct ClassNode;
	struct ConformanceNode;
	struct ConstantNode;
	struct ContinueNode;
	struct DictionaryNode;
	struct EnumNode;
	struct ExpressionNode;
	struct ForNode;
	struct FunctionNode;
	struct GetNodeNode;
	struct IdentifierNode;
	struct IfNode;
	struct LambdaNode;
	struct LiteralNode;
	struct MatchNode;
	struct MatchBranchNode;
	struct ParameterNode;
	struct PassNode;
	struct PatternNode;
	struct PreloadNode;
	struct ReturnNode;
	struct SelfNode;
	struct SignalNode;
	struct SubscriptNode;
	struct SuiteNode;
	struct TernaryOpNode;
	struct TraitNode;
	struct TupleLiteralNode;
	struct TupleNode;
	struct TypeAliasNode;
	struct TypeNode;
	struct TypeParameterNode;
	struct TypeTestNode;
	struct UnaryOpNode;
	struct VariableNode;
	struct VariableDestructureNode;
	struct WhileNode;

	class DataType {
	public:
		Vector<DataType> container_element_types;

		enum Kind {
			BUILTIN,
			NATIVE,
			SCRIPT,
			CLASS, // BaristaScript.
			ENUM, // Enumeration.
			TUPLE, // Fixed-size immutable aggregate, named (`tuple Vec2(...)`) or unnamed (`(int, String)`).
			UNION, // Static set of alternative types, e.g. `int | uint`. Always holds two or more canonical members.
			TYPE_PARAMETER, // Generic type parameter, e.g. `T` in `class Box[T]`.
			VARIANT, // Can be any type.
			RESOLVING, // Currently resolving.
			UNRESOLVED,
		};
		Kind kind = UNRESOLVED;

		// Where a TYPE_PARAMETER was declared, used to disambiguate parameters that share a name across scopes.
		enum TypeParameterScope {
			TYPE_PARAMETER_NONE,
			TYPE_PARAMETER_CLASS,
			TYPE_PARAMETER_ENUM,
			TYPE_PARAMETER_METHOD,
		};

		enum TypeSource {
			UNDETECTED, // Can be any type.
			INFERRED, // Has inferred type, but still dynamic.
			ANNOTATED_EXPLICIT, // Has a specific type annotated.
			ANNOTATED_INFERRED, // Has a static type but comes from the assigned value.
		};
		TypeSource type_source = UNDETECTED;

		bool is_constant = false;
		bool is_read_only = false;
		bool is_meta_type = false;
		bool is_type_handle_annotation = false; // `Type[T]`: keep the represented type as a class-handle expectation.
		bool is_pseudo_type = false; // For global names that can't be used standalone.
		bool is_coroutine = false; // For function calls.
		bool is_nullable = false;

		// D1: the carrier is the whole of a slot's numeric type. Upstream hangs a width/signedness
		// descriptor here (fs_parser.h:165-171 @ c9d5e35) because its four integer spellings do not map
		// one-to-one onto `Variant::Type`; BaristaScript has one integer type on `Variant::INT`, so
		// there is nothing left to describe and nothing that can be erased by a carrier-only boundary.
		Variant::Type builtin_type = Variant::NIL;
		// True when this type replaced a `Self` type parameter while a declaration was lowered for
		// execution. A static call frame is handed the exact class handle it was invoked through, and
		// only positions carrying this flag may be re-bound to it. Without it, a `Self` position is
		// indistinguishable from a position the author spelled out as the declaring class or the
		// conformance target, and the receiver information is unrecoverable. Provenance only: it takes
		// no part in type identity.
		bool is_substituted_self = false;
		// True when this `Self` type parameter is a call site's receiver contract: an instance call
		// resolves it against the receiver expression's run-time class rather than against the calling
		// frame's own receiver. A `Self` that reached a signature any other way -- through a carrier's
		// element type, a class type argument, or an explicit method type argument written in the
		// caller -- still denotes the calling frame's receiver. Provenance only: it takes no part in
		// type identity.
		bool is_receiver_self_contract = false;
		// True when this type parameter reached a use site through a raw generic receiver -- a generic
		// class or trait named without type arguments -- which therefore never bound it. The parameter
		// is erased to a Variant at run time and no argument decides what it holds, so a value typed by
		// it is exactly as unchecked at a typed boundary as a Variant value is, and the unsafe-boundary
		// checks route it the same way. Provenance only: it takes no part in type identity.
		bool is_raw_generic_projection = false;
		// The declaration whose parameter list a raw generic projection came out of. A type parameter's
		// identity is only its name, scope, and ordinal, so two declarations that spell their parameters
		// alike produce indistinguishable handles; the projection carries its owner so the boundary check
		// can tell a raw receiver's parameter from an identically named one the use site already binds.
		// Provenance only: it takes no part in type identity.
		ClassNode *raw_generic_projection_owner = nullptr;
		// True when this Variant replaced a type parameter that generic inference could not solve, so
		// the rest of the call stays type-checkable after the inference error. Nothing at the call site
		// expects a Variant there; a diagnostic that names what a position expects must not present
		// this recovery type as the author's expectation. Provenance only: it takes no part in type
		// identity.
		bool is_unresolved_inference_fallback = false;
		StringName native_type;
		StringName enum_type; // Enum name or the value name in an enum.
		Ref<Script> script_type;
		String script_path;
		ClassNode *class_type = nullptr;

		MethodInfo method_info; // For callable/signals.
		bool has_method_signature = false; // Whether method_info participates in callable/signal type checks.
		bool has_explicit_method_signature = false; // Whether the signature came from a Callable/Signal type annotation.
		bool signature_is_async = false; // Whether the callable type was written as AsyncCallable rather than Callable.
		Vector<DataType> method_parameter_types; // Rich BaristaScript signature preserving metadata MethodInfo cannot store.
		Vector<DataType> method_return_type; // Empty for signals, one element for callables.
		// Rest ("...") tail of a variadic BaristaScript signature: empty, or exactly one resolved Array
		// DataType. `METHOD_FLAG_VARARG` stays the arity bit. The slot is filled only when the rest
		// array narrows its element below Variant, so an external/native vararg and a gradual
		// `...args: Array` / `Array[Variant]` tail alike leave it empty and keep their current behavior.
		Vector<DataType> method_rest_parameter_type;
		bool method_return_is_erased_container = false; // Callable return needs typed-container conversion after call/callv.
		Vector<int> method_extra_allowed_argument_counts; // Extra exact arities not expressible by default arguments, for transformed Callables.
		int method_unbound_argument_count = 0; // Trailing arguments ignored by transformed Callables.
		bool callable_is_over_bound = false; // Set when bind()/bindv() bound more arguments than a fixed-arity target accepts, making any invocation fail.
		HashMap<StringName, int64_t> enum_values; // For enums.

		// Payload fields of one tagged-union case, in declaration order. Field names are an
		// analyzer-only convenience: the runtime representation is positional.
		struct EnumCasePayload {
			Vector<StringName> field_names;
			Vector<DataType> field_types;
		};

		// For ENUM kind. A tagged union is an enum where at least one case declares a payload; its
		// values are read-only Arrays (`[tag, payload...]`) rather than ints, so `builtin_type` is
		// ARRAY for a value and DICTIONARY for the meta type. `enum_values` still maps each case to
		// its ordinal tag.
		bool is_tagged_union = false;
		HashMap<StringName, EnumCasePayload> enum_case_payloads; // Only cases that declare a payload.
		StringName enum_case_name; // Set on a case-constructor pseudo-type, e.g. an un-called `Message.Move`.

		// For TUPLE kind. Element types live in `container_element_types`, so generic substitution
		// recurses through them for free.
		StringName tuple_name; // Empty for an unnamed (structural) tuple.
		Vector<StringName> tuple_field_names; // Parallel to the elements; an empty entry is a positional field.

		// For UNION kind. Alternatives of a static type set, already normalized: aliases expanded,
		// nested unions flattened, nullability hoisted onto the union itself, duplicates removed, and
		// order canonicalized so equality and diagnostics are deterministic. A set that normalizes to
		// a single alternative is not a union at all -- it collapses to that alternative -- so a UNION
		// always holds at least two members and no member is itself a UNION or nullable.
		Vector<DataType> union_members;

		// For TYPE_PARAMETER kind.
		StringName type_parameter_name;
		int type_parameter_index = -1; // Ordinal position in its declaring scope.
		TypeParameterScope type_parameter_scope = TYPE_PARAMETER_NONE;
		Vector<DataType> type_parameter_bound; // 0 or 1 element: optional upper bound, e.g. `[T: Resource]`.

		// Type arguments of a specialized type handle, e.g. the `int` in `Box[int]`. Empty for unspecialized types.
		Vector<DataType> type_arguments;

		_FORCE_INLINE_ bool is_type_parameter() const { return kind == TYPE_PARAMETER; }
		_FORCE_INLINE_ bool is_tuple() const { return kind == TUPLE; }
		_FORCE_INLINE_ bool is_union() const { return kind == UNION; }

		// Normalizes p_members into the type they denote: alias members are already expanded by the
		// caller, nested unions are flattened, a nullable member hoists nullability onto the result,
		// duplicates are removed, and the surviving members are ordered canonically. A single
		// surviving member is returned as itself (a one-member alias is indistinguishable from its
		// member, keeping its runtime typing and numeric width); two or more produce a UNION.
		// An empty member list yields an UNRESOLVED type; callers reject that at their own site.
		static DataType make_union(const Vector<DataType> &p_members);
		_FORCE_INLINE_ bool is_tagged_union_type() const { return kind == ENUM && is_tagged_union; }
		// Payload of the named case, or nullptr when the case is payload-less or unknown.
		_FORCE_INLINE_ const EnumCasePayload *get_enum_case_payload(const StringName &p_case_name) const {
			return enum_case_payloads.getptr(p_case_name);
		}
		// Index of the tuple field declared with p_name, or -1 when there is no such named field.
		int get_tuple_field_index(const StringName &p_name) const;
		_FORCE_INLINE_ bool has_type_arguments() const { return !type_arguments.is_empty(); }

		_FORCE_INLINE_ void set_type_argument(int p_index, const DataType &p_type) {
			ERR_FAIL_COND(p_index < 0);
			while (p_index >= type_arguments.size()) {
				type_arguments.push_back(get_variant_type());
			}
			type_arguments.write[p_index] = p_type;
		}

		_FORCE_INLINE_ void add_type_argument(const DataType &p_type) {
			set_type_argument(type_arguments.size(), p_type);
		}

		_FORCE_INLINE_ bool has_method_rest_parameter_type() const {
			return method_rest_parameter_type.size() == 1;
		}

		_FORCE_INLINE_ const DataType &get_method_rest_parameter_type() const {
			DEV_ASSERT(has_method_rest_parameter_type());
			return method_rest_parameter_type[0];
		}

		_FORCE_INLINE_ void set_method_rest_parameter_type(const DataType &p_type) {
			method_rest_parameter_type.clear();
			method_rest_parameter_type.push_back(p_type);
		}

		_FORCE_INLINE_ void clear_method_rest_parameter_type() {
			method_rest_parameter_type.clear();
		}

		// Returns a copy of p_type with every TYPE_PARAMETER replaced by its bound argument from p_bindings,
		// recursing through container elements, type arguments, and method signatures. Unbound parameters are left intact.
		//
		// p_bindings is keyed by type-parameter name and must hold the parameters of a single declaration scope, since a
		// name uniquely identifies a parameter within its own scope. For nested generics (e.g. a generic method on a
		// generic class), substitute outermost scope first so an inner parameter that shadows an outer name wins.
		//
		// p_mark_substituted_self records on every replacement of the `@Self` parameter that it came from
		// `Self`, at every nesting depth. Only declaration lowering asks for it: a call site that
		// substitutes `Self` against the receiver it can see has already produced the final type, and
		// marking it would invite a second, unrelated substitution at execution time.
		static DataType substitute(const DataType &p_type, const HashMap<StringName, DataType> &p_bindings,
				bool p_mark_substituted_self = false);

		_FORCE_INLINE_ bool is_set() const { return kind != RESOLVING && kind != UNRESOLVED; }
		_FORCE_INLINE_ bool is_resolving() const { return kind == RESOLVING; }
		_FORCE_INLINE_ bool has_no_type() const { return type_source == UNDETECTED; }
		_FORCE_INLINE_ bool is_variant() const { return kind == VARIANT || kind == RESOLVING || kind == UNRESOLVED; }
		_FORCE_INLINE_ bool is_hard_type() const { return type_source > INFERRED; }

		String to_string() const;
		// Like `to_string()`, but names width-only integer descriptors (`uint8`/`int16`/...) by their
		// stable diagnostic name instead of falling back to the carrier spelling. Diagnostics that
		// contrast two types must not render distinct widths identically; source-writing callers must
		// keep using `to_string()`.
		String to_string_diagnostic() const;
		// The script file that declares this type at its top level: a CLASS's declaring script, a
		// SCRIPT's own path, an empty string for every other kind. Containers, unions, and type
		// arguments are not recursed into.
		String declaring_script_path() const;
		// Two unnamed types declared in same-named files render identically, so a diagnostic that
		// contrasts them reads self-contradictorily ("cannot assign helper.fs ... expected helper.fs").
		// When the two rendered names are equal while the declaring files differ, this returns a
		// disambiguating clause (beginning with a single space) that names both declaring files; sites
		// append it to the base message with plain `+`. When the rendered names are equal and no file
		// clause applies, the sides can still differ only in their `Self` binding — a named composite
		// (`tuple Pair(index: int, owner: Self)`) renders its declared name without its slots, so a
		// declaration that reads `Self` contrasts identically against a value whose `Self` was already
		// substituted with a receiver's class ("should be Pair but is Pair"); that collision returns a
		// clause naming the side that keeps `Self` and the concrete type the other side carries in its
		// place. Returns an empty string when the names differ or neither clause can disambiguate.
		static String same_rendered_name_clause(const DataType &p_first, const String &p_first_subject, const DataType &p_second, const String &p_second_subject);
		_FORCE_INLINE_ String to_string_strict() const { return is_hard_type() ? to_string() : "Variant"; }
		PropertyInfo to_property_info(const String &p_name) const;

		_FORCE_INLINE_ static DataType get_variant_type() { // Default DataType for container elements.
			DataType datatype;
			datatype.kind = VARIANT;
			datatype.type_source = INFERRED;
			return datatype;
		}

		// D1 deletes upstream's `as_container_slot_type()` (fs_parser.h:365-379 @ c9d5e35). It existed
		// to strip a width that a carrier-only boundary had reconstructed rather than read from a
		// declaration, so that an inferred `Array[int]` stayed the same type as a declared one. With no
		// width to reconstruct, a container slot is its element type unchanged.

		_FORCE_INLINE_ void set_container_element_type(int p_index, const DataType &p_type) {
			ERR_FAIL_COND(p_index < 0);
			while (p_index >= container_element_types.size()) {
				container_element_types.push_back(get_variant_type());
			}
			container_element_types.write[p_index] = p_type;
		}

		_FORCE_INLINE_ int get_container_element_type_count() const {
			return container_element_types.size();
		}

		_FORCE_INLINE_ DataType get_container_element_type(int p_index) const {
			ERR_FAIL_INDEX_V(p_index, container_element_types.size(), get_variant_type());
			return container_element_types[p_index];
		}

		_FORCE_INLINE_ DataType get_container_element_type_or_variant(int p_index) const {
			if (p_index < 0 || p_index >= container_element_types.size()) {
				return get_variant_type();
			}
			return container_element_types[p_index];
		}

		_FORCE_INLINE_ bool has_container_element_type(int p_index) const {
			return p_index >= 0 && p_index < container_element_types.size();
		}

		_FORCE_INLINE_ bool has_container_element_types() const {
			return !container_element_types.is_empty();
		}

		bool is_typed_container_type() const;

		BSParser::DataType get_typed_container_type() const;

		bool operator==(const DataType &p_other) const {
			if (type_source == UNDETECTED || p_other.type_source == UNDETECTED) {
				return true; // Can be considered equal for parsing purposes.
			}

			if (type_source == INFERRED || p_other.type_source == INFERRED) {
				return true; // Can be considered equal for parsing purposes.
			}

			if (kind != p_other.kind) {
				return false;
			}
			if (is_nullable != p_other.is_nullable) {
				return false;
			}
			if (is_type_handle_annotation != p_other.is_type_handle_annotation) {
				return false;
			}

			bool equal = false;
			switch (kind) {
				case VARIANT:
					equal = true; // All variants are the same.
					break;
				case BUILTIN:
					equal = builtin_type == p_other.builtin_type &&
							container_element_types == p_other.container_element_types;
					break;
				case NATIVE:
					// Coroutine[T] is a NATIVE skin over BSFunctionState whose identity also
					// depends on the phantom result type, so two coroutines differ when their result
					// types differ and a coroutine is never equal to a plain native of the same class.
					equal = native_type == p_other.native_type && is_coroutine == p_other.is_coroutine &&
							container_element_types == p_other.container_element_types;
					break;
				case ENUM: // Enums use native_type to identify the enum and its base class.
					// A tagged-union case constructor (`Message.Move` un-called) is a distinct
					// pseudo-type from a value of the union itself, so the case name participates.
					equal = native_type == p_other.native_type && enum_case_name == p_other.enum_case_name;
					break;
				case TUPLE:
					// Named tuples are nominal (`native_type` carries the class-qualified name); unnamed
					// tuples are structural, so their identity is exactly their element shape.
					equal = native_type == p_other.native_type && script_path == p_other.script_path &&
							tuple_field_names == p_other.tuple_field_names &&
							container_element_types == p_other.container_element_types;
					break;
				case UNION:
					// Members are normalized into a canonical order, so set identity is positional
					// identity and `int | uint` equals `uint | int` without a quadratic comparison.
					equal = union_members == p_other.union_members;
					break;
				case SCRIPT:
					equal = script_type == p_other.script_type;
					break;
				case CLASS:
					equal = class_type == p_other.class_type ||
							(class_type != nullptr && p_other.class_type != nullptr &&
									class_type->fqcn == p_other.class_type->fqcn);
					break;
				case TYPE_PARAMETER:
					equal = type_parameter_name == p_other.type_parameter_name &&
							type_parameter_scope == p_other.type_parameter_scope &&
							type_parameter_index == p_other.type_parameter_index &&
							type_parameter_bound == p_other.type_parameter_bound;
					break;
				case RESOLVING:
				case UNRESOLVED:
					break;
			}

			if (!equal) {
				return false;
			}

			// Specialized handles differ by their type arguments, e.g. Box[int] != Box[String].
			return type_arguments == p_other.type_arguments;
		}

		bool operator!=(const DataType &p_other) const {
			return !(*this == p_other);
		}

		DataType() = default;
	};

	struct ParserError {
		// TODO: Do I really need a "type"?
		// enum Type {
		//     NO_ERROR,
		//     EMPTY_FILE,
		//     CLASS_NAME_USED_TWICE,
		//     EXTENDS_USED_TWICE,
		//     EXPECTED_END_STATEMENT,
		// };
		// Type type = NO_ERROR;
		String message;
		int line = 0, column = 0;
		int end_line = 0, end_column = 0;
	};

#ifdef TOOLS_ENABLED
	struct ClassDocData {
		String brief;
		String description;
		Vector<Pair<String, String>> tutorials;
		bool is_deprecated = false;
		String deprecated_message;
		bool is_experimental = false;
		String experimental_message;
	};

	struct MemberDocData {
		String description;
		bool is_deprecated = false;
		String deprecated_message;
		bool is_experimental = false;
		String experimental_message;
	};
#endif // TOOLS_ENABLED

	struct Node {
		enum Type {
			NONE,
			ANNOTATION,
			ANNOTATION_DECLARATION,
			ARRAY,
			ASSERT,
			ASSIGNMENT,
			AWAIT,
			BINARY_OPERATOR,
			BREAK,
			BREAKPOINT,
			CALL,
			CAST,
			CLASS,
			CONFORMANCE,
			CONSTANT,
			CONTINUE,
			DICTIONARY,
			ENUM,
			FOR,
			FUNCTION,
			GET_NODE,
			IDENTIFIER,
			IF,
			LAMBDA,
			LITERAL,
			MATCH,
			MATCH_BRANCH,
			PARAMETER,
			PASS,
			PATTERN,
			PRELOAD,
			RETURN,
			SELF,
			SIGNAL,
			SUBSCRIPT,
			SUITE,
			TERNARY_OPERATOR,
			TUPLE,
			TUPLE_LITERAL,
			TYPE,
			TYPE_ALIAS,
			TYPE_PARAMETER,
			TYPE_TEST,
			UNARY_OPERATOR,
			VARIABLE,
			VARIABLE_DESTRUCTURE,
			WHILE,
			// A BaristaScript addition, not upstream's (fs_parser.h:588 @ c9d5e35 ends at WHILE). It
			// is what lets the vocabulary be closed over: `get_node_type_name()` is a `switch` with
			// no `default:`, so the compiler proves it names every enumerator, and this bound lets a
			// test walk them all and assert each one is either produced by a fixture or explicitly
			// marked unreachable. Never a node's own type.
			NODE_TYPE_MAX,
		};

		Type type = NONE;
		int start_line = 0, end_line = 0;
		int start_column = 0, end_column = 0;
		Node *next = nullptr;
		List<AnnotationNode *> annotations;

		DataType datatype;

		virtual DataType get_datatype() const { return datatype; }
		virtual void set_datatype(const DataType &p_datatype) { datatype = p_datatype; }

		virtual bool is_expression() const { return false; }

		virtual ~Node() {}
	};

	struct ExpressionNode : public Node {
		// Base type for all expression kinds.
		bool reduced = false;
		bool is_constant = false;
		Variant reduced_value;
		// When the analyzer resolves this expression to a namespaced global class used as a
		// value (e.g. `Foo` from the current/imported namespace, or a qualified `ns.Foo`), it
		// records the canonical class name here so the compiler can emit the class object
		// directly. The registered name is dotted (`ns.Foo`), which a bare-identifier lookup
		// cannot match. This covers namespaced script classes and namespaced native classes
		// alike. Empty otherwise.
		StringName resolved_global_class;
		// One entry per redundant parenthesized grouping this expression was the
		// sole content of, outermost last, e.g. `(  # note\n  1 + 2\n)` records one
		// entry with `open_line`/`close_line` at the `(`/`)`. Such a grouping
		// carries no semantic effect and gets no dedicated AST node -- each
		// `parse_grouping` call drops it and returns this expression directly --
		// so a comment trailing either delimiter would have nowhere left to attach.
		// The formatter checks this token-level metadata and re-wraps only a
		// grouping proven to own such a comment, instead of appending the comment
		// to text the caller has not finished writing. Empty when this expression
		// was never the direct content of a dropped grouping.
		struct GroupingSpan {
			int open_line = 0;
			int close_line = 0;
			int close_column = 0;
			// True only when the consumed `)` was the final source token on its
			// physical line. A comment on that line then belongs to this delimiter;
			// otherwise it belongs to the later comma/operator/postfix token.
			bool close_is_last_token_on_line = false;
		};
		Vector<GroupingSpan> redundant_groupings;

		virtual bool is_expression() const override { return true; }
		virtual ~ExpressionNode() {}

	protected:
		ExpressionNode() {}
	};

	struct AnnotationNode : public Node {
		StringName name;
		Vector<ExpressionNode *> arguments;
		// Parallel to `arguments`: the explicit parameter name for each argument written as
		// `name = value` at the use site, or an empty `StringName` for a positional argument.
		// Named arguments are only recognized for custom annotation usages; the analyzer maps
		// the names to declaration parameters. Built-in annotations keep positional-only entries.
		Vector<StringName> argument_names;
		Vector<Variant> resolved_arguments;

		/** Information of the annotation. Might be null for unknown annotations. */
		AnnotationInfo *info = nullptr;
		// True for an unresolved non-built-in annotation usage (`@my_annotation`). The parser
		// preserves the node instead of rejecting the unknown name so the analyzer can perform
		// import-aware resolution and validation. Such nodes have a null `info`.
		bool is_custom = false;
		// Canonical identity of the custom annotation declaration this usage resolved to, set by
		// the analyzer ("<namespace>.<name>", or "<name>" in the global namespace). Empty until a
		// custom usage resolves; the compiler reads it to persist passive annotation metadata.
		String resolved_qualified_name;
		PropertyInfo export_info;
		bool is_resolved = false;
		bool is_applied = false;

		bool apply(BSParser *p_this, Node *p_target, ClassNode *p_class);
		bool applies_to(uint32_t p_target_kinds) const;

		AnnotationNode() {
			type = ANNOTATION;
		}
	};

	struct AnnotationDeclarationNode : public Node {
		// Targets a custom annotation declaration may be applied to.
		// Stored as flags so an annotation can be valid for several kinds at once.
		// Bits beyond the v1 set are reserved for future targets.
		enum Target {
			TARGET_NONE = 0,
			TARGET_CLASS = 1 << 0,
			TARGET_METHOD = 1 << 1,
			TARGET_VARIABLE = 1 << 2,
			TARGET_SIGNAL = 1 << 3,
			TARGET_CONSTANT = 1 << 4,
			TARGET_PARAMETER = 1 << 5,
		};

		IdentifierNode *identifier = nullptr;
		Vector<ParameterNode *> parameters;
		HashMap<StringName, int> parameters_indices;
		ParameterNode *rest_parameter = nullptr; // Variadic parameter; must be the final parameter.
		uint32_t targets = TARGET_NONE; // Flags from `Target`.
		// Canonical identity: "<namespace>.<name>", or just "<name>" in the global namespace.
		String qualified_name;
		// True once the analyzer has validated the declaration signature (parameter types and
		// constant defaults). Guards the idempotent resolution shared by the local validation
		// pass and import-aware usage resolution from other files.
		bool resolved_signature = false;
#ifdef TOOLS_ENABLED
		MemberDocData doc_data;
#endif // TOOLS_ENABLED

		bool is_variadic() const { return rest_parameter != nullptr; }

		AnnotationDeclarationNode() {
			type = ANNOTATION_DECLARATION;
		}
	};

	struct ArrayNode : public ExpressionNode {
		Vector<ExpressionNode *> elements;

		ArrayNode() {
			type = ARRAY;
		}
	};

	// An unnamed tuple literal: `(a, b)`, arity >= 2. `(a)` remains ordinary expression
	// grouping (parsed as `a` itself, never this node) and `(a,)` is a hard parse error.
	struct TupleLiteralNode : public ExpressionNode {
		Vector<ExpressionNode *> elements;

		TupleLiteralNode() {
			type = TUPLE_LITERAL;
		}
	};

	struct AssertNode : public Node {
		ExpressionNode *condition = nullptr;
		ExpressionNode *message = nullptr;
		// The condition binds tagged-union payloads, so the binds are locals of the enclosing suite.
		bool condition_has_case_binds = false;

		AssertNode() {
			type = ASSERT;
		}
	};

	struct AssignableNode : public Node {
		IdentifierNode *identifier = nullptr;
		ExpressionNode *initializer = nullptr;
		TypeNode *datatype_specifier = nullptr;
		bool infer_datatype = false;
		bool use_conversion_assign = false;
		int usages = 0;

		virtual ~AssignableNode() {}

	protected:
		AssignableNode() {}
	};

	struct AssignmentNode : public ExpressionNode {
		// Assignment is not really an expression but it's easier to parse as if it were.
		enum Operation {
			OP_NONE,
			OP_ADDITION,
			OP_SUBTRACTION,
			OP_MULTIPLICATION,
			OP_DIVISION,
			OP_MODULO,
			OP_POWER,
			OP_BIT_SHIFT_LEFT,
			OP_BIT_SHIFT_RIGHT,
			OP_BIT_AND,
			OP_BIT_OR,
			OP_BIT_XOR,
		};

		Operation operation = OP_NONE;
		Variant::Operator variant_op = Variant::OP_MAX;
		ExpressionNode *assignee = nullptr;
		ExpressionNode *assigned_value = nullptr;
		bool use_conversion_assign = false;

		AssignmentNode() {
			type = ASSIGNMENT;
		}
	};

	struct AwaitNode : public ExpressionNode {
		ExpressionNode *to_await = nullptr;

		AwaitNode() {
			type = AWAIT;
		}
	};

	struct BinaryOpNode : public ExpressionNode {
		enum OpType {
			OP_ADDITION,
			OP_SUBTRACTION,
			OP_MULTIPLICATION,
			OP_DIVISION,
			OP_MODULO,
			OP_POWER,
			OP_BIT_LEFT_SHIFT,
			OP_BIT_RIGHT_SHIFT,
			OP_BIT_AND,
			OP_BIT_OR,
			OP_BIT_XOR,
			OP_LOGIC_AND,
			OP_LOGIC_OR,
			OP_CONTENT_TEST,
			OP_COMP_EQUAL,
			OP_COMP_NOT_EQUAL,
			OP_COMP_LESS,
			OP_COMP_LESS_EQUAL,
			OP_COMP_GREATER,
			OP_COMP_GREATER_EQUAL,
		};

		OpType operation = OpType::OP_ADDITION;
		Variant::Operator variant_op = Variant::OP_MAX;
		ExpressionNode *left_operand = nullptr;
		ExpressionNode *right_operand = nullptr;

		BinaryOpNode() {
			type = BINARY_OPERATOR;
		}
	};

	struct BreakNode : public Node {
		BreakNode() {
			type = BREAK;
		}
	};

	struct BreakpointNode : public Node {
		BreakpointNode() {
			type = BREAKPOINT;
		}
	};

	struct CallNode : public ExpressionNode {
		enum EnumCallKind {
			ENUM_CALL_NONE,
			ENUM_CALL_STATIC,
			ENUM_CALL_INSTANCE,
		};

		ExpressionNode *callee = nullptr;
		Vector<ExpressionNode *> arguments;
		// Parallel to `arguments`: the explicit parameter name for each argument written as
		// `name = value` at the call site, or an empty `StringName` for a positional argument.
		// The parser only records this surface syntax; the analyzer maps the names to parameter
		// positions and rewrites the call into canonical positional order.
		Vector<StringName> argument_names;
		StringName function_name;
		bool is_super = false;
		bool is_static = false;
		bool is_noreturn = false;
		// Explicit analyzer-to-compiler contract for calls to BaristaScript enum functions.
		// The scalar identity is stable across dependency parsers and avoids making codegen infer
		// enum dispatch from syntax or the enum's runtime Variant representation.
		EnumCallKind enum_call_kind = ENUM_CALL_NONE;
		String enum_call_owner_script_path;
		StringName enum_call_owner_class;
		StringName enum_call_enum_type;
		StringName enum_call_function;
		// Set by the analyzer when this is the built-in generic proxy constructor
		// `create_proxy[T](handler)`. The compiler lowers it to a
		// `create_proxy_dynamic(T, handler)` utility call, materializing T's script
		// from the `[T]` type argument.
		bool is_proxy_construct = false;
		// Set by the analyzer when the callee is a named tuple declaration rather than a function, so
		// the compiler builds the tuple value instead of dispatching a call. The result datatype alone
		// cannot decide this: an ordinary function may also return a tuple.
		bool is_tuple_construction = false;
		// Set by the analyzer when the callee is a payload-carrying case of a tagged union
		// (`Message.Move(1, 2)`) rather than a function, so the compiler builds the case value
		// `[tag, payload...]` instead of dispatching a call. `enum_case_tag` is the case's ordinal tag.
		bool is_enum_case_construction = false;
		int64_t enum_case_tag = 0;
		// Set by the parser when the callee is the leading-`.` contextual case shorthand
		// (`.Ok(1)`), whose tagged union is supplied by the expected type at the consumer site
		// rather than written at the call site. The callee is the contextual `SubscriptNode`,
		// which has no base until the analyzer qualifies it.
		bool is_contextual_enum_case = false;
		// Set by the analyzer when this calls a method whose typed-container return needs retyping at the
		// assignment target. Generic method elements (`-> Array[T]`) are erased at runtime; inherited
		// `Self` container returns are compiled against the declaring class while the static call type is
		// receiver-specialized.
		bool returns_erased_container = false;
		// Set by the analyzer once it has resolved which receiver this call dispatches on: true when
		// that receiver is the calling frame's own (an unqualified call, a `self`-qualified one, or
		// `super`), including the explicit-type-argument forms `name[T](...)` and `self.name[T](...)`
		// whose callee syntax is a subscript rather than a plain identifier or attribute access. A
		// `Self` parameter position then denotes the same value as the caller's `Self`.
		bool receiver_is_current_self = false;
		// Set by the analyzer on an enum-case construction spelled through a class-handle receiver
		// whose represented type is a type parameter (`handle.Message.Attach(...)` with
		// `handle: Type[T]`). A payload field the spelling substitutes to that parameter erases to
		// Variant at runtime, so the static schema keeps no class the argument's store could check --
		// but the handle value the frame received is exactly that class. The compiler evaluates this
		// expression once and checks every such argument against it, which is what makes the handle
		// spelling as strong at runtime as the concrete one.
		const ExpressionNode *enum_case_receiver_handle = nullptr;
		DataType enum_case_reified_parameter;
		// Set by the analyzer when this coroutine call's result is captured into a statically
		// `Coroutine[T]`-typed slot (a `Coroutine[T]` variable/parameter/return, or a
		// `Coroutine[T]` container element). Holding the live `BSFunctionState` handle to
		// await later is intentional, not a missing-await bug, so the compiler emits
		// `OPCODE_CALL_ASYNC` (store the handle without suspending and skip the debug
		// missing-await guard) exactly as it does for the operand of an `await`. A coroutine call
		// captured into a non-coroutine/Variant slot is left unmarked, so the runtime guard still
		// flags a genuinely forgotten "await".
		bool is_coroutine_handle_capture = false;
		// Canonical argument positions whose value the analyzer synthesized from a skipped middle
		// parameter's constant default during named-argument gap fill. Such an argument is excluded
		// from generic type-parameter inference and from post-substitution argument validation, so a
		// baked default behaves exactly like a trailing omitted default the callee fills in at runtime.
		HashSet<int> synthesized_argument_indices;
		// Source (left-to-right written) evaluation order for a canonicalized named-argument call.
		// Canonicalization rewrites `arguments` into parameter (positional) order so the callee binds
		// by position, but the argument expressions must still be evaluated in the order they were
		// written, like Python/C#/Kotlin. Each entry is an index into the canonical `arguments`,
		// listed in the order the compiler should evaluate them; synthesized constant gap-fills are
		// side-effect-free and ordered last. Empty for ordinary calls, where positional order already
		// matches source order and the compiler evaluates `arguments` front to back.
		Vector<int> argument_evaluation_order;
		// Resolved parameter types for the called signature, in declaration order,
		// recorded by the analyzer right before argument validation. The runtime compiler uses Type-handle
		// entries to preserve generic method substitutions at the call site; editor refactors also read
		// these to learn the type each argument flows into. Entries beyond the fixed parameter count
		// (varargs) are not recorded.
		Vector<DataType> resolved_parameter_types;
		// A call-site check the caller performs on one argument before dispatching. A generic function
		// is compiled once with its method-scope type parameters erased, so a parameter declared as `T`
		// reaches the callee with no run-time type to check an incoming value against. The caller is the
		// only frame that knows what the call substituted, so a gradual argument flowing into such a
		// parameter is validated and converted here, against the substituted type, and the converted
		// value is what the callee receives.
		struct GenericArgumentCheck {
			int argument_index = 0;
			DataType substituted_type;
		};
		// Recorded in ascending argument order by the analyzer, covering fixed parameters and resolved
		// rest elements alike. A substitution that stays open -- forwarding `inner[U](value)` while `U`
		// is still a parameter of the enclosing generic frame -- records nothing, because the caller has
		// no concrete type to check against either.
		Vector<GenericArgumentCheck> generic_argument_checks;
#ifdef TOOLS_ENABLED
		// Surface argument name per call slot, captured at parse time for editor code completion.
		// Indexed by argument slot: an empty entry for a positional argument and the parameter
		// name for a `name = value` argument (recorded even when the value is still missing at the
		// cursor). Unlike `argument_names`, this survives the analyzer's canonicalization, which
		// maps named arguments to parameter positions and clears `argument_names`. Completion reads
		// it to know which parameters a call already supplies; the runtime compiler does not use it.
		Vector<StringName> parsed_argument_names;
#endif // TOOLS_ENABLED

		CallNode() {
			type = CALL;
		}

		Type get_callee_type() const {
			if (callee == nullptr) {
				return Type::NONE;
			} else {
				return callee->type;
			}
		}
	};

	struct CastNode : public ExpressionNode {
		ExpressionNode *operand = nullptr;
		TypeNode *cast_type = nullptr;
		// D1 deletes upstream's `is_reinterpret` flag (fs_parser.h:973-976 @ c9d5e35). It marked the
		// `as!` bit-reinterpret operator, whose only purpose was crossing between equal-width integer
		// types; with one integer type there is nothing to reinterpret between, so `as!` is reserved
		// and never lexed (docs/GRAMMAR.md section 2.8) and every cast is a checked `as`.

		CastNode() {
			type = CAST;
		}
	};

	struct EnumNode : public Node {
		// A payload field belonging to a tagged-union case, e.g. `x: int` in `Move(x: int, y: int)`.
		// Names are required on every payload field in v1.
		struct PayloadField {
			IdentifierNode *identifier = nullptr;
			TypeNode *type = nullptr;
			int line = 0;
			int start_column = 0;
			int end_column = 0;
#ifdef TOOLS_ENABLED
			MemberDocData doc_data;
#endif // TOOLS_ENABLED
		};

		struct Value {
			IdentifierNode *identifier = nullptr;
			ExpressionNode *custom_value = nullptr;
			EnumNode *parent_enum = nullptr;
			// Payload fields for a tagged-union case; empty for a payload-less case.
			Vector<PayloadField> payload_fields;
			// Source line of the payload's closing ")"; 0 when there is no payload. Tracked
			// separately from the last field's line so formatting can interleave a comment
			// attached to the closing delimiter itself.
			int payload_close_line = 0;
			int index = -1;
			bool resolved = false;
			int64_t value = 0;
			int line = 0;
			// Line of the last token that belongs to this case (its own line for a bare or
			// `= expression` case; the payload's closing ")" line for a payload case). `line`
			// alone is not enough once a payload can span multiple source lines.
			int end_line = 0;
			int start_column = 0;
			int end_column = 0;
#ifdef TOOLS_ENABLED
			MemberDocData doc_data;
#endif // TOOLS_ENABLED

			bool has_payload() const {
				return !payload_fields.is_empty();
			}
		};

		IdentifierNode *identifier = nullptr;
		// Type parameters of a generic tagged union, e.g. `T` and `E` in `enum Result[T, E]`.
		// Only a named enum whose completed body declares at least one payload-bearing case may
		// carry them; every other shape is rejected while parsing the declaration.
		Vector<TypeParameterNode *> type_parameters;
		Vector<Value> values;
		Vector<FunctionNode *> functions;
		HashMap<StringName, int> functions_indices;
		Variant dictionary;
		// True iff any case declares a payload, i.e. this enum is a tagged union. Tags are then
		// ordinal by declaration order and explicit `= value` on any case is a parser error.
		bool is_tagged_union = false;
#ifdef TOOLS_ENABLED
		MemberDocData doc_data;
#endif // TOOLS_ENABLED

		EnumNode() {
			type = ENUM;
		}
	};

	// A named tuple declaration: `tuple Name(x: float, y: float)`. Fields may be named
	// (`x: float`) or positional (a bare type). Element order is significant: `.0`-style
	// index access always follows declaration order regardless of naming.
	struct TupleNode : public Node {
		struct Field {
			IdentifierNode *identifier = nullptr; // Null for a positional field.
			TypeNode *type = nullptr;
			int line = 0;
			int start_column = 0;
			int end_column = 0;
#ifdef TOOLS_ENABLED
			MemberDocData doc_data;
#endif // TOOLS_ENABLED
		};

		IdentifierNode *identifier = nullptr;
		Vector<Field> fields;
#ifdef TOOLS_ENABLED
		MemberDocData doc_data;
#endif // TOOLS_ENABLED

		TupleNode() {
			type = TUPLE;
		}
	};

	struct ClassNode : public Node {
		struct TraitUse {
			Vector<IdentifierNode *> name;
			// Type arguments specializing a generic trait at the use site: `uses Container[int]`.
			Vector<TypeNode *> type_arguments;
			// The resolved (and validated) type arguments, bound to the trait's type parameters.
			Vector<DataType> resolved_type_arguments;
			ClassNode *resolved_trait = nullptr;
			// True when the named trait is generic but this entry supplied no type arguments. The
			// condition is recorded during trait-use resolution and reported by the declaring file's
			// own analyzer, so the diagnostic lands in the file that wrote the entry no matter which
			// file's analysis resolved it first.
			bool missing_type_arguments = false;

			String to_string() const {
				String result;
				for (int i = 0; i < name.size(); i++) {
					if (i > 0) {
						result += ".";
					}
					result += name[i]->name;
				}
				return result;
			}
		};

		struct Member {
			enum Type {
				UNDEFINED,
				CLASS,
				CONSTANT,
				FUNCTION,
				SIGNAL,
				VARIABLE,
				ENUM,
				ENUM_VALUE, // For unnamed enums.
				GROUP, // For member grouping.
				TUPLE,
				TYPE_ALIAS, // `type Name = ...`: a static-only type name, erased before runtime.
			};

			Type type = UNDEFINED;

			union {
				ClassNode *m_class = nullptr;
				ConstantNode *constant;
				FunctionNode *function;
				SignalNode *signal;
				VariableNode *variable;
				EnumNode *m_enum;
				AnnotationNode *annotation;
				TupleNode *m_tuple;
				TypeAliasNode *type_alias;
			};
			EnumNode::Value enum_value;

			String get_name() const {
				switch (type) {
					case UNDEFINED:
						return "<undefined member>";
					case CLASS:
						// All class-type members have an id.
						return m_class->identifier->name;
					case CONSTANT:
						return constant->identifier->name;
					case FUNCTION:
						return function->identifier->name;
					case SIGNAL:
						return signal->identifier->name;
					case VARIABLE:
						return variable->identifier->name;
					case ENUM:
						// All enum-type members have an id.
						return m_enum->identifier->name;
					case ENUM_VALUE:
						return enum_value.identifier->name;
					case GROUP:
						return annotation->export_info.name;
					case TUPLE:
						// All tuple-type members have an id.
						return m_tuple->identifier->name;
					case TYPE_ALIAS:
						// All alias members have an id.
						return type_alias->identifier->name;
				}
				return "";
			}

			String get_type_name() const {
				switch (type) {
					case UNDEFINED:
						return "???";
					case CLASS:
						return m_class != nullptr && m_class->is_trait ? "trait" : "class";
					case CONSTANT:
						return "constant";
					case FUNCTION:
						return "function";
					case SIGNAL:
						return "signal";
					case VARIABLE:
						return "variable";
					case ENUM:
						return "enum";
					case ENUM_VALUE:
						return "enum value";
					case GROUP:
						return "group";
					case TUPLE:
						return "tuple";
					case TYPE_ALIAS:
						return "type alias";
				}
				return "";
			}

			int get_line() const {
				switch (type) {
					case CLASS:
						return m_class->start_line;
					case CONSTANT:
						return constant->start_line;
					case FUNCTION:
						return function->start_line;
					case VARIABLE:
						return variable->start_line;
					case ENUM_VALUE:
						return enum_value.line;
					case ENUM:
						return m_enum->start_line;
					case SIGNAL:
						return signal->start_line;
					case GROUP:
						return annotation->start_line;
					case TUPLE:
						return m_tuple->start_line;
					case TYPE_ALIAS:
						return type_alias->start_line;
					case UNDEFINED:
						ERR_FAIL_V_MSG(-1, "Reaching undefined member type.");
				}
				ERR_FAIL_V_MSG(-1, "Reaching unhandled type.");
			}

			DataType get_datatype() const {
				switch (type) {
					case CLASS:
						return m_class->get_datatype();
					case CONSTANT:
						return constant->get_datatype();
					case FUNCTION:
						return function->get_datatype();
					case VARIABLE:
						return variable->get_datatype();
					case ENUM:
						return m_enum->get_datatype();
					case ENUM_VALUE:
						return enum_value.identifier->get_datatype();
					case SIGNAL:
						return signal->get_datatype();
					case GROUP:
						return DataType();
					case TUPLE:
						// The declaration carries the tuple's meta type; `type_from_metatype()`
						// turns it into the instance type at use sites.
						return m_tuple->get_datatype();
					case TYPE_ALIAS:
						// An alias is a spelling for another type, never a value, so the
						// declaration itself has no type of its own.
						return DataType();
					case UNDEFINED:
						return DataType();
				}
				ERR_FAIL_V_MSG(DataType(), "Reaching unhandled type.");
			}

			Node *get_source_node() const {
				switch (type) {
					case CLASS:
						return m_class;
					case CONSTANT:
						return constant;
					case FUNCTION:
						return function;
					case VARIABLE:
						return variable;
					case ENUM:
						return m_enum;
					case ENUM_VALUE:
						return enum_value.identifier;
					case SIGNAL:
						return signal;
					case GROUP:
						return annotation;
					case TUPLE:
						return m_tuple;
					case TYPE_ALIAS:
						return type_alias;
					case UNDEFINED:
						return nullptr;
				}
				ERR_FAIL_V_MSG(nullptr, "Reaching unhandled type.");
			}

			Member() {}

			Member(ClassNode *p_class) {
				type = CLASS;
				m_class = p_class;
			}
			Member(ConstantNode *p_constant) {
				type = CONSTANT;
				constant = p_constant;
			}
			Member(VariableNode *p_variable) {
				type = VARIABLE;
				variable = p_variable;
			}
			Member(SignalNode *p_signal) {
				type = SIGNAL;
				signal = p_signal;
			}
			Member(FunctionNode *p_function) {
				type = FUNCTION;
				function = p_function;
			}
			Member(EnumNode *p_enum) {
				type = ENUM;
				m_enum = p_enum;
			}
			Member(const EnumNode::Value &p_enum_value) {
				type = ENUM_VALUE;
				enum_value = p_enum_value;
			}
			Member(AnnotationNode *p_annotation) {
				type = GROUP;
				annotation = p_annotation;
			}
			Member(TupleNode *p_tuple) {
				type = TUPLE;
				m_tuple = p_tuple;
			}
			Member(TypeAliasNode *p_type_alias) {
				type = TYPE_ALIAS;
				type_alias = p_type_alias;
			}
		};

		IdentifierNode *identifier = nullptr;
		String icon_path;
		String simplified_icon_path;
		Vector<Member> members;
		HashMap<StringName, int> members_indices;
		ClassNode *outer = nullptr;
		bool extends_used = false;
		bool onready_used = false;
		bool is_abstract = false;
		bool is_final = false;
		bool has_static_data = false;
		bool annotated_static_unload = false;
		// True for inline TraitNode declarations and root ClassNode declarations created by `trait_name`.
		// Do not infer the dynamic node type from this flag.
		bool is_trait = false;
		bool is_enum_file = false; // Root class only. File declares a top-level enum_name.
		EnumNode *enum_file_decl = nullptr; // Root class only. The enum declared by enum_name.
		bool is_tuple_file = false; // Root class only. File declares a top-level tuple_name.
		TupleNode *tuple_file_decl = nullptr; // Root class only. The tuple declared by tuple_name.
		bool trait_name_used = false;
		bool uses_used = false;
		String extends_path;
		Vector<IdentifierNode *> extends; // List for indexing: extends A.B.C
		Vector<TypeNode *> extends_type_arguments; // Type arguments on a generic base: extends List[T], extends List[int].
		Vector<TypeParameterNode *> type_parameters; // Generic parameters: class Box[T], class_name Pair[K, V].
		Vector<TraitUse> used_traits;
		Vector<ClassNode *> resolved_traits;
		DataType base_type;
		String fqcn; // Fully-qualified class name. Identifies uniquely any class in the project.
		String namespace_name; // Root class only. Empty means global namespace.
		String qualified_global_name; // Root class only. Namespace + "." + `class_name`.
		Vector<String> imports; // Root class only. File-local imported namespaces.
		// Root class only. Custom annotation declarations live in a separate symbol space and are
		// intentionally kept out of `members` so they never become runtime members, constants, or methods.
		Vector<AnnotationDeclarationNode *> annotation_declarations;
		// Root class only. Retroactive trait conformances (`extend Target uses Trait: ...`) live in a
		// separate space and are intentionally kept out of `members` so they never become runtime members.
		Vector<ConformanceNode *> conformances;
#ifdef TOOLS_ENABLED
		ClassDocData doc_data;
		// Source lines, in source order, of the body-level `pass` statements the parser
		// erased. A class-body `pass` declares nothing, so it leaves no member behind and
		// a comment authored on its line would lose its only owner. Trivia ownership
		// only: nothing outside the formatter reads this and it carries no semantics.
		Vector<int> erased_pass_lines;

		// EnumValue docs are parsed after itself, so we need a method to add/modify the doc property later.
		void set_enum_value_doc_data(const StringName &p_name, const MemberDocData &p_doc_data) {
			ERR_FAIL_INDEX(members_indices[p_name], members.size());
			members.write[members_indices[p_name]].enum_value.doc_data = p_doc_data;
		}
#endif // TOOLS_ENABLED

		bool resolved_interface = false;
		bool resolved_body = false;
		bool resolving_trait_uses = false;
		bool resolved_trait_uses = false;
		bool failed_trait_uses = false;
		// True for the member-less stand-in the analyzer synthesizes to represent a native engine-class
		// target of a retroactive conformance (`extend Node uses ...`). It is owned by the parser but is
		// not part of any file's class table, so the external-parser lookups that back cross-file member
		// resolution must treat it as a fully-resolved local class rather than a foreign one.
		bool is_native_conformance_shim = false;
		// True for the member-less stand-in the analyzer synthesizes to represent a builtin value-type
		// target of a retroactive conformance (`extend int uses ...`). Like the native stand-in, it is
		// owned by the parser but absent from the class table.
		bool is_builtin_conformance_shim = false;

		StringName get_global_name() const {
			if (outer != nullptr || identifier == nullptr) {
				return StringName();
			}
			return qualified_global_name.is_empty() ? identifier->name : StringName(qualified_global_name);
		}

		Member get_member(const StringName &p_name) const {
			return members[members_indices[p_name]];
		}
		bool has_member(const StringName &p_name) const {
			return members_indices.has(p_name);
		}
		bool has_function(const StringName &p_name) const {
			return has_member(p_name) && members[members_indices[p_name]].type == Member::FUNCTION;
		}
		template <typename T>
		void add_member(T *p_member_node) {
			members_indices[p_member_node->identifier->name] = members.size();
			members.push_back(Member(p_member_node));
		}
		void add_member(const EnumNode::Value &p_enum_value) {
			members_indices[p_enum_value.identifier->name] = members.size();
			members.push_back(Member(p_enum_value));
		}
		void add_member_group(AnnotationNode *p_annotation_node) {
			// Avoid name conflict. See GH-78252.
			StringName name = vformat("@group_%d_%s", members.size(), p_annotation_node->export_info.name);
			members_indices[name] = members.size();
			members.push_back(Member(p_annotation_node));
		}

		ClassNode() {
			type = CLASS;
		}
	};

	struct TraitNode : public ClassNode {
		TraitNode() {
			is_trait = true;
		}
	};

	struct ConformanceNode : public Node {
		TypeNode *target = nullptr; // Unspecialized target type; type arguments are a parse error.
		Vector<ClassNode::TraitUse> traits; // Traits supplied by `uses`.
		Vector<FunctionNode *> witnesses; // Method witnesses parsed from the body.
		// For a native engine-class target (`extend Node uses ...`), the analyzer synthesizes a member-less
		// stand-in ClassNode whose base is the native class so the shared conformance machinery (witness
		// `self` typing, member access against the native surface, signature validation) can be reused. Null
		// for BaristaScript class/script targets, which resolve to a real ClassNode.
		ClassNode *native_target_shim = nullptr;
		// For a builtin value-type target (`extend int uses ...`), the analyzer synthesizes a member-less
		// stand-in ClassNode whose self datatype is the builtin type so witness bodies resolve against the
		// Variant builtin surface. Null for other target kinds.
		ClassNode *builtin_target_shim = nullptr;
#ifdef TOOLS_ENABLED
		// Source lines of the body-level `pass` statements the parser erased. See
		// `ClassNode::erased_pass_lines`: trivia ownership only, for the formatter.
		Vector<int> erased_pass_lines;
#endif // TOOLS_ENABLED

		ConformanceNode() {
			type = CONFORMANCE;
		}
	};

	struct ConstantNode : public AssignableNode {
#ifdef TOOLS_ENABLED
		MemberDocData doc_data;
#endif // TOOLS_ENABLED

		ConstantNode() {
			type = CONSTANT;
		}
	};

	struct ContinueNode : public Node {
		ContinueNode() {
			type = CONTINUE;
		}
	};

	struct DictionaryNode : public ExpressionNode {
		struct Pair {
			ExpressionNode *key = nullptr;
			ExpressionNode *value = nullptr;
		};
		Vector<Pair> elements;

		enum Style {
			LUA_TABLE,
			PYTHON_DICT,
		};
		Style style = PYTHON_DICT;

		DictionaryNode() {
			type = DICTIONARY;
		}
	};

	struct ForNode : public Node {
		IdentifierNode *variable = nullptr;
		TypeNode *datatype_specifier = nullptr;
		bool use_conversion_assign = false;
		ExpressionNode *list = nullptr;
		SuiteNode *loop = nullptr;

		ForNode() {
			type = FOR;
		}
	};

	struct FunctionNode : public Node {
		IdentifierNode *identifier = nullptr;
		Vector<TypeParameterNode *> type_parameters; // Generic parameters: func swap[T]().
		Vector<ParameterNode *> parameters;
		HashMap<StringName, int> parameters_indices;
		ParameterNode *rest_parameter = nullptr;
		TypeNode *return_type = nullptr;
		SuiteNode *body = nullptr;
		// False when the declaration ended without a ":" and a body. Only valid for an
		// `abstract` function -- the analyzer rejects any other bodyless function -- but
		// the form parses either way, so tooling that must reproduce the source (the
		// formatter) needs to tell it apart from a function whose body is merely empty.
		bool has_body = true;
		// Source line of the last token of the signature (`:` when present, otherwise the
		// closing `)` / return type). Distinct from `end_line`, which spans a body when
		// one is present. The formatter uses this to reattach comments from a collapsed
		// multi-line signature without walking the body.
		int signature_end_line = 0;
		bool is_abstract = false;
		bool is_final = false;
		bool is_noreturn = false;
		bool is_static = false; // For lambdas it's determined in the analyzer.
		bool is_declared_async = false;
		bool is_coroutine = false;
		Variant rpc_config;
		MethodInfo info;
		LambdaNode *source_lambda = nullptr;
		EnumNode *owner_enum = nullptr;
		Vector<Variant> default_arg_values;
#ifdef TOOLS_ENABLED
		MemberDocData doc_data;
		int min_local_doc_line = 0;
		String signature; // For autocompletion.
#endif // TOOLS_ENABLED

		bool resolved_signature = false;
		bool resolved_body = false;

		_FORCE_INLINE_ bool is_vararg() const { return rest_parameter != nullptr; }

		FunctionNode() {
			type = FUNCTION;
		}
	};

	struct GetNodeNode : public ExpressionNode {
		String full_path;
		bool use_dollar = true;

		GetNodeNode() {
			type = GET_NODE;
		}
	};

	struct IdentifierNode : public ExpressionNode {
		StringName name;
		SuiteNode *suite = nullptr; // The block in which the identifier is used.

		enum Source {
			UNDEFINED_SOURCE,
			FUNCTION_PARAMETER,
			LOCAL_VARIABLE,
			LOCAL_CONSTANT,
			LOCAL_ITERATOR, // `for` loop iterator.
			LOCAL_BIND, // Pattern bind.
			MEMBER_VARIABLE,
			MEMBER_CONSTANT,
			MEMBER_FUNCTION,
			MEMBER_SIGNAL,
			MEMBER_CLASS,
			INHERITED_VARIABLE,
			STATIC_VARIABLE,
			NATIVE_CLASS,
			// `Self` in an expression position: the class handle of the frame's exact static receiver,
			// which is only known when the frame runs.
			STATIC_SELF_CLASS,
		};
		Source source = UNDEFINED_SOURCE;

		union {
			ParameterNode *parameter_source = nullptr;
			IdentifierNode *bind_source;
			VariableNode *variable_source;
			ConstantNode *constant_source;
			SignalNode *signal_source;
			FunctionNode *function_source;
		};
		bool function_source_is_static = false; // For non-BaristaScript scripts.

		FunctionNode *source_function = nullptr; // TODO: Rename to disambiguate `function_source`.

		int usages = 0; // Useful for binds/iterator variable.

		// Set when a conformance witness resolved this name through the declaration-site half of its
		// scope rather than through its target. Compilation binds a witness's members to the target's
		// script, so it needs this to know which names to look up in the declaring script instead.
		bool resolved_from_conformance_declaration_scope = false;

		IdentifierNode() {
			type = IDENTIFIER;
		}
	};

	struct IfNode : public Node {
		ExpressionNode *condition = nullptr;
		SuiteNode *true_block = nullptr;
		SuiteNode *false_block = nullptr;
		// The condition binds tagged-union payloads, so the true block's locals must be allocated
		// before the condition is compiled.
		bool condition_has_case_binds = false;

		IfNode *get_elif() {
			return const_cast<IfNode *>(static_cast<const IfNode *>(this)->get_elif());
		}

		// An `elif` is parsed as a false block holding a single `if` that begins on the same line.
		const IfNode *get_elif() const {
			if (false_block == nullptr || false_block->statements.size() != 1) {
				return nullptr;
			}
			const Node *statement = false_block->statements[0];
			if (statement->type != IF || false_block->start_line != statement->start_line) {
				return nullptr;
			}
			return static_cast<const IfNode *>(statement);
		}

		IfNode() {
			type = IF;
		}
	};

	struct LambdaNode : public ExpressionNode {
		FunctionNode *function = nullptr;
		FunctionNode *parent_function = nullptr;
		LambdaNode *parent_lambda = nullptr;
		Vector<IdentifierNode *> captures;
		HashMap<StringName, int> captures_indices;
		bool use_self = false;

		bool has_name() const {
			return function && function->identifier;
		}

		LambdaNode() {
			type = LAMBDA;
		}
	};

	struct LiteralNode : public ExpressionNode {
		// D1: an integer literal has no suffix to declare a width with and no magnitude-selected
		// width to record (fs_parser.h:1656-1662 @ c9d5e35). Its `Variant` carrier is its whole type.
		Variant value;

		LiteralNode() {
			type = LITERAL;
		}
	};

	struct MatchNode : public Node {
		ExpressionNode *test = nullptr;
		Vector<MatchBranchNode *> branches;
		// Set by the analyzer when the branches provably cover every value the subject can take, so a
		// `match` whose branches all terminate is itself terminating for flow analysis. Computed in
		// every build configuration: flow finality decides whether a program compiles.
		bool covers_subject_domain = false;
		// Display name of the subject's domain and the comma-joined values it leaves uncovered. Only
		// populated for the domains whose values are enumerated -- a tagged union, a plain enum, or
		// `bool`; used by diagnostics.
		String subject_domain_name;
		String uncovered_domain_values;
		// Set when the subject is a plain enum without an unguarded catch-all. A plain enum is backed
		// by an integer carrier that accepts undeclared values, so listing every declared member never
		// closes the match; diagnostics say that instead of naming values to add.
		bool subject_domain_is_open_enum = false;
		// Structured companion to `uncovered_domain_values` for a tagged-union subject: the cases no
		// branch provably covers, in declaration order, and whether the subject's `null` value is
		// among them. Tooling (code generation, completion filtering) must read these instead of
		// parsing the display string. Only populated for tagged-union subjects; an empty
		// `uncovered_case_names` with `covers_subject_domain` false and `uncovered_includes_null`
		// false means coverage could not be proven.
		Vector<StringName> uncovered_case_names;
		bool uncovered_includes_null = false;
#ifdef TOOLS_ENABLED
		// Source lines of the `match`-level `pass` statements the parser erased. See
		// `ClassNode::erased_pass_lines`: trivia ownership only, for the formatter.
		Vector<int> erased_pass_lines;
#endif // TOOLS_ENABLED

		MatchNode() {
			type = MATCH;
		}
	};

	struct MatchBranchNode : public Node {
		Vector<PatternNode *> patterns;
		SuiteNode *block = nullptr;
		bool has_wildcard = false;
		SuiteNode *guard_body = nullptr;

		MatchBranchNode() {
			type = MATCH_BRANCH;
		}
	};

	struct ParameterNode : public AssignableNode {
		ParameterNode() {
			type = PARAMETER;
		}
	};

	struct PassNode : public Node {
		PassNode() {
			type = PASS;
		}
	};

	struct PatternNode : public Node {
		enum Type {
			PT_LITERAL,
			PT_EXPRESSION,
			PT_BIND,
			PT_ARRAY,
			PT_DICTIONARY,
			PT_REST,
			PT_WILDCARD,
			PT_TUPLE, // `(a, b)`: parenthesized sub-patterns, arity >= 2.
			PT_ENUM_CASE, // `Message.Move(x, _)`: tagged-union case reference plus payload sub-patterns.
		};
		Type pattern_type = PT_LITERAL;

		union {
			LiteralNode *literal = nullptr;
			IdentifierNode *bind;
			ExpressionNode *expression;
		};
		// Sub-patterns of an array, tuple or enum-case pattern.
		Vector<PatternNode *> array;
		bool rest_used = false; // For array/dict patterns.
		// A bind written without `var`, which only payload positions of a case pattern allow.
		bool implicit_bind = false;
		// Whether the pattern was written parenthesized. Grouping is dropped from the tree but stays
		// meaningful in a case payload position, where `Case(NAME)` binds and `Case((NAME))` compares.
		bool was_grouped = false;
		// Whether this pattern matches every value of the type it was resolved against. Set by the
		// analyzer; a case pattern is never irrefutable, since it always tests the tag.
		bool is_irrefutable = false;
		// For PT_ENUM_CASE: whether every payload sub-pattern is irrefutable, i.e. the pattern matches
		// every value of its case and therefore covers it for exhaustiveness.
		bool case_payload_is_irrefutable = false;
		// For PT_EXPRESSION: whether the analyzer classified this as the supported same-subject type
		// test, `match value: value is T:`. The analyzer owns the classification so pattern validation,
		// flow narrowing, and the compiler's lowering choice cannot disagree about which expression
		// patterns are a type test on the match subject rather than a value comparison.
		bool is_subject_type_test = false;

		// For PT_ENUM_CASE: whether the head was written with the leading-`.` contextual shorthand
		// (`.Ok(value)`), so `case_type` names only the case and the union comes from the subject.
		bool is_contextual_enum_case = false;

		// For PT_ENUM_CASE: the dotted case reference (`Message.Move`) and its resolved case type.
		TypeNode *case_type = nullptr;
		DataType case_datatype;

		struct Pair {
			ExpressionNode *key = nullptr;
			PatternNode *value_pattern = nullptr;
		};
		Vector<Pair> dictionary;

		HashMap<StringName, IdentifierNode *> binds;

		bool has_bind(const StringName &p_name);
		IdentifierNode *get_bind(const StringName &p_name);

		PatternNode() {
			type = PATTERN;
		}
	};
	struct PreloadNode : public ExpressionNode {
		ExpressionNode *path = nullptr;
		String resolved_path;
		Ref<Resource> resource;

		PreloadNode() {
			type = PRELOAD;
		}
	};

	struct ReturnNode : public Node {
		ExpressionNode *return_value = nullptr;
		bool void_return = false;

		ReturnNode() {
			type = RETURN;
		}
	};

	struct SelfNode : public ExpressionNode {
		ClassNode *current_class = nullptr;

		SelfNode() {
			type = SELF;
		}
	};

	struct SignalNode : public Node {
		IdentifierNode *identifier = nullptr;
		Vector<ParameterNode *> parameters;
		HashMap<StringName, int> parameters_indices;
		MethodInfo method_info;
#ifdef TOOLS_ENABLED
		MemberDocData doc_data;
#endif // TOOLS_ENABLED

		int usages = 0;

		SignalNode() {
			type = SIGNAL;
		}
	};

	struct SubscriptNode : public ExpressionNode {
		ExpressionNode *base = nullptr;
		union {
			ExpressionNode *index = nullptr;
			IdentifierNode *attribute;
		};

		bool is_attribute = false;
		// Set for a tuple-index access (`t.0`), distinguishing it from an ordinary subscript
		// (`arr[0]`). `is_attribute` is false and `index` holds the integer-literal index node.
		bool is_tuple_index = false;
		// Set for the leading-`.` contextual tagged-union case shorthand (`.None`, and the callee
		// of `.Ok(1)`). `is_attribute` is true and `attribute` holds the case name, but `base` is
		// null: the union it belongs to comes from the expected type at the consumer site.
		bool is_contextual_enum_case = false;

		// Use-site type-argument list for generics, e.g. `Pair[int, String]` or `id[Node?]`.
		// Populated only when the subscript brackets carry more than one comma-separated argument
		// and/or a `?` nullable marker; `index` always aliases the first element. Empty for an
		// ordinary single-index subscript so existing consumers keep reading `index` directly.
		Vector<ExpressionNode *> type_arguments;
		// Parallel to `type_arguments`: whether each argument carried a trailing `?` (e.g. `Node?`).
		Vector<bool> type_argument_is_nullable;

		SubscriptNode() {
			type = SUBSCRIPT;
		}
	};

	struct SuiteNode : public Node {
		SuiteNode *parent_block = nullptr;
		Vector<Node *> statements;
		struct Local {
			enum Type {
				UNDEFINED,
				CONSTANT,
				VARIABLE,
				PARAMETER,
				FOR_VARIABLE,
				PATTERN_BIND,
				CASE_BIND, // Payload bind of an `is Case(...)` test, scoped to the guarded suite.
			};
			Type type = UNDEFINED;
			union {
				ConstantNode *constant = nullptr;
				VariableNode *variable;
				ParameterNode *parameter;
				IdentifierNode *bind;
			};
			StringName name;
			FunctionNode *source_function = nullptr;

			int start_line = 0, end_line = 0;
			int start_column = 0, end_column = 0;

			DataType get_datatype() const;
			String get_name() const;

			Local() {}
			Local(ConstantNode *p_constant, FunctionNode *p_source_function) {
				type = CONSTANT;
				constant = p_constant;
				name = p_constant->identifier->name;
				source_function = p_source_function;

				start_line = p_constant->start_line;
				end_line = p_constant->end_line;
				start_column = p_constant->start_column;
				end_column = p_constant->end_column;
			}
			Local(VariableNode *p_variable, FunctionNode *p_source_function) {
				type = VARIABLE;
				variable = p_variable;
				name = p_variable->identifier->name;
				source_function = p_source_function;

				start_line = p_variable->start_line;
				end_line = p_variable->end_line;
				start_column = p_variable->start_column;
				end_column = p_variable->end_column;
			}
			Local(ParameterNode *p_parameter, FunctionNode *p_source_function) {
				type = PARAMETER;
				parameter = p_parameter;
				name = p_parameter->identifier->name;
				source_function = p_source_function;

				start_line = p_parameter->start_line;
				end_line = p_parameter->end_line;
				start_column = p_parameter->start_column;
				end_column = p_parameter->end_column;
			}
			Local(IdentifierNode *p_identifier, FunctionNode *p_source_function) {
				type = FOR_VARIABLE;
				bind = p_identifier;
				name = p_identifier->name;
				source_function = p_source_function;

				start_line = p_identifier->start_line;
				end_line = p_identifier->end_line;
				start_column = p_identifier->start_column;
				end_column = p_identifier->end_column;
			}
		};
		Local empty;
		Vector<Local> locals;
		HashMap<StringName, int> locals_indices;

		FunctionNode *parent_function = nullptr;
		IfNode *parent_if = nullptr;

		bool has_return = false;
		bool has_continue = false;
		bool has_unreachable_code = false; // Just so warnings aren't given more than once per block.
		bool is_in_loop = false; // The block is nested in a loop (directly or indirectly).

		bool has_local(const StringName &p_name) const;
		const Local &get_local(const StringName &p_name) const;
		template <typename T>
		void add_local(T *p_local, FunctionNode *p_source_function) {
			locals_indices[p_local->identifier->name] = locals.size();
			locals.push_back(Local(p_local, p_source_function));
		}
		void add_local(const Local &p_local) {
			locals_indices[p_local.name] = locals.size();
			locals.push_back(p_local);
		}

		SuiteNode() {
			type = SUITE;
		}
	};

	struct TernaryOpNode : public ExpressionNode {
		// Only one ternary operation exists, so no abstraction here.
		ExpressionNode *condition = nullptr;
		ExpressionNode *true_expr = nullptr;
		ExpressionNode *false_expr = nullptr;

		TernaryOpNode() {
			type = TERNARY_OPERATOR;
		}
	};

	struct TypeNode : public Node {
		Vector<IdentifierNode *> type_chain;
		Vector<TypeNode *> container_types;
		Vector<TypeNode *> signature_parameter_types;
		// Optional final `...Array[T]` entry of a Callable signature. A Signal never has one.
		TypeNode *signature_rest_parameter_type = nullptr;
		TypeNode *signature_return_type = nullptr;
		bool has_signature = false;
		bool signature_is_async = false; // Set when the type was written as AsyncCallable.
		bool is_coroutine = false; // Set when the type was written as Coroutine[T].
		bool is_nullable = false;
		// Set when the type was written as an unnamed tuple type, `(int, String)`. `type_chain`
		// is empty in this case; the element types live in `tuple_element_types` instead.
		bool is_tuple = false;
		Vector<TypeNode *> tuple_element_types;
		// Set when the type was written as a union of alternatives, `int | uint`. `type_chain` is
		// empty in this case and the alternatives, in source order, live in `union_member_types`.
		// A union node is only produced when a `|` was actually written, so a lone alternative is
		// still its own node and no type acquires a wrapper it was not spelled with.
		bool is_union = false;
		Vector<TypeNode *> union_member_types;
		// Set only for the right-hand side of an `is` test, where a dotted name may name a
		// tagged-union case (`Message.Move`) instead of a type. Type annotations keep rejecting
		// case names, since a case is not a type.
		bool allows_enum_case = false;
		// Set for the leading-`.` contextual case shorthand on the right of an `is` test (`x is
		// .Ok(value)`). `type_chain` holds only the case name: the union it belongs to comes from the
		// tested operand's own type, so the node is never resolved as a written-out type.
		bool is_contextual_enum_case = false;
		// Type arguments written in expression form, produced only by a `match` case-pattern head such
		// as `Result[int, String].Ok(value)`. Only the trailing `(` tells a case reference apart from
		// an ordinary indexed value pattern (`TABLE[KEY]`), so the brackets are parsed as a subscript
		// and their arguments are reinterpreted as types once the case is known. A type written in a
		// type position fills `container_types` instead; the two are never both populated.
		Vector<ExpressionNode *> type_argument_expressions;
		// Parallel to `type_argument_expressions`: whether each argument carried a trailing `?`.
		Vector<bool> type_argument_expression_is_nullable;
		// Index in `type_chain` of the name the argument list was written after: 0 for `Box[int]` and
		// 1 for `Outer.Box[int]`. Only meaningful when the type carries arguments, and it is what lets
		// the type be printed back with the suffix in the position the author wrote it.
		int type_arguments_chain_index = 0;

		TypeNode *get_container_type_or_null(int p_index) const {
			return p_index >= 0 && p_index < container_types.size() ? container_types[p_index] : nullptr;
		}

		TypeNode() {
			type = TYPE;
		}
	};

	// `type Name = A | B`: a transparent, file-local name for a type. The alias is static only --
	// it declares no runtime member, no constant, and no nominal type -- so `aliased_type` is the
	// whole declaration and the name never reaches the runtime.
	struct TypeAliasNode : public Node {
		IdentifierNode *identifier = nullptr;
		TypeNode *aliased_type = nullptr;

		TypeAliasNode() {
			type = TYPE_ALIAS;
		}
	};

	struct TypeParameterNode : public Node {
		IdentifierNode *identifier = nullptr;
		TypeNode *bound = nullptr; // Optional upper bound: [T: Resource].
		// Resolved (non-meta) upper bound, populated eagerly during class-interface analysis so that
		// runtime reflection can report a parameter's bound even when the parameter is never referenced.
		DataType resolved_bound;

		TypeParameterNode() {
			type = TYPE_PARAMETER;
		}
	};

	struct TypeTestNode : public ExpressionNode {
		ExpressionNode *operand = nullptr;
		TypeNode *test_type = nullptr;
		DataType test_datatype;
		// Payload binds of a tagged-union case test, `msg is Message.Move(x, _)`. A null entry is a
		// `_` skip. Empty when the test carries no bind list.
		Vector<IdentifierNode *> case_binds;
		// Set by the statement parsers when the test sits where binds can become suite locals: the
		// condition, or an `and`-conjunct of the condition, of `if`/`elif`/`while`/`assert`.
		bool binds_allowed = false;

		TypeTestNode() {
			type = TYPE_TEST;
		}
	};

	struct UnaryOpNode : public ExpressionNode {
		enum OpType {
			OP_POSITIVE,
			OP_NEGATIVE,
			OP_COMPLEMENT,
			OP_LOGIC_NOT,
		};

		OpType operation = OP_POSITIVE;
		Variant::Operator variant_op = Variant::OP_MAX;
		ExpressionNode *operand = nullptr;
		// `x is not T` desugars to a logical negation of a type test, so the negation the author wrote
		// after `is` is not recoverable from the tree alone. Recording it lets the formatter reprint the
		// surface form; nothing in parsing or semantics reads it.
		bool source_is_not = false;

		UnaryOpNode() {
			type = UNARY_OPERATOR;
		}
	};

	struct VariableNode : public AssignableNode {
		enum PropertyStyle {
			PROP_NONE,
			PROP_INLINE,
			PROP_SETGET,
		};

		PropertyStyle property = PROP_NONE;
		union {
			FunctionNode *setter = nullptr;
			IdentifierNode *setter_pointer;
		};
		IdentifierNode *setter_parameter = nullptr;
		union {
			FunctionNode *getter = nullptr;
			IdentifierNode *getter_pointer;
		};

		bool exported = false;
		bool onready = false;
		PropertyInfo export_info;
		int assignments = 0;
		bool is_static = false;
		bool is_final = false;
#ifdef TOOLS_ENABLED
		MemberDocData doc_data;
#endif // TOOLS_ENABLED

		VariableNode() {
			type = VARIABLE;
		}
	};

	// A destructuring declaration (`var (x, y) = pair` / `const (name, hp) = player`). Each binding is
	// a regular local variable so name resolution, typing, and code generation reuse the ordinary
	// local machinery; a `_` slot is stored as a null entry, meaning "skip this element".
	struct VariableDestructureNode : public Node {
		Vector<VariableNode *> bindings;
		ExpressionNode *initializer = nullptr;
		// `const` bindings are write-once locals, enforced by the same analysis as `final var`.
		bool is_const = false;

		VariableDestructureNode() {
			type = VARIABLE_DESTRUCTURE;
		}
	};

	struct WhileNode : public Node {
		ExpressionNode *condition = nullptr;
		SuiteNode *loop = nullptr;
		// The condition binds tagged-union payloads, so the loop's locals must be allocated before
		// the condition is compiled.
		bool condition_has_case_binds = false;

		WhileNode() {
			type = WHILE;
		}
	};

	enum CompletionType {
		COMPLETION_NONE,
		COMPLETION_ANNOTATION, // Annotation (following @).
		COMPLETION_ANNOTATION_ARGUMENTS, // Annotation arguments hint.
		COMPLETION_ASSIGN, // Assignment based on type (e.g. enum values).
		COMPLETION_ATTRIBUTE, // After id.| to look for members.
		COMPLETION_ATTRIBUTE_METHOD, // After id.| to look for methods.
		COMPLETION_BUILT_IN_TYPE_CONSTANT_OR_STATIC_METHOD, // Constants inside a built-in type (e.g. Color.BLUE) or static methods (e.g. Color.html).
		COMPLETION_CALL_ARGUMENTS, // Complete with nodes, input actions, enum values (or usual expressions).
		COMPLETION_CONTEXTUAL_UNION_CASE, // Tagged-union case named by a leading "." (.Ok), with no union spelled.
		COMPLETION_DECLARATION, // Potential class-body declaration (var, const, func).
		COMPLETION_GET_NODE, // Get node with $ notation.
		COMPLETION_IDENTIFIER, // List available identifiers in scope.
		COMPLETION_IMPORT_NAMESPACE, // Namespace after import.
		COMPLETION_INHERIT_TYPE, // Type after extends. Exclude non-viable types (built-ins, enums, void). Includes subtypes using the argument index.
		COMPLETION_METHOD, // List available methods in scope.
		COMPLETION_OVERRIDE_METHOD, // Override implementation, also for native virtuals.
		COMPLETION_PROPERTY_DECLARATION, // Property declaration (get, set).
		COMPLETION_PROPERTY_DECLARATION_OR_TYPE, // Property declaration (get, set) or a type hint.
		COMPLETION_PROPERTY_METHOD, // Property setter or getter (list available methods).
		COMPLETION_RESOURCE_PATH, // For load/preload.
		COMPLETION_SUBSCRIPT, // Inside id[|].
		COMPLETION_SUPER, // super(), used for lookup.
		COMPLETION_SUPER_METHOD, // After super.
		COMPLETION_TYPE_ATTRIBUTE, // Attribute in type name (Type.|).
		COMPLETION_TYPE_HANDLE_ARGUMENT, // Represented instance type inside `Type[T]`.
		COMPLETION_TYPE_NAME, // Name of type (after :).
		COMPLETION_TYPE_NAME_OR_VOID, // Same as TYPE_NAME, but allows void (in function return type).
		COMPLETION_USES, // Trait name after uses. Only traits are viable.
	};

	struct CompletionCall {
		Node *call = nullptr;
		int argument = -1;
	};

	struct CompletionContext {
		CompletionType type = COMPLETION_NONE;
		ClassNode *current_class = nullptr;
		FunctionNode *current_function = nullptr;
		SuiteNode *current_suite = nullptr;
		int current_line = -1;
		union {
			int current_argument = -1;
			int type_chain_index;
		};
		Variant::Type builtin_type = Variant::VARIANT_MAX;
		Node *node = nullptr;
		Object *base = nullptr;
		// The `match` whose pattern list holds the cursor, and the branch inside it, for a contextual
		// case shorthand completed in match-pattern position. Null for every other completion, which is
		// what keeps expression-position and `is`-position completion unfiltered. Raw pointers are safe
		// for the same reason `node` and `current_suite` are: a context never outlives its parse tree.
		MatchNode *match = nullptr;
		MatchBranchNode *match_branch = nullptr;
		BSParser *parser = nullptr;
		CompletionCall call;
		// Identifiers already typed before the cursor in a dotted clause (e.g. the
		// `characters` in `uses characters.`), used to resolve the namespace prefix.
		Vector<IdentifierNode *> chain;
	};

private:
	friend class BSAnalyzer;
	// The parse tree's test surface. godot-cpp's String and Variant are engine-backed, so a ported
	// front-end can only be exercised from inside a running Godot; the probe needs the node
	// allocation list and the error list to render what a test compares. It reads and never mutates.
	friend class BaristaScriptParserProbe;
	bool _is_tool = false;
	String script_path;
	bool for_completion = false;
	bool parse_body = true;
	bool panic_mode = false;
	bool can_break = false;
	bool can_continue = false;
	List<bool> multiline_stack;

	// Bound recursion in the recursive-descent parser. Deeply nested expressions or
	// statements (e.g. thousands of nested parentheses) or nested type annotations
	// (e.g. `Array[Array[...]]`) would otherwise overflow the native call stack and
	// crash, since the editor and language server parse untrusted, partially-typed
	// scripts. When the limit is exceeded the parser reports an error and bails out
	// instead of descending further. The limit mirrors `Variant::MAX_RECURSION_DEPTH`,
	// well below the stack-overflow threshold yet far beyond any legitimate source.
	// Upstream uses `Variant::MAX_RECURSION_DEPTH` (fs_parser.h:2220 @ c9d5e35). godot-cpp's
	// `Variant` is the opaque, engine-backed one and exposes no such enumerator, so the value core
	// declares -- 1024, Foundry `core/variant/variant.h:156` @ c9d5e35, unchanged from stock Godot --
	// is written down once here. It is a constant, not a behaviour: an extension that guessed
	// differently would accept nesting the engine's own `Variant` recursion would refuse.
	static constexpr int MAX_NESTING_DEPTH = 1024;
	int expression_nesting_depth = 0;
	int statement_nesting_depth = 0;
	int type_nesting_depth = 0;
	int pattern_nesting_depth = 0;

	ClassNode *head = nullptr;
	Node *list = nullptr;
	List<ParserError> errors;

protected:
	// Every node the parse allocated, newest first. Tooling subclasses use it to find the analyzed node
	// at a source position without re-walking the tree shape; nothing may mutate the chain.
	Node *get_allocated_nodes() const { return list; }

private:
#ifdef DEBUG_ENABLED
public:
	struct WarningDirectoryRule {
		enum Decision {
			DECISION_EXCLUDE,
			DECISION_INCLUDE,
			DECISION_MAX,
		};

		String directory_path; // With a trailing slash.
		Decision decision = DECISION_EXCLUDE;
	};

private:
	struct PendingWarning {
		const Node *source = nullptr;
		BSWarning::Code code = BSWarning::WARNING_MAX;
		bool treated_as_error = false;
		Vector<String> symbols;
	};

	static bool is_project_ignoring_warnings;
	static BSWarning::WarnLevel warning_levels[BSWarning::WARNING_MAX];
	static LocalVector<WarningDirectoryRule> warning_directory_rules;

	List<BSWarning> warnings;
	List<PendingWarning> pending_warnings;
	bool is_script_ignoring_warnings = false;
	HashSet<int> warning_ignored_lines[BSWarning::WARNING_MAX];
	int warning_ignore_start_lines[BSWarning::WARNING_MAX];
	HashSet<int> unsafe_lines;
#endif // DEBUG_ENABLED

	BSTokenizer *tokenizer = nullptr;
	BSTokenizer::Token previous;
	BSTokenizer::Token current;
	BSTokenizer::Token lookahead;
	bool has_lookahead = false;

	ClassNode *current_class = nullptr;
	EnumNode *current_enum = nullptr; // The enum whose body is being parsed, for its type-parameter scope.
	FunctionNode *current_function = nullptr;
	LambdaNode *current_lambda = nullptr;
	SuiteNode *current_suite = nullptr;

	// Case-payload binds declared while parsing the condition currently being parsed (if/elif/while/assert).
	// They are declared as transient locals of `current_suite` as soon as `is Case(binds)` is parsed so a
	// later `and`-conjunct of the same condition can already reference them; `declare_condition_case_binds()`
	// removes the transient entries and relocates the ones in a legal bind position into the guarded suite.
	Vector<IdentifierNode *> pending_case_binds;
	// Non-zero while parsing the condition expression of an if/elif/while/assert, i.e. only while
	// there is a matching declare_condition_case_binds() call downstream that will clean up any
	// transient case-bind locals. A case-bind test parsed anywhere else (an ordinary expression
	// statement, a default value, ...) must not declare a transient local, or it would leak into the
	// rest of the suite with nothing to remove it.
	int case_bind_condition_depth = 0;

	CompletionContext completion_context;
	List<CompletionCall> completion_call_stack;
	bool in_lambda = false;
	bool lambda_ended = false; // Marker for when a lambda ends, to apply an end of statement if needed.

	typedef bool (BSParser::*AnnotationAction)(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	struct AnnotationInfo {
		enum TargetKind {
			NONE = 0,
			SCRIPT = 1 << 0,
			CLASS = 1 << 1,
			VARIABLE = 1 << 2,
			CONSTANT = 1 << 3,
			SIGNAL = 1 << 4,
			FUNCTION = 1 << 5,
			STATEMENT = 1 << 6,
			STANDALONE = 1 << 7,
			PARAMETER = 1 << 8,
			CLASS_LEVEL = CLASS | VARIABLE | CONSTANT | SIGNAL | FUNCTION,
		};
		uint32_t target_kind = 0; // Flags.
		AnnotationAction apply = nullptr;
		MethodInfo info;
	};
	static HashMap<StringName, AnnotationInfo> valid_annotations;
	List<AnnotationNode *> annotation_stack;

	typedef ExpressionNode *(BSParser::*ParseFunction)(ExpressionNode *p_previous_operand, bool p_can_assign);
	// Higher value means higher precedence (i.e. is evaluated first).
	enum Precedence {
		PREC_NONE,
		PREC_ASSIGNMENT,
		PREC_CAST,
		PREC_TERNARY,
		PREC_LOGIC_OR,
		PREC_LOGIC_AND,
		PREC_LOGIC_NOT,
		PREC_CONTENT_TEST,
		PREC_COMPARISON,
		PREC_BIT_OR,
		PREC_BIT_XOR,
		PREC_BIT_AND,
		PREC_BIT_SHIFT,
		PREC_ADDITION_SUBTRACTION,
		PREC_FACTOR,
		PREC_SIGN,
		PREC_BIT_NOT,
		PREC_POWER,
		PREC_TYPE_TEST,
		PREC_AWAIT,
		PREC_CALL,
		PREC_ATTRIBUTE,
		PREC_SUBSCRIPT,
		PREC_PRIMARY,
	};
	struct ParseRule {
		ParseFunction prefix = nullptr;
		ParseFunction infix = nullptr;
		Precedence precedence = PREC_NONE;
	};
	// The table is `constexpr`, so the rule it hands back is read-only; nothing mutates a rule.
	static const ParseRule *get_rule(BSTokenizer::Token::Type p_token_type);

	List<Node *> nodes_in_progress;
	void complete_extents(Node *p_node);
	void update_extents(Node *p_node);
	void reset_extents(Node *p_node, BSTokenizer::Token p_token);
	void reset_extents(Node *p_node, Node *p_from);

	template <typename T>
	T *alloc_node() {
		T *node = memnew(T);

		node->next = list;
		list = node;

		reset_extents(node, previous);
		nodes_in_progress.push_back(node);

		return node;
	}

	// Allocates a node for patching up the parse tree when an error occurred.
	// Such nodes don't track their extents as they don't relate to actual tokens.
	template <typename T>
	T *alloc_recovery_node() {
		T *node = memnew(T);
		node->next = list;
		list = node;

		return node;
	}

	SuiteNode *alloc_recovery_suite() {
		SuiteNode *suite = alloc_recovery_node<SuiteNode>();
		suite->parent_block = current_suite;
		suite->parent_function = current_function;
		suite->is_in_loop = current_suite->is_in_loop;
		return suite;
	}

	void clear();

	void push_error(const String &p_message, const Node *p_origin = nullptr);
	/**
	 * Reports a diagnostic at a token's own span rather than at `previous`.
	 *
	 * A tokenizer diagnostic arrives on an `ERROR` token that `advance()` skips, so by the time it
	 * is reported `previous` is whatever preceded the malformed text -- upstream points the reader
	 * at unrelated punctuation (fs_parser.cpp:634 @ c9d5e35). The offending token knows where it is.
	 */
	void push_error_at(const String &p_message, const BSTokenizer::Token &p_token);
	/**
	 * Reports a tokenizer diagnostic carried on an `ERROR` token, at that token's span.
	 *
	 * Separate from `push_error_at()` because it is what makes the run a *lexical* failure. A
	 * parser-owned rejection also wants a token's span -- `reject_reserved_type_name()` points at
	 * the spelling rather than at the punctuation before it -- and must not be mistaken for one.
	 */
	void push_tokenizer_error(const BSTokenizer::Token &p_error_token);

	/**
	 * True when the token now in `current` was reached by skipping one or more `ERROR` tokens that
	 * `advance()` has already reported.
	 *
	 * A type position the tokenizer settles (`->`, `as`, `is`) rejects a D1-removed spelling itself
	 * and consumes it, which leaves the parser looking at a hole. Deriving "Expected type specifier"
	 * from that hole would be a second, differently worded complaint about a token the tokenizer
	 * already rejected -- which the fail-closed contract forbids. The flag is what lets those sites
	 * stay silent, and only those: it says nothing about whether the source is otherwise valid.
	 */
	bool current_follows_tokenizer_error = false;
	/** The same fact about the buffered lookahead token, carried across when it becomes `current`. */
	bool lookahead_follows_tokenizer_error = false;

	/**
	 * Sticky for the whole run: the source did not tokenize.
	 *
	 * The parser keeps building after a lexical failure, and deliberately: the tree it recovers is
	 * exactly the tree an editor needs while someone is mid-way through typing a string literal, and
	 * upstream recovers the same way. That leaves `get_tree()` non-null on source that never
	 * tokenized, so the distinction the fail-closed contract asks for is written down here instead
	 * of being inferred from a tree that looks ordinary. `parse()` returns `ERR_PARSE_ERROR` too;
	 * this says *why*.
	 */
	bool tokenizer_failed = false;
#ifdef DEBUG_ENABLED
	void push_warning(const Node *p_source, BSWarning::Code p_code, const Vector<String> &p_symbols);
	template <typename... Symbols>
	void push_warning(const Node *p_source, BSWarning::Code p_code, const Symbols &...p_symbols) {
		push_warning(p_source, p_code, Vector<String>{ p_symbols... });
	}
	void apply_pending_warnings();
	void evaluate_warning_directory_rules_for_script_path();
#endif // DEBUG_ENABLED

	// Setting p_force to false will prevent the completion context from being update if a context was already set before.
	// This should only be done when we push context before we consumed any tokens for the corresponding structure.
	// See parse_precedence for an example.
	void make_completion_context(CompletionType p_type, Node *p_node, int p_argument = -1, bool p_force = true, const Vector<IdentifierNode *> *p_chain = nullptr);
	void make_completion_context(CompletionType p_type, Variant::Type p_builtin_type, bool p_force = true);
	// In some cases it might become necessary to alter the completion context after parsing a subexpression.
	// For example to not override COMPLETE_CALL_ARGUMENTS with COMPLETION_NONE from string literals.
	void override_completion_context(const Node *p_for_node, CompletionType p_type, Node *p_node, int p_argument = -1);
	void push_completion_call(Node *p_call);
	void pop_completion_call();
	void set_last_completion_call_arg(int p_argument);

	BSTokenizer::Token advance();
	const BSTokenizer::Token &peek();
	bool match(BSTokenizer::Token::Type p_token_type);
	bool check(BSTokenizer::Token::Type p_token_type) const;
	bool consume(BSTokenizer::Token::Type p_token_type, const String &p_error_message);
	/**
	 * Reports a D1-reserved type spelling standing in a type position, without consuming it.
	 *
	 * Called at every type position the token stream itself cannot settle -- a type argument, a
	 * qualified type name's segments, `extends`/`uses`, a `type` alias, and every position that
	 * declares a type name. See the definition for why it does not consume.
	 */
	void reject_reserved_type_name();
	bool is_at_end() const;
	bool is_statement_end_token() const;
	bool is_statement_end() const;
	void end_statement(const String &p_context);
	void synchronize();
	void push_multiline(bool p_state);
	void pop_multiline();

	// Leading run of declaration modifiers (`final`, `abstract`, `static`, `async`) collected
	// before a class-body member and validated against the declaration that follows.
	struct DeclarationModifiers {
		bool is_final = false;
		bool is_abstract = false;
		bool is_static = false;
		bool is_async = false;

		int final_line = 0;
		int final_column = 0;
		int abstract_line = 0;
		int abstract_column = 0;
		int static_line = 0;
		int static_column = 0;
		int async_line = 0;
		int async_column = 0;

		bool has_any() const { return is_final || is_abstract || is_static || is_async; }
	};

	DeclarationModifiers collect_declaration_modifiers();
	void validate_declaration_modifiers(const DeclarationModifiers &p_modifiers, const char *p_target_kind, bool p_allow_abstract, bool p_allow_static, bool p_allow_async, bool p_allow_final, bool p_in_trait);

	// Main blocks.
	void parse_program();
	ClassNode *parse_class(const DeclarationModifiers &p_modifiers);
	TraitNode *parse_trait(const DeclarationModifiers &p_modifiers);
	bool parse_identifier_chain(const String &p_declaration_name, String &r_chain);
	void parse_namespace();
	void parse_import();
	void parse_class_name();
	void parse_trait_name();
	void parse_enum_name(bool p_can_register_enum_file);
	void parse_tuple_name(bool p_can_register_tuple_file);
	void parse_extends();
	void parse_uses();
	bool parse_trait_use(ClassNode::TraitUse &r_trait_use);
	void parse_type_parameters(Vector<TypeParameterNode *> &r_type_parameters);
	void parse_class_body(bool p_is_multiline);
	List<AnnotationNode *> parse_class_member_annotations(AnnotationInfo::TargetKind p_target,
			const String &p_member_kind, const StringName &p_exclusive_builtin = StringName());
	template <typename T>
	void finalize_class_member(T *p_member, List<AnnotationNode *> &p_annotations, const String &p_member_kind);
	template <typename T>
	void parse_class_member(T *(BSParser::*p_parse_function)(const DeclarationModifiers &),
			AnnotationInfo::TargetKind p_target, const String &p_member_kind,
			const DeclarationModifiers &p_modifiers, const StringName &p_exclusive_builtin = StringName());
	void parse_function_class_member(const DeclarationModifiers &p_modifiers);
	AnnotationDeclarationNode *parse_annotation_declaration();
	TypeAliasNode *parse_type_alias();
	bool starts_type_alias_declaration();
	ConformanceNode *parse_conformance();
	void parse_conformance_uses(ConformanceNode *p_conformance);
	void parse_conformance_body(ConformanceNode *p_conformance, bool p_is_multiline);
	void parse_annotation_declaration_parameters(AnnotationDeclarationNode *p_annotation_declaration);
	void parse_annotation_declaration_targets(AnnotationDeclarationNode *p_annotation_declaration);
	SignalNode *parse_signal(const DeclarationModifiers &p_modifiers);
	EnumNode *parse_enum(const DeclarationModifiers &p_modifiers);
	TupleNode *parse_tuple(const DeclarationModifiers &p_modifiers);
	void finalize_enum_function(EnumNode *p_enum, FunctionNode *p_function,
			List<AnnotationNode *> &p_annotations, int &r_min_doc_line, bool p_store);
	void parse_enum_case_payload(EnumNode::Value &r_value);
	ParameterNode *parse_parameter(bool p_allow_annotations = true);
	FunctionNode *parse_function_declaration(const DeclarationModifiers &p_modifiers);
	bool parse_function_signature(FunctionNode *p_function, SuiteNode *p_body, const String &p_type, int p_signature_start);
	SuiteNode *parse_suite(const String &p_context, SuiteNode *p_suite = nullptr, bool p_for_lambda = false);
	// Annotations
	AnnotationNode *parse_annotation(uint32_t p_valid_targets);
	static bool register_annotation(const MethodInfo &p_info, uint32_t p_target_kinds, AnnotationAction p_apply, const Vector<Variant> &p_default_arguments = Vector<Variant>(), bool p_is_vararg = false);
	bool validate_annotation_arguments(AnnotationNode *p_annotation);
	void clear_unused_annotations();
	bool tool_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool icon_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool static_unload_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool autoload_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool keep_name_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool noreturn_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool onready_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	template <PropertyHint t_hint, Variant::Type t_type>
	bool export_annotations(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool export_storage_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool export_custom_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool export_tool_button_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	template <PropertyUsageFlags t_usage>
	bool export_group_annotations(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool warning_ignore_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool warning_ignore_region_annotations(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	bool rpc_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class);
	// Statements.
	Node *parse_statement();
	VariableNode *parse_variable(const DeclarationModifiers &p_modifiers);
	VariableNode *parse_variable(bool p_is_static, bool p_allow_property, bool p_is_final = false);
	VariableDestructureNode *parse_variable_destructure(bool p_is_const);
	VariableNode *parse_property(VariableNode *p_variable, bool p_need_indent);
	void parse_property_getter(VariableNode *p_variable);
	void parse_property_setter(VariableNode *p_variable);
	ConstantNode *parse_constant(const DeclarationModifiers &p_modifiers);
	AssertNode *parse_assert();
	BreakNode *parse_break();
	ContinueNode *parse_continue();
	ForNode *parse_for();
	IfNode *parse_if(const String &p_token = "if");
	MatchNode *parse_match();
	MatchBranchNode *parse_match_branch();
	PatternNode *parse_match_pattern(PatternNode *p_root_pattern = nullptr);
	void parse_match_pattern_dotted_head(PatternNode *p_pattern, PatternNode *p_root_pattern);
	PatternNode *parse_match_case_payload_bind(PatternNode *p_root_pattern);
	WhileNode *parse_while();
	// Expressions.
	ExpressionNode *parse_expression(bool p_can_assign, bool p_stop_on_assign = false, bool p_stop_on_question_mark = false);
	ExpressionNode *parse_precedence(Precedence p_precedence, bool p_can_assign, bool p_stop_on_assign = false, bool p_stop_on_question_mark = false);
	// Runs the Pratt infix loop over an already-parsed left operand, so a caller that consumed a
	// prefix form itself (e.g. the dotted head of a match pattern) can finish the expression.
	ExpressionNode *parse_infix_operators(ExpressionNode *p_previous_operand, Precedence p_precedence, bool p_can_assign, bool p_stop_on_assign = false, bool p_stop_on_question_mark = false);
	ExpressionNode *parse_literal(ExpressionNode *p_previous_operand, bool p_can_assign);
	LiteralNode *parse_literal();
	ExpressionNode *parse_self(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_identifier(ExpressionNode *p_previous_operand, bool p_can_assign);
	IdentifierNode *parse_identifier();
	ExpressionNode *parse_builtin_constant(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_unary_operator(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_binary_operator(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_binary_not_in_operator(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_ternary_operator(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_assignment(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_array(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_dictionary(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_call(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_get_node(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_preload(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_grouping(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_cast(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_await(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_attribute(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_contextual_enum_case(ExpressionNode *p_previous_operand, bool p_can_assign);
	TypeNode *parse_contextual_enum_case_type();
	ExpressionNode *parse_subscript(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_lambda(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_type_test(ExpressionNode *p_previous_operand, bool p_can_assign);
	void parse_type_test_case_binds(TypeTestNode *p_type_test);
	ExpressionNode *parse_yield(ExpressionNode *p_previous_operand, bool p_can_assign);
	ExpressionNode *parse_invalid_token(ExpressionNode *p_previous_operand, bool p_can_assign);
	TypeNode *parse_type(bool p_allow_void = false, CompletionType p_forced_completion = COMPLETION_NONE, bool p_allow_enum_case = false);
	TypeNode *parse_type_member(bool p_allow_void, CompletionType p_forced_completion, bool p_allow_enum_case);
	void validate_union_member_type(const TypeNode *p_member);
	bool is_enclosing_type_parameter_name(const StringName &p_name) const;

	// Declares a case-payload bind name as a transient local of `current_suite`, as soon as it is
	// parsed, so later `and`-conjuncts of the same condition can already reference it. Rejects names
	// that shadow an existing local. `declare_condition_case_binds()` later removes the transient
	// entry (and relocates it if it ended up in a legal bind position).
	void declare_transient_case_bind(IdentifierNode *p_bind);

	// Collects the bind-carrying `is` tests a condition may legally declare binds for: the condition
	// itself, or any `and`-conjunct of it. Marks each collected test as permitted so the analyzer can
	// reject bind lists written anywhere else.
	static void collect_condition_case_binds(ExpressionNode *p_condition, Vector<TypeTestNode *> &r_type_tests);
	// Removes the transient case-bind locals declared into `current_suite` since `p_pending_mark`
	// (an index into `pending_case_binds`, rebuilding `SuiteNode::locals_indices`), then declares
	// the ones in a legal bind position (from p_type_tests) as locals of p_suite.
	bool declare_condition_case_binds(const Vector<TypeTestNode *> &p_type_tests, SuiteNode *p_suite, int p_pending_mark);

#ifdef TOOLS_ENABLED
	int max_script_doc_line = INT_MAX;
	int min_member_doc_line = 1;
	bool has_comment(int p_line, bool p_must_be_doc = false);
	MemberDocData parse_doc_comment(int p_line, bool p_single_line = false);
	ClassDocData parse_class_doc_comment(int p_line, bool p_single_line = false);
#endif // TOOLS_ENABLED

public:
	Error parse(const String &p_source_code, const String &p_script_path, bool p_for_completion, bool p_parse_body = true);
	// `BSTokenizerBuffer::set_code_buffer()` takes a `PackedByteArray`, which is the byte-array type
	// godot-cpp's `FileAccess` returns; upstream spells both `Vector<uint8_t>` (fs_parser.h:2601 @
	// c9d5e35), which is the same type in core.
	Error parse_binary(const PackedByteArray &p_binary, const String &p_script_path);
	ClassNode *get_tree() const { return head; }
	/** Whether the source failed to tokenize. See the member for why a tree still exists. */
	bool has_tokenizer_failure() const { return tokenizer_failed; }
	bool is_tool() const { return _is_tool; }
	ClassNode *find_class(const String &p_qualified_name) const;
	bool has_class(const BSParser::ClassNode *p_class) const;
	/**
	 * The name of an AST node kind, as one `switch` with no `default:` label.
	 *
	 * A `default:` is what would let a new enumerator fall through to an "unknown node" string, so
	 * there is none; the build promotes the compiler's unhandled-enumerator diagnostic to an error
	 * (`-Werror=switch`, set in `SConstruct` and `CMakeLists.txt`), which is what makes exhaustive a
	 * build guarantee rather than a comment. This is the only place a node kind is spelled in text.
	 */
	static String get_node_type_name(Node::Type p_type);

	static Variant::Type get_builtin_type(const StringName &p_type); // Excluding `Variant::NIL` and `Variant::OBJECT`.

	// A built-in source name resolved to the Variant carrier it travels in. D1 collapses upstream's
	// `BuiltinDataType` pair (fs_parser.h:2628-2635 @ c9d5e35) back to the carrier alone: with one
	// integer type the carrier answers what a source type name means, so the pair would be a carrier
	// plus a constant.
	static bool is_builtin_data_type(const StringName &p_type) { return get_builtin_type(p_type) < Variant::VARIANT_MAX; }

	// The compiler-provided `Number` type: the closed union of the source-spellable numeric types,
	// which D1 fixes at exactly `int` and `float` (docs/GRAMMAR.md section 7.1).
	static const StringName &get_number_type_name();
	static DataType make_number_type();

	// The BaristaScript source spelling of a built-in slot, the single spelling policy every
	// source-facing surface uses. D1 drops upstream's width parameter: a carrier names itself.
	static String get_builtin_type_source_name(Variant::Type p_builtin_type);

	CompletionContext get_completion_context() const { return completion_context; }
	void get_annotation_list(List<MethodInfo> *r_annotations) const;
	bool annotation_exists(const String &p_annotation_name) const;

	const List<ParserError> &get_errors() const { return errors; }
	// Errors are appended in the order the front-end discovers them, which is not source order: the
	// end-of-phase sweeps report positions that precede diagnostics already recorded. Surfaces that
	// show diagnostics to a human present them in source order through this view; `errors` itself keeps
	// emission order, which the compiler and the tests rely on.
	Vector<const ParserError *> get_errors_in_source_order() const;
	List<String> get_dependencies() const;
	// The files this one loads solely because they declare retroactive conformances in its own
	// namespace or in a namespace it imports. Included in `get_dependencies()`; reported separately
	// so the compiler can load exactly these at run time, where nothing else references them.
	List<String> get_namespace_conformance_dependencies() const;

#ifdef DEBUG_ENABLED
	static void update_project_settings();
	static bool is_ignoring_warnings() { return is_project_ignoring_warnings; }
	static void set_ignoring_warnings(bool p_ignore) { is_project_ignoring_warnings = p_ignore; }
	const List<BSWarning> &get_warnings() const { return warnings; }
#endif // DEBUG_ENABLED

#ifdef DEBUG_ENABLED
	const HashSet<int> &get_unsafe_lines() const { return unsafe_lines; }
#endif // DEBUG_ENABLED

#ifdef DEBUG_ENABLED
	int get_last_line_number() const { return current.end_line; }
#endif // DEBUG_ENABLED

#ifdef TOOLS_ENABLED
	static HashMap<String, String> theme_color_names;

	HashMap<int, BSTokenizer::CommentData> comment_data;
#endif // TOOLS_ENABLED

	BSParser();
	~BSParser();

#ifdef DEBUG_ENABLED
	class TreePrinter {
		int indent_level = 0;
		String indent;
		StringBuilder printed;
		bool pending_indent = false;

		void increase_indent();
		void decrease_indent();
		void push_line(const String &p_line = String());
		void push_text(const String &p_text);

		void print_annotation(const AnnotationNode *p_annotation);
		void print_annotation_declaration(AnnotationDeclarationNode *p_annotation_declaration);
		void print_array(ArrayNode *p_array);
		void print_assert(AssertNode *p_assert);
		void print_assignment(AssignmentNode *p_assignment);
		void print_await(AwaitNode *p_await);
		void print_binary_op(BinaryOpNode *p_binary_op);
		void print_call(CallNode *p_call);
		void print_cast(CastNode *p_cast);
		void print_class(ClassNode *p_class);
		void print_constant(ConstantNode *p_constant);
		void print_dictionary(DictionaryNode *p_dictionary);
		void print_expression(ExpressionNode *p_expression);
		void print_enum(EnumNode *p_enum);
		void print_for(ForNode *p_for);
		void print_function(FunctionNode *p_function, const String &p_context = "Function");
		void print_get_node(GetNodeNode *p_get_node);
		void print_if(IfNode *p_if, bool p_is_elif = false);
		void print_identifier(IdentifierNode *p_identifier);
		void print_lambda(LambdaNode *p_lambda);
		void print_literal(LiteralNode *p_literal);
		void print_match(MatchNode *p_match);
		void print_match_branch(MatchBranchNode *p_match_branch);
		void print_match_pattern(PatternNode *p_match_pattern);
		void print_parameter(ParameterNode *p_parameter);
		void print_preload(PreloadNode *p_preload);
		void print_return(ReturnNode *p_return);
		void print_self(SelfNode *p_self);
		void print_signal(SignalNode *p_signal);
		void print_statement(Node *p_statement);
		void print_subscript(SubscriptNode *p_subscript);
		void print_suite(SuiteNode *p_suite);
		void print_ternary_op(TernaryOpNode *p_ternary_op);
		void print_tuple(TupleNode *p_tuple);
		void print_tuple_literal(TupleLiteralNode *p_tuple_literal);
		void print_type(TypeNode *p_type);
		void print_type_alias(TypeAliasNode *p_type_alias);
		void print_type_parameters(const Vector<TypeParameterNode *> &p_type_parameters);
		void print_type_test(TypeTestNode *p_type_test);
		void print_unary_op(UnaryOpNode *p_unary_op);
		void print_variable(VariableNode *p_variable);
		void print_variable_destructure(VariableDestructureNode *p_destructure);
		void print_while(WhileNode *p_while);

	public:
		/** The rendering `print_tree()` prints, returned instead of printed. */
		String render_tree(const BSParser &p_parser);
		void print_tree(const BSParser &p_parser);
	};
#endif // DEBUG_ENABLED
	static void cleanup();
	static void clear_builtin_type_cache();
};

} // namespace barista_script
