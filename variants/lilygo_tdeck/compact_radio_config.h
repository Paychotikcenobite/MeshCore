#pragma once

// The global MeshCore Arduino base currently compiles with an EU default.
// Keep the upstream/reference targets untouched and replace those defaults
// only for the T-Deck Communicator target before any radio headers are parsed.
#ifdef LORA_FREQ
#undef LORA_FREQ
#endif
#define LORA_FREQ 910.525

#ifdef LORA_BW
#undef LORA_BW
#endif
#define LORA_BW 62.5

#ifdef LORA_SF
#undef LORA_SF
#endif
#define LORA_SF 7

#ifdef LORA_CR
#undef LORA_CR
#endif
#define LORA_CR 5
