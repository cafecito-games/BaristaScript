/**************************************************************************/
/*  bs_script_instance.h                                                  */
/*                                                                        */
/*  The one script-instance vtable in the extension. godot-cpp ships no   */
/*  ScriptInstanceExtension wrapper, so the instance is a plain struct    */
/*  plus a static GDExtensionScriptInstanceInfo3 built by hand and given  */
/*  to script_instance_create3. Rewrite of FSInstance, foundry_script.h   */
/*  :924 @ c9d5e35.                                                       */
/*  Copyright (c) 2026-present Cafecito Games LLC.                        */
/*  This file is part of BaristaScript, a Godot GDExtension.              */
/*  SPDX-License-Identifier: MIT                                          */
/**************************************************************************/

#pragma once

#include "bs_function.h"
#include "bs_platform.h"

namespace barista_script {

class BaristaScript;

/**
 * One running instance of a compiled script, attached to one owner object.
 *
 * The engine reaches it only through the static vtable, whose every callback is filled: a null
 * slot the engine may call is a null-pointer call, so the vtable is asserted complete at startup
 * rather than discovered incomplete at a call site.
 *
 * Three things `ScriptInstance` has in the engine have no slot in the extension ABI. Reified type
 * arguments and the synthetic flag are stored here and read back through
 * `object_get_script_instance`; the RPC configuration is answered at script level, by
 * `BaristaScript::_get_rpc_config`.
 */
class BSInstance {
public:
	Object *owner = nullptr;
	Ref<BaristaScript> script;
	// A live derived instance keeps its compiled base chain alive even when the outer script that
	// originally owned those inner-class resources is released.
	Vector<Ref<BaristaScript>> retained_bases;
	Vector<Variant> members;

	/** Set while the implicit initializer and `_init` run, so a member read cannot see a half-built frame. */
	bool initializing = false;
	/** Instance-side storage for the two `ScriptInstance` questions the extension ABI cannot ask. */
	bool synthetic = false;
	Vector<BSParser::DataType> reified_type_arguments;

	BSInstance() = default;
	~BSInstance();

	/** The vtable every BaristaScript instance shares. Complete: no callback the engine may call is null. */
	static const GDExtensionScriptInstanceInfo3 *get_vtable();

	/** True when every callback slot the engine may call is filled. Checked at extension startup. */
	static bool vtable_is_complete();

	/** The instance attached to `p_owner`, or null when its script is not a BaristaScript. */
	static BSInstance *from_owner(const Object *p_owner);

	Variant call(const StringName &p_method, const Variant **p_arguments, int p_argument_count, GDExtensionCallError &r_error);
	void notification(int p_what, bool p_reversed);
	bool set_property(const StringName &p_name, const Variant &p_value);
	bool get_property(const StringName &p_name, Variant &r_value) const;
	bool validate_property(PropertyInfo &r_property);
	bool property_can_revert(const StringName &p_name);
	bool property_get_revert(const StringName &p_name, Variant &r_value);
	bool has_method(const StringName &p_name) const;
};

} // namespace barista_script
