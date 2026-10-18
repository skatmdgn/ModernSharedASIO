#pragma once

namespace modern_asio {
// Steinberg ASIO double-buffer contract:
// bufferSwitch(index) tells the host to WRITE index, while the OTHER
// previously prepared half is transferred to the output device.
// These halves must never be treated as interchangeable.
constexpr long asio_output_half_for_callback(long callback_half) noexcept {
    return callback_half==0 ? 1 : callback_half==1 ? 0 : -1;
}
}
