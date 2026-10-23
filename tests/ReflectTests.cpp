// SPDX-License-Identifier: MIT
// Offline tests for our UE3 reflection layer (src/ue3, research/34): names, lookups, the property
// cache, bool masks and parameter blocks over a synthetic in-memory "engine" with its own layout
// (deliberately not LE1's: the layer must follow whatever Layout it's given).

#include "ue3/Names.hpp"
#include "ue3/Params.hpp"
#include "ue3/Pattern.hpp"
#include "ue3/Reflect.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ReflectTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

using namespace mevr;

ue3::Layout TestLayout() {
    ue3::Layout l;
    l.objectIndex = 0x08;
    l.objectFlags = 0x10;
    l.objectOuter = 0x18;
    l.objectName = 0x20;
    l.objectClass = 0x28;
    l.pendingKillFlag = 1ull << 40;
    l.processEventSlot = 0x10;
    l.fieldSuper = 0x30;
    l.fieldNext = 0x38;
    l.structChildren = 0x40;
    l.structPropertySize = 0x48;
    l.propertyArrayDim = 0x40;
    l.propertyElementSize = 0x44;
    l.propertyFlags = 0x48;
    l.propertyOffset = 0x50;
    l.propertyTyped = 0x58;
    l.functionFlags = 0x50;
    l.functionNative = 0x54;
    l.functionParmsSize = 0x56;
    l.functionReturnOffset = 0x58;
    l.functionFunc = 0x60;
    l.namePoolShift = 29;
    l.namePoolMask = 7;
    l.nameOffsetMask = 0x1FFFFFFF;
    l.nameChars = 0x0C;
    l.nameWideFlag = 0x40000000;
    return l;
}

// A tiny engine: a name pool, objects in 0x80-byte blocks, GObjects over them.
struct Fake {
    static constexpr std::size_t kObjectSize = 0x80;

    ue3::Layout layout = TestLayout();
    std::vector<std::uint8_t> pool = std::vector<std::uint8_t>(16);  // offset 0 unused (index 0 = None)
    std::uintptr_t bases[2]{};
    std::vector<std::unique_ptr<std::uint64_t[]>> storage;
    std::vector<ue3::Object*> objects;
    ue3::TArray<ue3::Object*> array;

    std::int32_t Name(std::string const& text, bool wide = false) {
        while (pool.size() % 4 != 0) {
            pool.push_back(0);
        }
        std::size_t const at = pool.size();
        std::uint32_t const header = (wide ? layout.nameWideFlag : 0u) | (static_cast<std::uint32_t>(text.size()) << 20);
        pool.resize(at + layout.nameChars);
        std::memcpy(pool.data() + at, &header, sizeof(header));
        for (char c : text) {
            pool.push_back(static_cast<std::uint8_t>(c));
            if (wide) {
                pool.push_back(0);
            }
        }
        pool.push_back(0);
        if (wide) {
            pool.push_back(0);
        }
        return static_cast<std::int32_t>((1u << layout.namePoolShift) | static_cast<std::uint32_t>(at));  // pool 1
    }

    template <class T>
    void Set(ue3::Object* object, std::uint32_t offset, T const& value) {
        std::memcpy(reinterpret_cast<std::uint8_t*>(object) + offset, &value, sizeof(T));
    }

    ue3::Object* Make(ue3::Name name, ue3::Object* cls, ue3::Object* outer) {
        storage.push_back(std::make_unique<std::uint64_t[]>(kObjectSize / 8));
        auto* const object = reinterpret_cast<ue3::Object*>(storage.back().get());
        Set(object, layout.objectIndex, static_cast<std::int32_t>(objects.size()));
        Set(object, layout.objectName, name);
        Set(object, layout.objectClass, cls);
        Set(object, layout.objectOuter, outer);
        objects.push_back(object);
        return object;
    }
    ue3::Object* Make(std::string const& name, ue3::Object* cls, ue3::Object* outer) {
        return Make(ue3::Name{Name(name), 0}, cls, outer);
    }

    void Link(ue3::Object* owner, std::vector<ue3::Object*> const& fields) {
        Set(owner, layout.structChildren, fields.empty() ? nullptr : fields.front());
        for (std::size_t i = 0; i + 1 < fields.size(); ++i) {
            Set(fields[i], layout.fieldNext, fields[i + 1]);
        }
    }

