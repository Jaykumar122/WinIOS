#include "input/virtual_input_device.h"

#include <algorithm>
#include <cstring>

namespace winios::input {

VirtualInputDevice::VirtualInputDevice(std::uint32_t device_id)
    : device_id_(device_id) {
    reset();
}

void VirtualInputDevice::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    for (std::uint32_t i = 0; i < kMaxTouchContacts; ++i) {
        contacts_[i].contact_id = i;
        contacts_[i].x = 0;
        contacts_[i].y = 0;
        contacts_[i].pressure = 0.0f;
        contacts_[i].is_active = false;
    }
}

bool VirtualInputDevice::inject_touch_begin(
    std::uint32_t contact_id,
    std::int32_t x,
    std::int32_t y,
    std::uint64_t timestamp_us) {

    if (contact_id >= kMaxTouchContacts) {
        return false;
    }

    InputEvent evt{};
    evt.type = InputEventType::TouchBegin;
    evt.device_id = device_id_;
    evt.timestamp_us = timestamp_us;

    {
        std::lock_guard<std::mutex> guard(lock_);
        contacts_[contact_id].contact_id = contact_id;
        contacts_[contact_id].x = x;
        contacts_[contact_id].y = y;
        contacts_[contact_id].pressure = 1.0f;
        contacts_[contact_id].is_active = true;
        evt.touch = contacts_[contact_id];
    }

    if (dispatch_cb_) {
        dispatch_cb_(evt);
    }
    return true;
}

bool VirtualInputDevice::inject_touch_move(
    std::uint32_t contact_id,
    std::int32_t x,
    std::int32_t y,
    std::uint64_t timestamp_us) {

    if (contact_id >= kMaxTouchContacts) {
        return false;
    }

    InputEvent evt{};
    evt.type = InputEventType::TouchMove;
    evt.device_id = device_id_;
    evt.timestamp_us = timestamp_us;

    {
        std::lock_guard<std::mutex> guard(lock_);
        if (!contacts_[contact_id].is_active) {
            return false; // Cannot move an inactive contact
        }
        contacts_[contact_id].x = x;
        contacts_[contact_id].y = y;
        evt.touch = contacts_[contact_id];
    }

    if (dispatch_cb_) {
        dispatch_cb_(evt);
    }
    return true;
}

bool VirtualInputDevice::inject_touch_end(std::uint32_t contact_id, std::uint64_t timestamp_us) {
    if (contact_id >= kMaxTouchContacts) {
        return false;
    }

    InputEvent evt{};
    evt.type = InputEventType::TouchEnd;
    evt.device_id = device_id_;
    evt.timestamp_us = timestamp_us;

    {
        std::lock_guard<std::mutex> guard(lock_);
        if (!contacts_[contact_id].is_active) {
            return false;
        }
        contacts_[contact_id].is_active = false;
        evt.touch = contacts_[contact_id];
    }

    if (dispatch_cb_) {
        dispatch_cb_(evt);
    }
    return true;
}

bool VirtualInputDevice::inject_touch_cancel(std::uint32_t contact_id, std::uint64_t timestamp_us) {
    if (contact_id >= kMaxTouchContacts) {
        return false;
    }

    InputEvent evt{};
    evt.type = InputEventType::TouchCancel;
    evt.device_id = device_id_;
    evt.timestamp_us = timestamp_us;

    {
        std::lock_guard<std::mutex> guard(lock_);
        if (!contacts_[contact_id].is_active) {
            return false;
        }
        contacts_[contact_id].is_active = false;
        evt.touch = contacts_[contact_id];
    }

    if (dispatch_cb_) {
        dispatch_cb_(evt);
    }
    return true;
}

bool VirtualInputDevice::inject_key(std::uint32_t key_code, bool is_down, std::uint64_t timestamp_us) {
    InputEvent evt{};
    evt.type = is_down ? InputEventType::KeyPress : InputEventType::KeyRelease;
    evt.device_id = device_id_;
    evt.timestamp_us = timestamp_us;
    evt.key_code = key_code;

    if (dispatch_cb_) {
        dispatch_cb_(evt);
    }
    return true;
}

bool VirtualInputDevice::inject_home_button(std::uint64_t timestamp_us) {
    InputEvent evt{};
    evt.type = InputEventType::ButtonHome;
    evt.device_id = device_id_;
    evt.timestamp_us = timestamp_us;

    if (dispatch_cb_) {
        dispatch_cb_(evt);
    }
    return true;
}

bool VirtualInputDevice::inject_lock_button(std::uint64_t timestamp_us) {
    InputEvent evt{};
    evt.type = InputEventType::ButtonLock;
    evt.device_id = device_id_;
    evt.timestamp_us = timestamp_us;

    if (dispatch_cb_) {
        dispatch_cb_(evt);
    }
    return true;
}

bool VirtualInputDevice::is_contact_active(std::uint32_t contact_id) const noexcept {
    if (contact_id >= kMaxTouchContacts) return false;
    std::lock_guard<std::mutex> guard(lock_);
    return contacts_[contact_id].is_active;
}

TouchPoint VirtualInputDevice::get_contact(std::uint32_t contact_id) const noexcept {
    if (contact_id >= kMaxTouchContacts) return TouchPoint{};
    std::lock_guard<std::mutex> guard(lock_);
    return contacts_[contact_id];
}

std::uint32_t VirtualInputDevice::active_contact_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    std::uint32_t count = 0;
    for (std::uint32_t i = 0; i < kMaxTouchContacts; ++i) {
        if (contacts_[i].is_active) {
            count++;
        }
    }
    return count;
}

} // namespace winios::input
