// SPDX-License-Identifier: MIT
// Included in VrCover's private namespace: reuse its trace/judge/settings instead of another system.
ue3::FunctionRef gLe3GetMode{"SFXGame.BioPlayerController.GetGameModeDefault", ue3::kEveryGame};
ue3::FunctionRef gLe3FindCover{"SFXGame.SFXGameModeDefault.FindCover", ue3::kEveryGame};
ue3::FunctionRef gLe3SlotLocation{"Engine.CoverLink.GetSlotLocation", ue3::kEveryGame};
ue3::FunctionRef gLe3SlotRotation{"Engine.CoverLink.GetSlotRotation", ue3::kEveryGame};
ue3::FieldRef gLe3Found{gLe3FindCover, "FoundCover"};
ue3::FieldRef gLe3FoundLink{gLe3Found, "Link"};
ue3::FieldRef gLe3Left{gLe3Found, "LtSlotIdx"};
ue3::FieldRef gLe3Right{gLe3Found, "RtSlotIdx"};
ue3::ClassRef gLe3CoverClass{"Engine.CoverLink", ue3::kEveryGame};
ue3::FieldRef gLe3Slots{"Engine.CoverLink", "Slots", ue3::kEveryGame};
ue3::FieldRef gLe3Unsafe{gLe3Slots, "bUnsafeCover"};
ue3::FieldRef gLe3Enabled{gLe3Slots, "bEnabled"};
ue3::FieldRef gLe3CurrentLink{"SFXGame.BioPawn", "CurrentLink", ue3::kEveryGame};
ue3::FieldRef gLe3CurrentSlot{"SFXGame.BioPawn", "CurrentSlotIdx", ue3::kEveryGame};
struct Le3DamageFrame {
    std::uint64_t token = 0;
    ue3::Object* object = nullptr;
    ue3::Object* world = nullptr;
    ue3::Object* outer = nullptr;
    ue3::Class* cls = nullptr;
    ue3::Name name{};
    std::uint8_t const* locals = nullptr;
    int resumeParent = -1;
    vc::SlotLease lease;
    vc::Vec3 normal;
};
thread_local std::array<Le3DamageFrame, 8> gLe3Frames;
thread_local unsigned gLe3Depth = 0;
thread_local std::uint64_t gLe3Serial = 0;
Le3DamageFrame* Le3Frame() {
    return gLe3Depth && gLe3Depth <= gLe3Frames.size() ? &gLe3Frames[gLe3Depth - 1] : nullptr;
}
bool Le3SameObject(Le3DamageFrame const& frame) {
    if (!ue3::runtime::Up() || !ue3::runtime::IsAlive(frame.object)) return false;
    auto* pc = player::Controller();
    if (!ue3::runtime::IsAlive(pc) || player::BioPawn(pc) != frame.object ||
        gWorldInfo.Read<ue3::Object*>(pc).value_or(nullptr) != frame.world) return false;
    auto& r = ue3::runtime::Get();
    return r.NameOf(frame.object) == frame.name && r.ClassOf(frame.object) == frame.cls &&
        r.OuterOf(frame.object) == frame.outer && frame.world && ue3::runtime::IsAlive(frame.world) &&
        gWorldInfo.Read<ue3::Object*>(frame.object).value_or(nullptr) == frame.world;
}
// operation 0 ends, 1 suspends for a nested invocation, 2 resumes after that invocation.
bool Le3ChangeLeaseBody(Le3DamageFrame& frame, int operation) {
    if (!Le3SameObject(frame)) { frame.lease.active = frame.lease.suspended = false; return false; }
    auto** link = gLe3CurrentLink.In<ue3::Object*>(frame.object);
    auto* slot = gLe3CurrentSlot.In<std::int32_t>(frame.object);
    if (!link || !slot) { frame.lease.active = frame.lease.suspended = false; return false; }
    vc::SlotState state{reinterpret_cast<std::uintptr_t>(*link), *slot};
    bool const write = operation == 1 ? frame.lease.Suspend(state, true) :
        operation == 2 ? frame.lease.Resume(state, true) : frame.lease.Release(state, true);
    if (write) {
        *link = reinterpret_cast<ue3::Object*>(state.link); *slot = state.index;
        write_journal::Wrote("VrCover.LE3.link.scope", link);
        write_journal::Wrote("VrCover.LE3.slot.scope", slot);
    }
    return write;
}
bool Le3ChangeLease(Le3DamageFrame& frame, int operation) noexcept {
    // A destroyed native object during exceptional unwinding must not fault again from cleanup.
    __try { return Le3ChangeLeaseBody(frame, operation); }
    __except (EXCEPTION_EXECUTE_HANDLER) { frame.lease.active = frame.lease.suspended = false; return false; }
}
std::uint64_t Le3EnterDamage(ue3::Object* context, ue3::Function*) {
    int parent = -1;
    // A nested damage call sees native state, even if it exits before asking IsInCover.
    for (unsigned i = std::min<unsigned>(gLe3Depth, static_cast<unsigned>(gLe3Frames.size())); i; --i) {
        if (gLe3Frames[i - 1].lease.active && Le3ChangeLease(gLe3Frames[i - 1], 1)) { parent = static_cast<int>(i - 1); break; }
    }
    ++gLe3Depth;
    auto const token = ++gLe3Serial;
    if (auto* frame = Le3Frame()) {
        *frame = {}; frame->token = token; frame->object = context; frame->resumeParent = parent;
    }
    return token;
}
void Le3LeaveDamage(ue3::Object* context, ue3::Function*, std::uint64_t token) noexcept {
    if (auto* frame = Le3Frame(); frame && frame->token == token && frame->object == context) {
        int const parent = frame->resumeParent;
        if (frame->lease.active) Le3ChangeLease(*frame, 0);
        *frame = {};
        if (parent >= 0 && parent < static_cast<int>(gLe3Frames.size())) Le3ChangeLease(gLe3Frames[parent], 2);
    }
    if (gLe3Depth) --gLe3Depth;
}
void PutBack() { /* LE3 restoration belongs to its exact __finally scope, never an unrelated post-handler/tick. */ }