    ue3::Universe Universe() {
        bases[1] = reinterpret_cast<std::uintptr_t>(pool.data());
        array.data = objects.data();
        array.count = array.max = static_cast<std::int32_t>(objects.size());
        ue3::Universe u;
        u.layout = layout;
        u.objects = &array;
        u.pools = {bases, 2};
        return u;
    }
};

// Core, Engine and SFXGame in miniature.
struct World {
    Fake f;
    ue3::Object* classClass = nullptr;
    ue3::Object* rotator = nullptr;
    ue3::Object* actor = nullptr;
    ue3::Object* pawn = nullptr;
    ue3::Object* bioPawn = nullptr;
    ue3::Object* sfxThing = nullptr;
    ue3::Object* setRotation = nullptr;
    ue3::Object* jump = nullptr;
    ue3::Object* shepard = nullptr;
    ue3::Object* dying = nullptr;
    ue3::Object* numbered = nullptr;

    ue3::Object* Prop(char const* name, ue3::Object* cls, ue3::Object* owner, std::int32_t offset, std::int32_t size,
                      std::uint64_t flags = 0, std::uint64_t typed = 0) {
        ue3::Object* const p = f.Make(name, cls, owner);
        f.Set(p, f.layout.propertyOffset, offset);
        f.Set(p, f.layout.propertyElementSize, size);
        f.Set(p, f.layout.propertyArrayDim, 1);
        f.Set(p, f.layout.propertyFlags, flags);
        f.Set(p, f.layout.propertyTyped, typed);
        return p;
    }

    World() {
        ue3::Object* const package = f.Make("Package", nullptr, nullptr);  // class fixed up below
        ue3::Object* const core = f.Make("Core", package, nullptr);
        ue3::Object* const engine = f.Make("Engine", package, nullptr);
        ue3::Object* const sfxGame = f.Make("SFXGame", package, nullptr);
        classClass = f.Make("Class", nullptr, core);
        f.Set(classClass, f.layout.objectClass, classClass);
        f.Set(package, f.layout.objectClass, classClass);
        f.Set(package, f.layout.objectOuter, core);
        auto cls = [&](char const* name, ue3::Object* super, ue3::Object* outer) {
            ue3::Object* const c = f.Make(name, classClass, outer);
            f.Set(c, f.layout.fieldSuper, super);
            return c;
        };
        ue3::Object* const object = cls("Object", nullptr, core);
        ue3::Object* const field = cls("Field", object, core);
        ue3::Object* const structClass = cls("Struct", field, core);
        f.Set(classClass, f.layout.fieldSuper, structClass);
        f.Set(package, f.layout.fieldSuper, object);
        ue3::Object* const scriptStruct = cls("ScriptStruct", structClass, core);
        ue3::Object* const function = cls("Function", structClass, core);
        ue3::Object* const property = cls("Property", field, core);
        ue3::Object* const boolProperty = cls("BoolProperty", property, core);
        ue3::Object* const intProperty = cls("IntProperty", property, core);
        ue3::Object* const structProperty = cls("StructProperty", property, core);
        ue3::Object* const objectProperty = cls("ObjectProperty", property, core);
        ue3::Object* const arrayProperty = cls("ArrayProperty", property, core);

        rotator = f.Make("Rotator", scriptStruct, object);
        f.Set(rotator, f.layout.structPropertySize, 12);
        f.Link(rotator, {Prop("Pitch", intProperty, rotator, 0, 4), Prop("Yaw", intProperty, rotator, 4, 4),
                         Prop("Roll", intProperty, rotator, 8, 4)});

        actor = cls("Actor", object, engine);
        pawn = cls("Pawn", actor, engine);
        bioPawn = cls("BioPawn", pawn, sfxGame);
        sfxThing = cls("Thing", object, sfxGame);
        auto const rotatorPtr = reinterpret_cast<std::uint64_t>(rotator);

        setRotation = f.Make("SetRotation", function, actor);
        ue3::Object* const newRotation = Prop("NewRotation", structProperty, setRotation, 0, 12, ue3::property_flags::kParm, rotatorPtr);
        ue3::Object* const returnValue = Prop("ReturnValue", boolProperty, setRotation, 12, 4,
            ue3::property_flags::kParm | ue3::property_flags::kOutParm | ue3::property_flags::kReturnParm, 1);
        ue3::Object* const local = Prop("Temp", intProperty, setRotation, 16, 4);  // a local, not a parameter
        f.Link(setRotation, {newRotation, returnValue, local});
        f.Set(setRotation, f.layout.functionParmsSize, std::uint16_t{16});
        f.Set(setRotation, f.layout.functionReturnOffset, std::uint16_t{12});
        f.Set(setRotation, f.layout.structPropertySize, 20);

        ue3::Object* const rotation = Prop("Rotation", structProperty, actor, 0x60, 12, 0, rotatorPtr);
        ue3::Object* const hidden = Prop("bHidden", boolProperty, actor, 0x6C, 4, 0, 0x4);
        ue3::Object* const owner = Prop("Owner", objectProperty, actor, 0x70, 8, 0, reinterpret_cast<std::uint64_t>(actor));
        f.Link(actor, {rotation, hidden, setRotation, owner});

        jump = f.Make("Jump", function, pawn);
        ue3::Object* const crouched = Prop("bIsCrouched", boolProperty, pawn, 0x78, 4, 0, 0x10);
        ue3::Object* const items = Prop("Items", arrayProperty, pawn, 0x40, 16, 0, 0);
        f.Link(pawn, {jump, crouched, items});
        f.Link(bioPawn, {});

        shepard = f.Make("BioPawn_0", bioPawn, engine);
        dying = f.Make("Dying", bioPawn, engine);
        f.Set(dying, f.layout.objectFlags, f.layout.pendingKillFlag);
        numbered = f.Make(ue3::Name{f.Name("Actor"), 4}, actor, engine);  // "Actor_3"
        f.Make(ue3::Name{f.Name("WideName", true), 0}, object, engine);
    }
};

