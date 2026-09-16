/**************************************************************************/
/*  native_corpus_arguments.cpp                                           */
/*                                                                        */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#include "native_corpus_arguments.h"

#include <godot_cpp/core/memory.hpp>

using namespace godot;

namespace barista_script::native_tests {
namespace {
// Storage is allocated on first use and never destroyed. Construction must happen after the
// extension loads, and teardown must not happen at all: a `String` destructor running during
// library unload would call back into an engine that is already deinitializing.
// `BSAnalyzer::bootstrap_root_storage` uses the same shape for the same reason.
String &root_storage() {
	static String *root = nullptr;
	if (root == nullptr) {
		root = memnew(String);
	}
	return *root;
}

String &case_storage() {
	static String *name = nullptr;
	if (name == nullptr) {
		name = memnew(String);
	}
	return *name;
}

String &order_storage() {
	static String *order = nullptr;
	if (order == nullptr) {
		order = memnew(String);
	}
	return *order;
}
} //namespace

const String &corpus_root() {
	return root_storage();
}

const String &corpus_case() {
	return case_storage();
}

const String &corpus_order() {
	return order_storage();
}

bool whole_corpus_selected() {
	return !corpus_root().is_empty() && corpus_case().is_empty();
}

void set_corpus_arguments(const String &p_root, const String &p_case, const String &p_order) {
	root_storage() = p_root;
	case_storage() = p_case;
	order_storage() = p_order;
}

} //namespace barista_script::native_tests
