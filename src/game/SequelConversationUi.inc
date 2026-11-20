// SPDX-License-Identifier: MIT
// The subtitle under the speaker's face and the wheel in front of the player on the sequels (included by
// game/SequelConversationWorld.cpp inside its anonymous namespace, after HeldFrame). ME1's placement (research/59, game/
// ConversationWorld's state: the subtitle's point and the wheel's hand) with ME1's keys and defaults in [Cinematics]:
// wheelHand (front | left | right; front by default since the owner's QA of 2026-11-06), wheelWidthM 0.40, wheelDistanceM 0.12,
// subtitleHeightDeg 4, subtitleBelowM 0.22, subtitleTowardM 0.15, subtitleCrop / wheelCrop ("x y w h" of the frame; empty: the
// movie's own bands, cw::DefaultSubtitleCrop / DefaultWheelCrop, measured the same on ME2's and ME3's frames: lane FPC5). The
// host draws them (plugin/ConversationUi); Shepard's own lines go under the one he talks to.

struct ConversationUiSettings {
    int wheelHand = 2;
    float wheelWidthM = 0.40f;
    float wheelDistanceM = 0.12f;
    float subtitleHeightDeg = 4.0f;
    float subtitleBelowM = 0.22f;
    float subtitleTowardM = 0.15f;
    float subtitleCrop[4] = {};
    float wheelCrop[4] = {};
    std::uint64_t ms = 0;
};
ConversationUiSettings gUiSettings;

void ReadConversationUiSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gUiSettings.ms != 0 && now - gUiSettings.ms < 2000) return;
    ini::Loaded const loaded = vr::settings_file::Load(engine::SettingsFile());
    ConversationUiSettings s;
    s.ms = now;
    auto const number = [&](char const* key, float fallback, float low, float high) {
        auto const text = ini::Get(loaded.file, "Cinematics", key);
        auto const n = text ? ini::Number(*text) : std::nullopt;
        return static_cast<float>(std::clamp(n.value_or(fallback), static_cast<double>(low), static_cast<double>(high)));
    };
    if (auto const text = ini::Get(loaded.file, "Cinematics", "wheelHand")) {
        std::string const w = ini::Lower(*text);
        s.wheelHand = w == "right" || w == "1" ? 1 : w == "left" || w == "0" ? 0 : 2;
    }
    s.wheelWidthM = number("wheelWidthM", s.wheelWidthM, 0.15f, 1.5f);
    s.wheelDistanceM = number("wheelDistanceM", s.wheelDistanceM, 0.05f, 0.8f);
    s.subtitleHeightDeg = number("subtitleHeightDeg", s.subtitleHeightDeg, 0.5f, 20.0f);
    s.subtitleBelowM = number("subtitleBelowM", s.subtitleBelowM, 0.0f, 1.0f);
    s.subtitleTowardM = number("subtitleTowardM", s.subtitleTowardM, 0.0f, 1.0f);
    for (auto const& [key, out] : {std::pair<char const*, float*>{"subtitleCrop", s.subtitleCrop}, {"wheelCrop", s.wheelCrop}}) {
        if (auto const text = ini::Get(loaded.file, "Cinematics", key)) {
            std::istringstream words(*text);
            std::string word;
            for (int i = 0; i < 4 && words >> word; ++i) {
                if (auto const n = ini::Number(word)) out[i] = static_cast<float>(std::clamp(*n, 0.0, 1.0));
            }
        }
    }
    gUiSettings = s;
}

// The speaker whose line shows (a new one is logged by the host once), counted for the host's log.
ue3::Object* gUiSpeaker = nullptr;
std::uint32_t gUiSpeakerSerial = 0;

// A held conversation's subtitle and wheel facts into the host's state.
void FillConversationUi(ue3::Object* controller, cw::State& s) {
    ReadConversationUiSettings();
    s.wheel = WheelUp(controller) ? 1 : 0;
    s.wheelHand = static_cast<std::uint8_t>(gUiSettings.wheelHand);
    s.wheelWidthM = gUiSettings.wheelWidthM;
    s.wheelDistanceM = gUiSettings.wheelDistanceM;
    s.subtitleHeightDeg = gUiSettings.subtitleHeightDeg;
    std::copy(std::begin(gUiSettings.subtitleCrop), std::end(gUiSettings.subtitleCrop), std::begin(s.subtitleCrop));
    std::copy(std::begin(gUiSettings.wheelCrop), std::end(gUiSettings.wheelCrop), std::begin(s.wheelCrop));
    ue3::Object* const conversation = gC.controller;
    if (conversation == nullptr || !ue3::runtime::IsAlive(conversation)) return;
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    ue3::Object* const shepard = ConversationPlayer(conversation);
    ue3::Object* face = gSpeakerNow.Read<ue3::Object*>(conversation).value_or(nullptr);
    if (face == nullptr || face == pawn || face == shepard) face = gListenerNow.Read<ue3::Object*>(conversation).value_or(nullptr);
    if (face != gUiSpeaker) {
        gUiSpeaker = face;
        ++gUiSpeakerSerial;
    }
    s.speakerSerial = gUiSpeakerSerial;
    if (face == nullptr || face == pawn || face == shepard || !ue3::runtime::IsAlive(face)) return;
    auto const frame = HeldFrame();
    std::optional<HeadPose> const head = HeadPoseOf(face);
    auto const at = head ? std::optional<ue3::Vector>(ue3::Vector{static_cast<float>(head->at.x), static_cast<float>(head->at.y),
                                                                  static_cast<float>(head->at.z)})
                         : player::Location(face);
    if (!frame || !at) return;
    XrVector3f const faceT = engine::ToTracking(*frame, engine::UnrealVector{at->x, at->y, at->z});
    XrVector3f point{};
    if (cw::SubtitlePoint(faceT, frame->head.position, gUiSettings.subtitleBelowM, gUiSettings.subtitleTowardM, point)) {
        s.subtitle = 1;
        s.subtitleAt[0] = point.x;
        s.subtitleAt[1] = point.y;
        s.subtitleAt[2] = point.z;
    }
}