void TestNames() {
    World w;
    ue3::Universe const u = w.f.Universe();
    ue3::Reflection r(u);
    CHECK(r.NameText(w.actor) == "Actor");
    CHECK(r.NameText(w.numbered) == "Actor_3");
    CHECK(r.NameIs(w.numbered, "actor_3"));
    CHECK(!r.NameIs(w.numbered, "Actor"));
    CHECK(!r.NameIs(w.numbered, "Actor_03"));
    CHECK(r.NameIs(w.actor, "ACTOR"));
    CHECK(!r.NameIs(w.actor, "Acto"));
    CHECK(!r.NameIs(w.actor, "Actors"));
    CHECK(ue3::names::BaseText(u.layout, u.pools, 0) == "None");
    CHECK(ue3::names::Equals(u.layout, u.pools, ue3::Name{0, 0}, "none"));
    // A pool that doesn't exist reads as nothing, not a crash.
    CHECK(ue3::names::BaseText(u.layout, u.pools, static_cast<std::int32_t>(5u << 29)).empty());
    ue3::Object* const wide = r.ObjectAt(r.ObjectCount() - 1);
    CHECK(r.NameText(wide) == "WideName");
    CHECK(r.NameIs(wide, "widename"));
    CHECK(r.FullName(w.setRotation) == "Function Engine.Actor.SetRotation");
    CHECK(r.FullName(w.actor) == "Class Engine.Actor");
    CHECK(r.PathName(w.rotator) == "Core.Object.Rotator");
    CHECK(r.FullName(nullptr) == "None");
}

void TestLookups() {
    World w;
    ue3::Reflection r(w.f.Universe());
    CHECK(r.ClassClass() == w.classClass);
    CHECK(r.FindClass("Engine.Actor") == w.actor);
    CHECK(r.FindClass("engine.actor") == w.actor);
    CHECK(r.FindClass("Actor") == w.actor);
    CHECK(r.FindClass("SFXGame.BioPawn") == w.bioPawn);
    CHECK(r.FindClass("Engine.Nothing") == nullptr);
    CHECK(r.TakeNotes().empty());
    // A class asked for in the wrong package is still found, with a note.
    CHECK(r.FindClass("Engine.Thing") == w.sfxThing);
    CHECK(r.TakeNotes().size() == 1);

    auto* const bioPawn = static_cast<ue3::Class*>(w.bioPawn);
    CHECK(r.IsA(w.shepard, bioPawn));
    CHECK(r.IsA(w.shepard, r.FindClass("Engine.Actor")));
    CHECK(!r.IsA(w.numbered, bioPawn));
    CHECK(r.IsChildOf(bioPawn, static_cast<ue3::Struct*>(w.pawn)));
    CHECK(!r.IsA(nullptr, bioPawn));
    CHECK(r.IsAlive(w.shepard));
    CHECK(!r.IsAlive(w.dying));  // pending kill

    // Functions: inherited through the class chain, or by exact path.
    CHECK(r.FindFunction(bioPawn, "SetRotation") == w.setRotation);
    CHECK(r.FindFunction(bioPawn, "Jump") == w.jump);
    CHECK(r.FindFunction("Engine.Actor.SetRotation") == w.setRotation);
    CHECK(r.FindFunction("SFXGame.BioPawn.SetRotation") == nullptr);  // not declared there
    CHECK(r.FindFunction(bioPawn, "Rotation") == nullptr);           // a property, not a function
    CHECK(r.FindFunctionsNamed("setrotation").size() == 1);
    CHECK(r.FindProperty(bioPawn, "SetRotation") == nullptr);        // a function, not a property

    // Late-loaded classes: a miss re-indexes once GObjects grew.
    CHECK(r.FindClass("Engine.Late") == nullptr);
    ue3::Object* const late = w.f.Make("Late", w.classClass, r.OuterOf(w.actor));
    ue3::Reflection r2(w.f.Universe());  // the array grew (and may have moved): a new universe view
    CHECK(r2.FindClass("Engine.Actor") == w.actor);
    CHECK(r2.FindClass("Engine.Late") == late);
}

