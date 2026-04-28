#pragma once

#include "types.h"

void InitBufferManager();
void RunBufferTask();
bool EnqueueOutgoingMessage(const OutgoingMessage& msg);
