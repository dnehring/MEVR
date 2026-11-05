// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadContext.hpp"

#include <string>
#include <vector>

// The model tier's grammar (research/54 "Rules first, a fine-tuned sub-1B model second"): a GBNF grammar (llama.cpp's)
// rebuilt per scene that lets the language model emit only legal action lines, `WHO|WEAPON|POWER|ORDER|TARGET` as
// core/vr/SquadContext's Line writes them and ParseLine reads them, with every alternative taken from the scene: the
// squadmates present (and ALL), the weapon codes, the party's power codes, the eight orders, the groups' ids, the deictics
// said (PT1..PTn), AIM/GAZE/LAST/OTHER/SELF and the four directions; or `none` when nothing was ordered. So an invented
// name or a dead group can't come out of the model: the grammar's job is to make every answer parse, the validator's to
// refuse what the scene forbids. Unit-tested.
namespace mevr::voice::grammar {

struct Options {
    int deictics = 0;    // PT1..PTn allowed (how many deictic words the transcript had)
    int maxClauses = 3;  // "line ; line ; line" at most
};

std::string Gbnf(vr::squad_context::Scene const& scene, std::vector<vr::squad_context::Group> const& groups, Options const& options);
// The same grammar read off a text block (its PARTY letters and power codes, its E ids and PT lines), for tools that have
// the prompt but not the scene (mevr-llm-bench over a dataset).
std::string GbnfFromBlock(std::string_view block, int maxClauses = 3);

// The prompt around the scene block: the block (core/vr/SquadContext's Block, the SAY line included) and the answer's cue.
std::string Prompt(std::string const& block);

}  // namespace mevr::voice::grammar
