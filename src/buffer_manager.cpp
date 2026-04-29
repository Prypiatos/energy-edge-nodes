#include "buffer_manager.h"

#include "globals.h"

#include <cstring>

// Maximum number of outgoing messages that can be held in the in-memory buffer.
static constexpr std::size_t kBufferCapacity = 32;

static OutgoingMessage g_buffer[kBufferCapacity] = {};
static std::size_t g_buffer_count = 0;

void InitBufferManager() {
	std::memset(g_buffer, 0, sizeof(g_buffer));
	g_buffer_count = 0;
	g_system_state.buffered_count = 0;
}

// Accepts an unsent outgoing message and stores it for later retry.
// When the buffer is full, the oldest entry is evicted to make room.
bool EnqueueOutgoingMessage(const OutgoingMessage& msg) {
	if (g_buffer_count >= kBufferCapacity) {
		// Drop oldest to make room for the newest message.
		for (std::size_t index = 1; index < g_buffer_count; ++index) {
			g_buffer[index - 1] = g_buffer[index];
		}

		g_buffer_count = kBufferCapacity - 1;
	}

	g_buffer[g_buffer_count] = msg;
	++g_buffer_count;
	g_system_state.buffered_count = static_cast<std::uint32_t>(g_buffer_count);
	return true;
}

void RunBufferTask() {}
