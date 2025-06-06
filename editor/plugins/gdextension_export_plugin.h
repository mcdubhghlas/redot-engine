/**************************************************************************/
/*  gdextension_export_plugin.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             REDOT ENGINE                               */
/*                        https://redotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2024-present Redot Engine contributors                   */
/*                                          (see REDOT_AUTHORS.md)        */
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/extension/gdextension_library_loader.h"
#include "editor/export/editor_export.h"

class GDExtensionExportPlugin : public EditorExportPlugin {
protected:
	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features);
	virtual String get_name() const { return "GDExtension"; }
};

void GDExtensionExportPlugin::_export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) {
	if (p_type != "GDExtension") {
		return;
	}

	Ref<ConfigFile> config;
	config.instantiate();

	Error err = config->load(p_path);
	ERR_FAIL_COND_MSG(err, "Failed to load GDExtension file: " + p_path);

	// Check whether this GDExtension should be exported.
	bool android_aar_plugin = config->get_value("configuration", "android_aar_plugin", false);
	if (android_aar_plugin && p_features.has("android")) {
		// The gdextension configuration and Android .so files will be provided by the Android aar
		// plugin it's part of, so we abort here.
		skip();
		return;
	}

	ERR_FAIL_COND_MSG(!config->has_section_key("configuration", "entry_symbol"), "Failed to export GDExtension file, missing entry symbol: " + p_path);

	String entry_symbol = config->get_value("configuration", "entry_symbol");

	HashSet<String> all_archs;
	all_archs.insert("x86_32");
	all_archs.insert("x86_64");
	all_archs.insert("arm32");
	all_archs.insert("arm64");
	all_archs.insert("rv64");
	all_archs.insert("ppc32");
	all_archs.insert("ppc64");
	all_archs.insert("wasm32");
	all_archs.insert("loongarch64");
	all_archs.insert("universal");

	HashSet<String> archs;
	HashSet<String> features_wo_arch;
	Vector<String> features_vector;
	for (const String &tag : p_features) {
		if (all_archs.has(tag)) {
			archs.insert(tag);
		} else {
			features_wo_arch.insert(tag);
		}
		features_vector.append(tag);
	}

	if (archs.is_empty()) {
		archs.insert("unknown_arch"); // Not archs specified, still try to match.
	}

	HashSet<String> libs_added;
	struct FoundLibInfo {
		int count = 0;
		Vector<String> libs;
	};
	HashMap<String, FoundLibInfo> libs_found;
	for (const String &arch_tag : archs) {
		if (arch_tag != "universal") {
			libs_found[arch_tag] = FoundLibInfo();
		}
	}

	for (const String &arch_tag : archs) {
		PackedStringArray tags;
		String library_path = GDExtensionLibraryLoader::find_extension_library(
				p_path, config, [features_wo_arch, arch_tag](const String &p_feature) { return features_wo_arch.has(p_feature) || (p_feature == arch_tag); }, &tags);

		if (libs_added.has(library_path)) {
			continue; // Universal library, already added for another arch, do not duplicate.
		}
		if (!library_path.is_empty()) {
			libs_added.insert(library_path);
			add_shared_object(library_path, tags);

			if (p_features.has("apple_embedded") && (library_path.ends_with(".a") || library_path.ends_with(".xcframework"))) {
				String additional_code = "extern void register_dynamic_symbol(char *name, void *address);\n"
										 "extern void add_apple_embedded_platform_init_callback(void (*cb)());\n"
										 "\n"
										 "extern \"C\" void $ENTRY();\n"
										 "void $ENTRY_init() {\n"
										 "  if (&$ENTRY) register_dynamic_symbol((char *)\"$ENTRY\", (void *)$ENTRY);\n"
										 "}\n"
										 "struct $ENTRY_struct {\n"
										 "  $ENTRY_struct() {\n"
										 "    add_apple_embedded_platform_init_callback($ENTRY_init);\n"
										 "  }\n"
										 "};\n"
										 "$ENTRY_struct $ENTRY_struct_instance;\n\n";
				additional_code = additional_code.replace("$ENTRY", entry_symbol);
				add_apple_embedded_platform_cpp_code(additional_code);

				String linker_flags = "-Wl,-U,_" + entry_symbol;
				add_apple_embedded_platform_linker_flags(linker_flags);
			}

			// Update found library info.
			if (arch_tag == "universal") {
				for (const String &sub_arch_tag : archs) {
					if (sub_arch_tag != "universal") {
						libs_found[sub_arch_tag].count++;
						libs_found[sub_arch_tag].libs.push_back(library_path);
					}
				}
			} else {
				libs_found[arch_tag].count++;
				libs_found[arch_tag].libs.push_back(library_path);
			}
		}

		Vector<SharedObject> dependencies_shared_objects = GDExtensionLibraryLoader::find_extension_dependencies(p_path, config, [p_features](String p_feature) { return p_features.has(p_feature); });
		for (const SharedObject &shared_object : dependencies_shared_objects) {
			_add_shared_object(shared_object);
		}
	}

	for (const KeyValue<String, FoundLibInfo> &E : libs_found) {
		if (E.value.count == 0) {
			if (get_export_platform().is_valid()) {
				get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_WARNING, TTR("GDExtension"), vformat(TTR("No \"%s\" library found for GDExtension: \"%s\". Possible feature flags for your platform: %s"), E.key, p_path, String(", ").join(features_vector)));
			}
		} else if (E.value.count > 1) {
			if (get_export_platform().is_valid()) {
				get_export_platform()->add_message(EditorExportPlatform::EXPORT_MESSAGE_WARNING, TTR("GDExtension"), vformat(TTR("Multiple \"%s\" libraries found for GDExtension: \"%s\": \"%s\"."), E.key, p_path, String(", ").join(E.value.libs)));
			}
		}
	}
}
