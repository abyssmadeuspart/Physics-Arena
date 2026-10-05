#pragma once

// private completion requests share the candidate process lifetime, with one outstanding request
constexpr unsigned int kGuiFrameRequestMessage = 0x8000 + 0x341;
constexpr const wchar_t* kGuiFrameEventName = L"Local\\PhysicsArena.Frame.%lu.%lu.%lu";

constexpr unsigned long kGuiFrameInputTag = 0x50410000;
constexpr unsigned long kGuiFrameInputMask = 0xffff0000;
