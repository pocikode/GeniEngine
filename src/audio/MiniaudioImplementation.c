// miniaudio's implementation, compiled once (docs/design/0012). Encoding and the
// generators are not used by the engine.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