bool Le3Finite(ue3::Vector v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
std::optional<vc::SlotState> Le3QualifySlot(ue3::Object* pawn) {
    auto* pc = player::Controller();
    ue3::Params modeParams(ue3::runtime::Get(), gLe3GetMode.Get());
    if (!modeParams.Valid() || !ue3::runtime::Call(pc, gLe3GetMode.Get(), modeParams)) return std::nullopt;
    auto* mode = modeParams.Return<ue3::Object*>().value_or(nullptr);
    if (!ue3::runtime::IsAlive(mode) || ue3::runtime::Get().OuterOf(mode) != pc) return std::nullopt;
    ue3::Params find(ue3::runtime::Get(), gLe3FindCover.Get());
    if (!find.Valid() || !find.SetBool("bWideCheck", true) || !ue3::runtime::Call(mode, gLe3FindCover.Get(), find) ||
        find.ReturnBool() != true) return std::nullopt;
    auto* info = gLe3Found.Address(find.Data());
    auto* link = gLe3FoundLink.Read<ue3::Object*>(info).value_or(nullptr);
    if (!ue3::runtime::IsAlive(link) || !ue3::IsA(link, gLe3CoverClass) ||
        (ue3::runtime::Get().FlagsOf(link) & 0x200) != 0 ||
        gWorldInfo.Read<ue3::Object*>(link) != gWorldInfo.Read<ue3::Object*>(pawn)) return std::nullopt;
    auto slots = gLe3Slots.ElementsOf(link, 4096);
    auto at = gLocation.Read<ue3::Vector>(pawn);
    auto view = view_anchor::Current();
    if (!slots || !at || !Le3Finite(*at) || !view || !std::isfinite(view->unitsPerMeter) || view->unitsPerMeter <= 0) return std::nullopt;
    int const left = gLe3Left.Read<std::int32_t>(info).value_or(-1);
    int const right = gLe3Right.Read<std::int32_t>(info).value_or(-1);
    // Both authored endpoints must be valid/safe; never index a default object's empty array.
    if (!slots->At(left) || !slots->At(right) || gLe3Unsafe.ReadBool(slots->At(left)) != false ||
        gLe3Unsafe.ReadBool(slots->At(right)) != false || gLe3Enabled.ReadBool(slots->At(left)) != true ||
        gLe3Enabled.ReadBool(slots->At(right)) != true) return std::nullopt;
    double nearest = gSettings.reachM * view->unitsPerMeter;
    std::optional<vc::SlotState> selected;
    for (int index : {left, right}) {
        ue3::Params location(ue3::runtime::Get(), gLe3SlotLocation.Get());
        if (!location.Valid() || !location.Set("SlotIdx", index) ||
            !ue3::runtime::Call(link, gLe3SlotLocation.Get(), location)) continue;
        auto there = location.Return<ue3::Vector>();
        if (!there || !Le3Finite(*there)) continue;
        double const distance = std::hypot(there->x - at->x, there->y - at->y, there->z - at->z);
        if (distance <= nearest) { nearest = distance; selected = vc::SlotState{reinterpret_cast<std::uintptr_t>(link), index}; }
    }
    return selected;
}
bool Le3AnswerCover(ue3::Object* context, ue3::Function*, ue3::Function* caller, std::uint8_t* locals, void* result) {
    auto* frame = Le3Frame();
    if (!frame || gLe3Depth >= gLe3Frames.size() || frame->object != context || !locals || !result || caller != gDamageFn.Get() ||
        context != gPlayerPawn || !gSettings.on || !posture::RefusingCover() || !posture::PhysicalMovementAllowed()) return false;
    if (frame->lease.active) return false; // no second borrow in the same invocation
    if (CallBool(context, gIsInCoverFn)) { ++gCounts.gameCover; return false; }
    auto verdict = JudgeHit(context, locals);
    if (!verdict.Covered() || !std::isfinite(verdict.coverM) ||
        !std::isfinite(verdict.coverNormal.x) || !std::isfinite(verdict.coverNormal.y)) return false;
    auto const slot = Le3QualifySlot(context);
    if (!slot) { gLast += "; no qualified safe native slot (native damage retained)"; return false; }
    auto** link = gLe3CurrentLink.In<ue3::Object*>(context);
    auto* index = gLe3CurrentSlot.In<std::int32_t>(context);
    if (!link || !index || *link != nullptr) return false; // never replace an existing native link or restore a stale original pointer
    auto& r = ue3::runtime::Get();
    frame->world = gWorldInfo.Read<ue3::Object*>(context).value_or(nullptr);
    frame->name = r.NameOf(context); frame->cls = r.ClassOf(context); frame->outer = r.OuterOf(context);
    if (!Le3SameObject(*frame)) return false;
    vc::SlotState state{reinterpret_cast<std::uintptr_t>(*link), *index};
    if (!frame->lease.Acquire(state, *slot)) return false;
    frame->locals = locals; frame->normal = verdict.coverNormal;
    *link = reinterpret_cast<ue3::Object*>(state.link); *index = state.index;
    write_journal::Wrote("VrCover.LE3.link.borrow", link); write_journal::Wrote("VrCover.LE3.slot.borrow", index);
    *static_cast<std::uint32_t*>(result) = 1;
    gLast += "; qualified native slot borrowed for this damage scope";
    return true;
}
bool Le3StillBorrowed(Le3DamageFrame const& frame) {
    return frame.lease.active && Le3SameObject(frame) &&
        reinterpret_cast<std::uintptr_t>(gLe3CurrentLink.Read<ue3::Object*>(frame.object).value_or(nullptr)) == frame.lease.written.link &&
        gLe3CurrentSlot.Read<std::int32_t>(frame.object).value_or(-1) == frame.lease.written.index;
}
bool Le3AnswerLeaning(ue3::Object* context, ue3::Function*, ue3::Function* caller, std::uint8_t* locals, void* result) {
    auto* frame = Le3Frame();
    if (!frame || !Le3StillBorrowed(*frame) || frame->object != context || caller != gDamageFn.Get() ||
        frame->locals != locals || !result) return false;
    *static_cast<std::uint32_t*>(result) = 0; return true;
}
void Le3AnswerFacing(ue3::Object* context, ue3::Function*, ue3::Function* caller, std::uint8_t* locals, void* result) {
    auto* frame = Le3Frame();
    if (!frame || !Le3StillBorrowed(*frame) || reinterpret_cast<std::uintptr_t>(context) != frame->lease.written.link ||
        caller != gDamageFn.Get() || frame->locals != locals || !result) return;
    double const yaw = std::atan2(-frame->normal.y, -frame->normal.x);
    ue3::Rotator rotation{0, static_cast<std::int32_t>(std::lround(yaw * 32768.0 / std::numbers::pi)), 0};
    std::memcpy(result, &rotation, sizeof(rotation));
    write_journal::Wrote("VrCover.LE3.slotFacing", result, sizeof(rotation));
}
bool Le3FieldShape(ue3::FieldRef& ref, ue3::Kind kind, int size) {
    auto* field = ref.Get();
    return field && field->kind == kind && field->elementSize == size && field->arrayDim == 1;
}
bool Le3QueryShape(ue3::FunctionRef& ref) {
    ue3::Params p(ue3::runtime::Get(), ref.Get());
    auto* index = p.Find("SlotIdx"); auto* offset = p.Find("bForceUseOffset"); auto* result = p.ReturnSpec();
    return p.Valid() && p.Size() == 20 && p.Specs().size() == 3 &&
        index && index->kind == ue3::Kind::Int && index->offset == 0 && index->size == 4 &&
        offset && offset->kind == ue3::Kind::Bool && offset->offset == 4 && offset->size == 4 &&
        result && result->kind == ue3::Kind::Struct && result->offset == 8 && result->size == 12;
}
bool Le3Startup() {
    if (gStarted) return true;
    if (!ue3::runtime::Up()) return false;
    auto boolOnly = [](ue3::FunctionRef& fn) {
        ue3::Params p(ue3::runtime::Get(), fn.Get()); auto* value = p.ReturnSpec();
        return p.Valid() && p.Size() == 4 && p.Specs().size() == 1 && value && value->kind == ue3::Kind::Bool && value->size == 4;
    };
    if (!gDamageFn.Get() || !boolOnly(gIsInCoverFn) || !boolOnly(gIsInCoverLeaningFn) ||
        !gInstigatedBy.Get() || !gHitLocation.Get() || !gCauser.Get() || !gTraceFn.Get() || !gPawnClass.Get() ||
        !gLe3GetMode.Get() || !gLe3FindCover.Get() || !gLe3SlotLocation.Get() || !gLe3SlotRotation.Get() ||
        !gLe3FoundLink.Get() || !gLe3Left.Get() || !gLe3Right.Get() || !gLe3Slots.Get() || !gLe3Unsafe.Get() || !gLe3Enabled.Get() ||
        !gLe3CurrentLink.Get() || !gLe3CurrentSlot.Get() || !gWorldInfo.Get()) return false;
    ue3::Params find(ue3::runtime::Get(), gLe3FindCover.Get());
    auto* wide = find.Find("bWideCheck"); auto* result = find.ReturnSpec();
    ue3::Params mode(ue3::runtime::Get(), gLe3GetMode.Get());
    auto* modeReturn = mode.ReturnSpec();
    auto* inner = gLe3Slots.Inner();
    auto* unsafe = gLe3Unsafe.Get(); auto* enabled = gLe3Enabled.Get();
    if (!find.Valid() || find.Size() != 64 || find.Specs().size() != 3 ||
        !wide || wide->kind != ue3::Kind::Bool || wide->offset != 56 || wide->size != 4 ||
        !result || result->kind != ue3::Kind::Bool || result->offset != 60 || result->size != 4 ||
        !mode.Valid() || mode.Size() != 8 || mode.Specs().size() != 1 || !modeReturn || modeReturn->kind != ue3::Kind::Object ||
        modeReturn->size != 8 || modeReturn->offset != 0 ||
        !Le3QueryShape(gLe3SlotLocation) || !Le3QueryShape(gLe3SlotRotation) ||
        !Le3FieldShape(gLe3Found, ue3::Kind::Struct, 56) || !Le3FieldShape(gLe3FoundLink, ue3::Kind::Object, 8) ||
        !Le3FieldShape(gLe3Left, ue3::Kind::Int, 4) || !Le3FieldShape(gLe3Right, ue3::Kind::Int, 4) ||
        !Le3FieldShape(gLe3CurrentLink, ue3::Kind::Object, 8) || !Le3FieldShape(gLe3CurrentSlot, ue3::Kind::Int, 4) ||
        !Le3FieldShape(gInstigatedBy, ue3::Kind::Object, 8) || !Le3FieldShape(gCauser, ue3::Kind::Object, 8) ||
        !Le3FieldShape(gHitLocation, ue3::Kind::Struct, 12) || !inner || inner->kind != ue3::Kind::Struct ||
        unsafe->kind != ue3::Kind::Bool || enabled->kind != ue3::Kind::Bool || unsafe->offset < 0 || enabled->offset < 0 ||
        unsafe->offset + 4 > inner->elementSize || enabled->offset + 4 > inner->elementSize) return false;
    if (!engine::call_function::AddScopedHandler(gDamageFn.Get(), &Le3EnterDamage, &Le3LeaveDamage)) return false;
    engine::call_function::AddAnswerHandler(gIsInCoverFn.Get(), &Le3AnswerCover);
    engine::call_function::AddAnswerHandler(gIsInCoverLeaningFn.Get(), &Le3AnswerLeaning);
    engine::call_function::AddFramePostHandler(gLe3SlotRotation.Get(), &Le3AnswerFacing);
    gStarted = gHandlersReady = true;
    log::Info("vr cover LE3: scoped real-slot candidate registered; explicit 'mevr cover snap off' required; native acceptance pending");
    return true;
}