void TestProperties() {
    World w;
    ue3::Reflection r(w.f.Universe());
    auto* const bioPawn = static_cast<ue3::Class*>(w.bioPawn);
    std::optional<ue3::PropertyInfo> const rotation = r.PropertyOf(bioPawn, "rotation");
    CHECK(rotation && rotation->offset == 0x60 && rotation->elementSize == 12 && rotation->kind == ue3::Kind::Struct);
    ue3::Struct* const rotator = rotation ? r.StructOf(*rotation) : nullptr;
    CHECK(rotator == w.rotator);
    CHECK(rotator != nullptr && r.PropertySize(rotator) == 12);
    std::optional<ue3::PropertyInfo> const yaw = r.PropertyOf(rotator, "Yaw");
    CHECK(yaw && yaw->offset == 4 && yaw->kind == ue3::Kind::Int);

    std::optional<ue3::PropertyInfo> const hidden = r.PropertyOf(bioPawn, "bHidden");
    CHECK(hidden && hidden->kind == ue3::Kind::Bool && hidden->offset == 0x6C && hidden->boolMask == 0x4);
    std::optional<ue3::PropertyInfo> const crouched = r.PropertyOf(bioPawn, "bIsCrouched");
    CHECK(crouched && crouched->boolMask == 0x10 && crouched->offset == 0x78);
    std::optional<ue3::PropertyInfo> const owner = r.PropertyOf(bioPawn, "Owner");
    CHECK(owner && owner->kind == ue3::Kind::Object && owner->typed == w.actor);
    CHECK(owner && r.StructOf(*owner) == nullptr);  // not a struct property
    std::optional<ue3::PropertyInfo> const items = r.PropertyOf(bioPawn, "Items");
    CHECK(items && items->kind == ue3::Kind::Array && items->elementSize == 16);
    CHECK(!r.PropertyOf(bioPawn, "Missing"));
    CHECK(!r.PropertyOf(nullptr, "Rotation"));

    // The cache answers again without walking (the same result, misses included).
    std::optional<ue3::PropertyInfo> const again = r.PropertyOf(bioPawn, "ROTATION");
    CHECK(again && again->property == rotation->property);
    CHECK(!r.PropertyOf(bioPawn, "missing"));

    // Reading and writing a bitfield bool by its mask leaves the other bits alone.
    std::vector<std::uint8_t> instance(0x100, 0);
    instance[0x6C] = 0x3;
    std::uint32_t word = 0;
    std::memcpy(&word, instance.data() + hidden->offset, 4);
    CHECK((word & hidden->boolMask) == 0);
    word |= hidden->boolMask;
    std::memcpy(instance.data() + hidden->offset, &word, 4);
    CHECK(instance[0x6C] == 0x7);
}

