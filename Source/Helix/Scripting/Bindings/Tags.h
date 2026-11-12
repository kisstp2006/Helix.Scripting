#pragma once

// Tags that Helix.Bindgen reads in C++ headers; they expand to nothing. A tag stands before what it describes:
//
//   HX_CLASS(Namespace = "HelixEngine")       class Transform : public Component { ... };
//   HX_STRUCT(Namespace = "HelixEngine")      struct Vector3 { HX_FIELD() f32 x; ... };
//   HX_ENUM(Namespace = "HelixEngine")        enum class Space : i32 { World = 0, Self = 1 };
//   HX_INTERFACE()                            class IDamageable { ... };
//   HX_FIELD(SerializeField)                  f32 speed = 1;
//   HX_PROPERTY(Name = "position")            Vector3 position() const;      (the setter SetPosition is paired)
//   HX_FUNCTION(Leaf)                         void Rotate(Vector3 eulers);
//   HX_EVENT(Name = "quitting")               static Event<>& quitting();
//
// Arguments, comma separated: Name = "...", Namespace = "...", Attributes = "[C# attributes]", and the flags Leaf (a
// call that neither blocks nor calls back into managed code: no GC transition), Hidden (not in C#), ReadOnly, Sealed,
// Abstract, Root (the class whose ids identify every object below it), SerializeField, NonSerialized.
#define HX_CLASS(...)
#define HX_STRUCT(...)
#define HX_ENUM(...)
#define HX_INTERFACE(...)
#define HX_FIELD(...)
#define HX_PROPERTY(...)
#define HX_FUNCTION(...)
#define HX_EVENT(...)
