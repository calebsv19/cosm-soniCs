#pragma once
#include "engine/mixer_automation.h"
struct cJSON;
// Serializes complete immutable mixer automation with exact project frame identities.
void daw_mixer_automation_json(struct cJSON*, const EngineMixerAutomation*);
// Reads a strict bounded curve without accepting duplicate/unknown fields or numeric frame
// coercion.
// Reads a signed physical parameter lane with the same strict wire contract.
bool daw_parameter_automation_parse(const struct cJSON*,EngineMixerAutomation*,float,float);
bool daw_mixer_automation_parse(const struct cJSON*, EngineMixerAutomation*, float);
