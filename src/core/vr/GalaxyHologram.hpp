// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <array>
#include <map>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The galaxy map as a hologram we draw ourselves (research/51 section 8). LE1's map is drawn by the game's Flash layer, which
// ignores the scene view (section 7), but the map's model is the game's: the galaxy camera's actors (clusters, systems,
// planets) with positions in the map's own space. The mod reads them each tick and sends a Scene; the host places the level
// on a table in front of the player, draws it in stereo as a projection layer of its own, and turns a controller's pick into a
// click on the game's own map at the place the game shows that object (ToGameScreen), so the game selects and travels itself.
//
// Room space is OpenXR's (metres, +Y up, -Z forward); game space is UE3's (units, X forward, Y right, Z up). This file is
// engine-agnostic and unit-tested (tests/Main.cpp TestGalaxyHologram); tools/galaxy-preview draws it offline.
namespace mevr::vr::galaxy_holo {

// ---- What the mod sends (a plain struct: it crosses the host/mod boundary) -----------------------------------------------
enum class Level : std::uint8_t { Unknown = 0, Galaxy = 1, Cluster = 2, System = 3, Planet = 4 };
enum class Kind : std::uint8_t { Other = 0, Cluster = 1, System = 2, Planet = 3, Relay = 4, Star = 5 };
enum Flag : std::uint8_t {
    kSelectable = 1,  // a pick on it means something to the game
    kSelected = 2,    // the game's current selection
    kHere = 4,        // the Normandy is here
    kMarked = 8,      // the game highlights it (a plot target)
};

inline constexpr int kMaxObjects = 64;
inline constexpr int kMaxLinks = 48;
inline constexpr int kLabelBytes = 48;

struct Object {
    float position[3] = {};  // game units, the map's own space
    float size = 0.0f;       // relative size (the game's draw scale); 0: the kind's default
    std::uint32_t id = 0;    // stable while the level is up
    std::uint8_t kind = 0;   // Kind
    std::uint8_t flags = 0;  // Flag bits
    std::uint8_t pad[2] = {};
    char label[kLabelBytes] = {};  // UTF-8, may be empty
};

struct Link {  // a relay route between two objects (indices into objects)
    std::uint8_t a = 0;
    std::uint8_t b = 0;
    std::uint8_t pad[2] = {};
};

// The game's map camera, to put a pick where the game's own map shows it.
struct GameCamera {
    float location[3] = {};
    float pitch = 0.0f;  // radians (UE3 rotator: pitch up, yaw right, roll)
    float yaw = 0.0f;
    float roll = 0.0f;
    float fovXDeg = 0.0f;  // horizontal field of view
    float aspect = 0.0f;   // the game window's width / height
    std::uint8_t valid = 0;
    std::uint8_t pad[3] = {};
};

struct Scene {
    std::uint32_t sequence = 0;
    std::uint8_t level = 0;  // Level
    std::uint8_t objectCount = 0;
    std::uint8_t linkCount = 0;
    std::uint8_t pad = 0;
    float centre[3] = {};               // game units: what sits at the table's centre
    float up[3] = {0.0f, 0.0f, 1.0f};   // the map plane's normal, game space, unit
    float forward[3] = {1.0f, 0.0f, 0.0f};  // 'away from the player' on the table, game space, unit, in the plane
    float extent = 0.0f;                // game units out to the table's rim; 0: from the objects
    GameCamera camera;
    char title[kLabelBytes] = {};
    Object objects[kMaxObjects];
    Link links[kMaxLinks];
};

// ---- The game's map, as the mod (live) or tools/galaxy-preview (a `mevr galaxy dump` file) reads it ----------------------
// LE1's map is a 3D layout of actors in the CIC's level (research/51 section 8): BioCameraBehaviorGalaxy's m_pDynamicObjects
// hold every level's actors; the current level's selectable ones are tagged m_sSelectableObject plus two digits ("Cluster01",
// "System02", "Planet03"), visible unless bHidden, and named by their text reference ("135724"). The game selects the one
// nearest its cross-hair actor; its camera (the player camera's view) shows the map, and the Flash layer draws its rings and
// labels where that camera projects each actor.
struct MapActor {
    std::string tag;
    std::string name;  // the object's name: the text reference of its label
    float location[3] = {};
    bool hidden = false;
};
struct MapModel {
    int state = 0;           // m_nCurrentState: 1 galaxy, 2 cluster, 3 system, 4 planet
    std::string selectable;  // m_sSelectableObject
    std::vector<MapActor> actors;
    bool hasCentre = false;
    float centre[3] = {};          // m_pCenterObject's place
    std::string selected;          // m_pSelectedObject's name
    GameCamera camera;
    std::map<std::string, std::string> labels;          // name (text reference) -> text
    std::vector<std::pair<std::string, std::string>> links;  // relay routes, by actor name
    std::string title;
};
// The actor is one of the level's selectable ones (the tag is the selectable kind plus two characters, as the game checks).
bool IsSelectable(MapActor const& actor, std::string const& selectable);
// The Scene for the level: its visible selectable actors with their labels, the selection, the relay routes between them,
// centred on the centre object and turned so 'away' on the table is where the game's camera looks.
Scene BuildScene(MapModel const& model);

// ---- Settings ([Galaxy] in <GAME>MEVR.ini) --------------------------------------------------------------------------------
struct TableSettings {
    float distanceM = 0.70f;   // the table's centre ahead of the head when the map opens
    float belowM = 0.45f;      // ... and below the eyes
    float radiusM = 0.48f;     // the map's rim
    float heightScale = 1.0f;  // stretches the map's height above its plane
};

// The table: its centre and a yaw-only orientation facing the player (its -Z points away from them).
struct Table {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    float radiusM = 0.40f;
};
Table PlaceTable(XrPosef const& head, TableSettings const& settings);

// Metres per game unit for `scene` on a table of `radiusM` (the extent, or the farthest object in the plane, at the rim).
float FitScale(Scene const& scene, float radiusM);
// A game position on the table, room space.
XrVector3f ToRoom(Scene const& scene, Table const& table, float scale, float const gamePosition[3], float heightScale = 1.0f);

struct Placed {
    XrVector3f position{};
    float radiusM = 0.0f;  // drawn size
    int index = -1;        // into scene.objects
};
std::vector<Placed> PlaceObjects(Scene const& scene, Table const& table, TableSettings const& settings);

// The selectable object a ray points at: the smallest angle off the ray within a tolerance (the larger of `minToleranceDeg`
// and the object's own angular size); -1: none.
int Pick(XrVector3f const& origin, XrVector3f const& direction, std::span<Placed const> placed, Scene const& scene,
         float minToleranceDeg = 2.5f);

// The pointer: what's hovered, and a select when the trigger goes down on it (latched 0.6 down, 0.4 up).
class Pointer {
public:
    // Returns the index selected this step, if any.
    std::optional<int> Step(int candidate, float trigger, bool valid);
    int Hovered() const { return hovered_; }
    bool Down() const { return down_; }
    void Reset();

private:
    int hovered_ = -1;
    bool down_ = false;
};

// Where the game's map shows a game position: window UV (0..1, y down), nullopt behind the camera or without a camera.
std::optional<std::array<float, 2>> ToGameScreen(GameCamera const& camera, float const gamePosition[3]);

// ---- What the host draws ----------------------------------------------------------------------------------------------------
struct Colour {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;  // linear; a scales the light (all of it is additive but labels' plates)
};
enum class SpriteStyle : std::uint8_t { Star = 0, Halo = 1, Planet = 2, Relay = 3 };
struct Sprite {
    XrVector3f centre{};
    float radiusM = 0.0f;
    Colour colour;
    SpriteStyle style = SpriteStyle::Star;
    XrVector3f light{0.0f, 1.0f, 0.0f};  // Planet: toward its star, room space, unit
};
struct Line {
    XrVector3f a{}, b{};
    float widthM = 0.0f;
    Colour colour;
};
struct Ring {  // a circle in a plane: orbits, the table's rim, a selection marker
    XrVector3f centre{};
    XrVector3f normal{0.0f, 1.0f, 0.0f};
    float radiusM = 0.0f;
    float widthM = 0.0f;
    Colour colour;
};
struct Label {
    XrVector3f anchor{};  // the text's bottom centre (raised above its object when it would cover another label)
    float heightM = 0.0f;
    Colour colour;
    std::string text;
    bool plate = true;  // a dark plate behind it
};
enum class DiscStyle : std::uint8_t { Galaxy = 0, Grid = 1 };
struct Disc {
    XrVector3f centre{};
    XrVector3f normal{0.0f, 1.0f, 0.0f};
    XrVector3f forward{0.0f, 0.0f, -1.0f};  // the disc's own 'up' in its plane
    float radiusM = 0.0f;
    Colour colour;
    DiscStyle style = DiscStyle::Grid;
};
struct DrawList {
    std::vector<Disc> discs;
    std::vector<Ring> rings;
    std::vector<Line> lines;
    std::vector<Sprite> sprites;
    std::vector<Label> labels;
};

// The level's picture: the table's base, the galaxy's disc (galaxy level), relay routes, orbits (system level), each object
// with its label and a stalk down to the plane, the hovered one's halo, the title over the far rim. `timeS` drives pulses.
DrawList Build(Scene const& scene, Table const& table, TableSettings const& settings, std::span<Placed const> placed, int hovered,
               float timeS);

// One line for the log.
std::string Describe(Scene const& scene);

// XR helpers this file and its users share.
XrVector3f Add(XrVector3f const& a, XrVector3f const& b);
XrVector3f Sub(XrVector3f const& a, XrVector3f const& b);
XrVector3f Mul(XrVector3f const& a, float s);
float Dot(XrVector3f const& a, XrVector3f const& b);
XrVector3f Cross(XrVector3f const& a, XrVector3f const& b);
float Length(XrVector3f const& a);
XrVector3f Normalize(XrVector3f const& a);

} // namespace mevr::vr::galaxy_holo
