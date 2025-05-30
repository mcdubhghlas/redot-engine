/**************************************************************************/
/*  embedded_process.cpp                                                  */
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

#include "embedded_process.h"

#include "core/config/project_settings.h"
#include "editor/editor_string_names.h"
#include "scene/main/window.h"
#include "scene/resources/style_box_flat.h"
#include "scene/theme/theme_db.h"

void EmbeddedProcessBase::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			ProjectSettings::get_singleton()->connect("settings_changed", callable_mp(this, &EmbeddedProcessBase::_project_settings_changed));
		} break;
		case NOTIFICATION_ENTER_TREE: {
			window = get_window();
			transp_enabled = GLOBAL_GET("display/window/per_pixel_transparency/allowed");
			clear_color = GLOBAL_GET("rendering/environment/defaults/default_clear_color");
		} break;
		case NOTIFICATION_DRAW: {
			_draw();
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			checkerboard = get_editor_theme_icon(SNAME("Checkerboard"));
			focus_style_box = get_theme_stylebox(SNAME("FocusViewport"), EditorStringName(EditorStyles));
			Ref<StyleBoxFlat> focus_style_box_flat = focus_style_box;
			if (focus_style_box_flat.is_valid()) {
				margin_top_left = Point2i(focus_style_box_flat->get_border_width(SIDE_LEFT), focus_style_box_flat->get_border_width(SIDE_TOP));
				margin_bottom_right = Point2i(focus_style_box_flat->get_border_width(SIDE_RIGHT), focus_style_box_flat->get_border_width(SIDE_BOTTOM));
			} else if (focus_style_box.is_valid()) {
				margin_top_left = Point2i(focus_style_box->get_margin(SIDE_LEFT), focus_style_box->get_margin(SIDE_TOP));
				margin_bottom_right = Point2i(focus_style_box->get_margin(SIDE_RIGHT), focus_style_box->get_margin(SIDE_BOTTOM));
			} else {
				margin_top_left = Point2i();
				margin_bottom_right = Point2i();
			}
		} break;
	}
}

void EmbeddedProcessBase::_project_settings_changed() {
	transp_enabled = GLOBAL_GET("display/window/per_pixel_transparency/allowed");
	clear_color = GLOBAL_GET("rendering/environment/defaults/default_clear_color");
	queue_redraw();
}

void EmbeddedProcessBase::_bind_methods() {
	ADD_SIGNAL(MethodInfo("embedding_completed"));
	ADD_SIGNAL(MethodInfo("embedding_failed"));
	ADD_SIGNAL(MethodInfo("embedded_process_updated"));
	ADD_SIGNAL(MethodInfo("embedded_process_focused"));
}

void EmbeddedProcessBase::_draw() {
	if (is_embedding_completed()) {
		Rect2 r = get_adjusted_embedded_window_rect(get_rect());
#ifndef MACOS_ENABLED
		r.position -= get_window()->get_position();
#endif
		if (transp_enabled) {
			draw_texture_rect(checkerboard, r, true);
		} else {
			draw_rect(r, clear_color, true);
		}
	}
	if (is_process_focused() && focus_style_box.is_valid()) {
		Size2 size = get_size();
		Rect2 r = Rect2(Point2(), size);
		focus_style_box->draw(get_canvas_item(), r);
	}
}

void EmbeddedProcessBase::set_window_size(const Size2i &p_window_size) {
	if (window_size != p_window_size) {
		window_size = p_window_size;
		queue_update_embedded_process();
		queue_redraw();
	}
}

void EmbeddedProcessBase::set_keep_aspect(bool p_keep_aspect) {
	if (keep_aspect != p_keep_aspect) {
		keep_aspect = p_keep_aspect;
		queue_update_embedded_process();
		queue_redraw();
	}
}

Rect2i EmbeddedProcessBase::get_screen_embedded_window_rect() const {
	return get_adjusted_embedded_window_rect(get_global_rect());
}

