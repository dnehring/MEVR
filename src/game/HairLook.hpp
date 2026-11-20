// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace mevr::ue3 {
struct Object;
}

// The look of a hair (docs/trilogy-hair-plan.md section 5.1, B1): the material parameters its master material already has
// (ME3's hair: two highlight lobes, each an exponent, an intensity and a colour; ME2's: two exponents), set on the hair
// component's own material instance (MeshComponent.CreateAndSetMaterialInstanceConstant: the shared package object is never
// changed), put back when the look is off. Parameter names are the master material's own (its MaterialExpression*Parameter
// objects in memory give their FNames and defaults). Game thread; every game. Mod side.
namespace mevr::game::hair_look {

// `mevr hair material`: the component's material chain, every parameter of its master material with the default and the
// value the component's material gives it now.
std::string Describe(ue3::Object* component);
// `mevr hair param`: one parameter (a scalar: one value; a vector: r g b [a]) on the component's own instance.
std::string Set(ue3::Object* component, std::string const& name, std::vector<double> const& values);
// A look "Name=value; Name=r,g,b[,a]; ..." on the component's own instance; once per component and look (cheap to call
// every tick). Empty: nothing. Returns false if a parameter isn't the material's (logged once).
bool Apply(ue3::Object* component, std::string const& look);
// The component's material back to its mesh's (the instance dropped).
void PutBack(ue3::Object* component);
// A level change: every remembered component and instance forgotten (their memory may be reused).
void Forget();
// The component's material in slot 0 (its override, else its mesh's), and setting the override (null: the mesh's own).
ue3::Object* Material(ue3::Object* component);
bool SetMaterial(ue3::Object* component, ue3::Object* material);

} // namespace mevr::game::hair_look
