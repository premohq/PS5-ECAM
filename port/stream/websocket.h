// SPDX-License-Identifier: GPL-3.0-or-later
// Playtendoh: the remote touch screen's WebSockets (stream.h), from dolphin78's prototype (#38).
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ps5stream
{
// Small RFC 6455 transport for the two local streaming endpoints. Client messages
// are capped at 512 bytes; server messages carry a single JPEG or a control frame.
class WebSocket
{
public:
	enum class Result { Idle, Text, Binary, Closed, Error };
	WebSocket(int socket, const std::function<bool()>& alive) : fd(socket), alive(alive) {}
	bool Upgrade(const std::string& request);
	bool Send(uint8_t opcode, const void* bytes, size_t size);
	Result Receive(std::string& message, int timeoutMs);
private:
	int fd;
	std::function<bool()> alive;
	std::vector<uint8_t> input, fragmented;
	uint8_t fragmentOpcode = 0;
};
bool SendBytes(int fd, const void* bytes, size_t size, const std::function<bool()>& alive);
void ConfigureSocket(int fd);
}