void TestParams() {
    World w;
    ue3::Reflection r(w.f.Universe());
    auto* const setRotation = static_cast<ue3::Function*>(w.setRotation);
    std::vector<ue3::ParamSpec> const specs = r.Parameters(setRotation);
    CHECK(specs.size() == 2);  // the local isn't a parameter
    CHECK(specs.size() == 2 && specs[0].name == "NewRotation" && specs[0].offset == 0 && specs[0].size == 12);
    CHECK(specs.size() == 2 && specs[1].boolMask == 1 && (specs[1].flags & ue3::property_flags::kReturnParm) != 0);
    CHECK(r.ParmsSize(setRotation) == 16 && r.ReturnOffset(setRotation) == 12);

    ue3::Params p(r, setRotation);
    CHECK(p.Valid() && p.Size() == 16);
    ue3::Rotator const turn{1, 2, 3};
    CHECK(p.Set("newrotation", turn));
    CHECK(!p.Set("NewRotation", 5));          // wrong size
    CHECK(!p.Set("Nothing", turn));           // no such parameter
    CHECK(!p.SetBool("NewRotation", true));   // not a bool
    std::optional<ue3::Rotator> const back = p.Get<ue3::Rotator>("NewRotation");
    CHECK(back && back->pitch == 1 && back->yaw == 2 && back->roll == 3);
    CHECK(p.ReturnBool() == std::optional<bool>(false));
    // The engine writes the return value; ours reads it by its mask.
    auto* const bytes = static_cast<std::uint8_t*>(p.Data());
    bytes[12] = 0x1;
    CHECK(p.ReturnBool() == std::optional<bool>(true));
    CHECK(!p.Return<int>());  // a bool isn't read as an int

    ue3::Params none(r, nullptr);
    CHECK(!none.Valid());

    // By hand: a bool parameter with another bit, next to an int.
    std::vector<ue3::ParamSpec> hand(2);
    hand[0].name = "bPause";
    hand[0].offset = 0;
    hand[0].size = 4;
    hand[0].boolMask = 0x2;
    hand[0].flags = ue3::property_flags::kParm;
    hand[1].name = "Count";
    hand[1].offset = 4;
    hand[1].size = 4;
    hand[1].flags = ue3::property_flags::kParm;
    ue3::Params q(hand, 6);  // a ParmsSize shorter than the last parameter: the block still holds it
    CHECK(q.Size() == 8);
    CHECK(q.SetBool("bPause", true) && q.GetBool("bPause") == std::optional<bool>(true));
    CHECK(static_cast<std::uint8_t*>(q.Data())[0] == 0x2);
    CHECK(q.Set("Count", 7) && q.Get<int>("Count") == std::optional<int>(7));
    CHECK(q.SetBool("bPause", false) && static_cast<std::uint8_t*>(q.Data())[0] == 0);
    CHECK(!q.ReturnSpec());
}

void TestPatterns() {
    namespace pt = ue3::pattern;
    CHECK(!pt::Compile(""));
    CHECK(!pt::Compile("4"));
    CHECK(!pt::Compile("GG"));
    std::optional<pt::Compiled> const c = pt::Compile("48 8D 15 ?? ?? ?? ?? 48 C1");
    CHECK(c && c->bytes.size() == 9 && c->wild[3] && !c->wild[0]);
    // Code at 0x10: lea rdx,[rip+0x100]; sar ... -> the target is 0x10 + 7 + 0x100.
    std::vector<std::uint8_t> code(0x40, 0x90);
    std::uint8_t const lea[] = {0x48, 0x8D, 0x15, 0x00, 0x01, 0x00, 0x00, 0x48, 0xC1};
    std::memcpy(code.data() + 0x10, lea, sizeof(lea));
    std::vector<std::size_t> const hits = pt::FindAll(code, *c);
    CHECK(hits.size() == 1 && hits[0] == 0x10);
    std::optional<std::size_t> const target = pt::RipTarget(code, 0x10, 3, 7);
    CHECK(target && *target == 0x10 + 7 + 0x100);
    // A pattern that starts with a wildcard, and one that runs off the end.
    std::optional<pt::Compiled> const lead = pt::Compile("?? 8D 15");
    CHECK(lead && pt::FindAll(code, *lead).size() == 1);
    std::optional<pt::Compiled> const tail = pt::Compile("90 90 90 FF");
    CHECK(tail && pt::FindAll(code, *tail).empty());
    CHECK(!pt::RipTarget(code, 0x3E, 3, 7));
    // Several matches, capped.
    std::optional<pt::Compiled> const nop = pt::Compile("90");
    CHECK(nop && pt::FindAll(code, *nop, 4).size() == 4);
}

} // namespace

int RunReflectTests() {
    TestNames();
    TestLookups();
    TestProperties();
    TestParams();
    TestPatterns();
    return gFailures;
}