int EmbeddedProcessBase::get_margin_size(Side p_side) const {
	ERR_FAIL_INDEX_V((int)p_side, 4, 0);

	switch (p_side) {
		case SIDE_LEFT:
			return margin_top_left.x;
		case SIDE_RIGHT:
			return margin_bottom_right.x;
		case SIDE_TOP:
			return margin_top_left.y;
		case SIDE_BOTTOM:
			return margin_bottom_right.y;
	}

	return 0;
}

Size2 EmbeddedProcessBase::get_margins_size() const {
	return margin_top_left + margin_bottom_right;
}

EmbeddedProcessBase::EmbeddedProcessBase() {
	set_focus_mode(FOCUS_ALL);
}

EmbeddedProcessBase::~EmbeddedProcessBase() {
}

Rect2i EmbeddedProcess::get_adjusted_embedded_window_rect(const Rect2i &p_rect) const {
	Rect2i control_rect = Rect2i(p_rect.position + margin_top_left, (p_rect.size - get_margins_size()).maxi(1));
	if (window) {
		control_rect.position += window->get_position();
	}
	if (window_size != Size2i()) {
		Rect2i desired_rect;
		if (!keep_aspect && control_rect.size.x >= window_size.x && control_rect.size.y >= window_size.y) {
			// Fixed at the desired size.
			desired_rect.size = window_size;
		} else {
			float ratio = MIN((float)control_rect.size.x / window_size.x, (float)control_rect.size.y / window_size.y);
			desired_rect.size = Size2i(window_size.x * ratio, window_size.y * ratio).maxi(1);
		}
		desired_rect.position = Size2i(control_rect.position.x + ((control_rect.size.x - desired_rect.size.x) / 2), control_rect.position.y + ((control_rect.size.y - desired_rect.size.y) / 2));
		return desired_rect;
	} else {
		// Stretch, use all the control area.
		return control_rect;
	}
}

bool EmbeddedProcess::is_embedding_in_progress() const {
	return !timer_embedding->is_stopped();
}

bool EmbeddedProcess::is_embedding_completed() const {
	return embedding_completed;
}

bool EmbeddedProcess::is_process_focused() const {
	return focused_process_id == current_process_id && has_focus();
}

int EmbeddedProcess::get_embedded_pid() const {
	return current_process_id;
}

void EmbeddedProcess::embed_process(OS::ProcessID p_pid) {
	if (!window) {
		return;
	}

	ERR_FAIL_COND_MSG(!DisplayServer::get_singleton()->has_feature(DisplayServer::FEATURE_WINDOW_EMBEDDING), "Embedded process not supported by this display server.");

	if (current_process_id != 0) {
		// Stop embedding the last process.
		OS::get_singleton()->kill(current_process_id);
	}

	reset();

	current_process_id = p_pid;
	start_embedding_time = OS::get_singleton()->get_ticks_msec();
	embedding_grab_focus = has_focus();
	timer_update_embedded_process->start();
	set_process(true);
	set_notify_transform(true);

	// Attempt to embed the process, but if it has just started and the window is not ready yet,
	// we will retry in this case.
	_try_embed_process();
}

void EmbeddedProcess::reset() {
	if (current_process_id != 0 && embedding_completed) {
		DisplayServer::get_singleton()->remove_embedded_process(current_process_id);
	}
	current_process_id = 0;
	embedding_completed = false;
	start_embedding_time = 0;
	embedding_grab_focus = false;
	timer_embedding->stop();
	timer_update_embedded_process->stop();
	set_process(false);
	set_notify_transform(false);
	queue_redraw();
}

void EmbeddedProcess::request_close() {
	if (current_process_id != 0 && embedding_completed) {
		DisplayServer::get_singleton()->request_close_embedded_process(current_process_id);
	}
}

