#include "Soundengine/MIDI/ParameterArray.h"
#include "Soundengine/MIDI/MidiParser.h"
uint8_t activeParameters[4096];
Parameter parameterArray[END_OF_SOUND_PARAMETERS];
float adc_getFaderVolume(uint8_t idx){ (void)idx; return 1.0f; }
float midiParser_calcDetune(uint8_t value){ return 1.0f + (value/127.f-0.5f)*(1.0594630943592953f-1.f); }
void midiParser_ccHandler(MidiMsg msg, uint8_t u, uint8_t f, float v){ (void)msg;(void)u;(void)f;(void)v; }
void paramArray_setParameter(uint16_t idx, ptrValue nv){ (void)idx;(void)nv; }
uint16_t seq_getBpm(void){ return 120; }

uint8_t midiParser_originalCcValues[0xff];