void EmbeddedProcess::_try_embed_process() {
	bool is_visible = is_visible_in_tree();
	Error err = DisplayServer::get_singleton()->embed_process(window->get_window_id(), current_process_id, get_screen_embedded_window_rect(), is_visible, is_visible && application_has_focus && embedding_grab_focus);
	if (err == OK) {
		embedding_completed = true;
		queue_redraw();
		emit_signal(SNAME("embedding_completed"));
	} else if (err == ERR_DOES_NOT_EXIST) {
		if (OS::get_singleton()->get_ticks_msec() - start_embedding_time >= (uint64_t)embedding_timeout) {
			// Embedding process timed out.
			reset();
			emit_signal(SNAME("embedding_failed"));
		} else {
			// Tries another shot.
			timer_embedding->start();
		}
	} else {
		// Another unknown error.
		reset();
		emit_signal(SNAME("embedding_failed"));
	}
}

bool EmbeddedProcess::_is_embedded_process_updatable() {
	return window && current_process_id != 0 && embedding_completed;
}

void EmbeddedProcess::queue_update_embedded_process() {
	updated_embedded_process_queued = true;
}

void EmbeddedProcess::_timer_update_embedded_process_timeout() {
	_check_focused_process_id();
	_check_mouse_over();

	if (!updated_embedded_process_queued) {
		// We need to detect when the control globally changes location or size on the screen.
		// NOTIFICATION_RESIZED and NOTIFICATION_WM_POSITION_CHANGED are not enough to detect
		// resized parent to siblings controls that can affect global position.
		Rect2i new_global_rect = get_global_rect();
		if (last_global_rect != new_global_rect) {
			last_global_rect = new_global_rect;
			queue_update_embedded_process();
		}
	}
}

void EmbeddedProcess::_update_embedded_process() {
	if (!_is_embedded_process_updatable()) {
		return;
	}

	bool must_grab_focus = false;
	bool focus = has_focus();
	if (last_updated_embedded_process_focused != focus) {
		if (focus) {
			must_grab_focus = true;
		}
		last_updated_embedded_process_focused = focus;
	}

	DisplayServer::get_singleton()->embed_process(window->get_window_id(), current_process_id, get_screen_embedded_window_rect(), is_visible_in_tree(), must_grab_focus);
	emit_signal(SNAME("embedded_process_updated"));
}

void EmbeddedProcess::_timer_embedding_timeout() {
	_try_embed_process();
}

void EmbeddedProcess::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS: {
			if (updated_embedded_process_queued) {
				updated_embedded_process_queued = false;
				_update_embedded_process();
			}
		} break;
		case NOTIFICATION_RESIZED:
		case NOTIFICATION_VISIBILITY_CHANGED:
		case NOTIFICATION_WM_POSITION_CHANGED: {
			queue_update_embedded_process();
		} break;
		case NOTIFICATION_APPLICATION_FOCUS_IN: {
			application_has_focus = true;
			last_application_focus_time = OS::get_singleton()->get_ticks_msec();
		} break;
		case NOTIFICATION_APPLICATION_FOCUS_OUT: {
			application_has_focus = false;
		} break;
		case NOTIFICATION_FOCUS_ENTER: {
			queue_update_embedded_process();
		} break;
	}
}

void EmbeddedProcess::_check_mouse_over() {
	// This method checks if the mouse is over the embedded process while the current application is focused.
	// The goal is to give focus to the embedded process as soon as the mouse hovers over it,
	// allowing the user to interact with it immediately without needing to click first.
	if (!embedding_completed || !application_has_focus || !window || has_focus() || !is_visible_in_tree() || !window->has_focus() || Input::get_singleton()->is_mouse_button_pressed(MouseButton::LEFT) || Input::get_singleton()->is_mouse_button_pressed(MouseButton::RIGHT)) {
		return;
	}

	// Before checking whether the mouse is truly inside the embedded process, ensure
	// the editor has enough time to re-render. When a breakpoint is hit in the script editor,
	// `_check_mouse_over` may be triggered before the editor hides the game workspace.
	// This prevents the embedded process from regaining focus immediately after the editor has taken it.
	if (OS::get_singleton()->get_ticks_msec() - last_application_focus_time < 500) {
		return;
	}

	// Input::is_mouse_button_pressed is not sufficient to detect the mouse button state
	// while the floating game window is being resized.
	BitField<MouseButtonMask> mouse_button_mask = DisplayServer::get_singleton()->mouse_get_button_state();
	if (!mouse_button_mask.is_empty()) {
		return;
	}

	// Not stealing focus from a textfield.
	if (get_viewport()->gui_get_focus_owner() && get_viewport()->gui_get_focus_owner()->is_text_field()) {
		return;
	}

	Vector2 mouse_position = DisplayServer::get_singleton()->mouse_get_position();
	Rect2i window_rect = get_screen_embedded_window_rect();
	if (!window_rect.has_point(mouse_position)) {
		return;
	}

	// Don't grab the focus if mouse over another window.
	DisplayServer::WindowID window_id_over = DisplayServer::get_singleton()->get_window_at_screen_position(mouse_position);
	if (window_id_over > 0 && window_id_over != window->get_window_id()) {
		return;
	}

	// Check if there's an exclusive popup, an open menu, or a tooltip.
	// We don't want to grab focus to prevent the game window from coming to the front of the modal window
	// or the open menu from closing when the mouse cursor moves outside the menu and over the embedded game.
	Vector<DisplayServer::WindowID> wl = DisplayServer::get_singleton()->get_window_list();
	for (const DisplayServer::WindowID &window_id : wl) {
		Window *w = Window::get_from_id(window_id);
		if (w && (w->is_exclusive() || w->get_flag(Window::FLAG_POPUP))) {
			return;
		}
	}

	// Force "regrabbing" the game window focus.
	last_updated_embedded_process_focused = false;

	grab_focus();
	queue_redraw();
}

void EmbeddedProcess::_check_focused_process_id() {
	OS::ProcessID process_id = DisplayServer::get_singleton()->get_focused_process_id();
	if (process_id != focused_process_id) {
		focused_process_id = process_id;
		if (focused_process_id == current_process_id) {
			// The embedded process got the focus.

			// Refocus the current model when focusing the embedded process.
			Window *modal_window = _get_current_modal_window();
			if (!modal_window) {
				emit_signal(SNAME("embedded_process_focused"));
				if (has_focus()) {
					// Redraw to updated the focus style.
					queue_redraw();
				} else {
					grab_focus();
				}
			}
		} else if (has_focus()) {
			release_focus();
		}
	}

	// Ensure that the opened modal dialog is refocused when the focused process is the embedded process.
	if (!application_has_focus && focused_process_id == current_process_id) {
		Window *modal_window = _get_current_modal_window();
		if (modal_window) {
			if (modal_window->get_mode() == Window::MODE_MINIMIZED) {
				modal_window->set_mode(Window::MODE_WINDOWED);
			}
			callable_mp(modal_window, &Window::grab_focus).call_deferred();
		}
	}
}

Window *EmbeddedProcess::_get_current_modal_window() {
	Vector<DisplayServer::WindowID> wl = DisplayServer::get_singleton()->get_window_list();
	for (const DisplayServer::WindowID &window_id : wl) {
		Window *w = Window::get_from_id(window_id);
		if (!w) {
			continue;
		}

		if (w->is_exclusive()) {
			return w;
		}
	}
	return nullptr;
}

EmbeddedProcess::EmbeddedProcess() :
		EmbeddedProcessBase() {
	timer_embedding = memnew(Timer);
	timer_embedding->set_wait_time(0.1);
	timer_embedding->set_one_shot(true);
	add_child(timer_embedding);
	timer_embedding->connect("timeout", callable_mp(this, &EmbeddedProcess::_timer_embedding_timeout));

	timer_update_embedded_process = memnew(Timer);
	timer_update_embedded_process->set_wait_time(0.1);
	add_child(timer_update_embedded_process);
	timer_update_embedded_process->connect("timeout", callable_mp(this, &EmbeddedProcess::_timer_update_embedded_process_timeout));
}

EmbeddedProcess::~EmbeddedProcess() {
	if (current_process_id != 0) {
		// Stop embedding the last process.
		OS::get_singleton()->kill(current_process_id);
		reset();
	}
}
