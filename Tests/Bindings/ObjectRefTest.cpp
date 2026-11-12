#include <Helix/Scripting/Bindings/ObjectRef.h>
#include <Helix/Testing/Test.h>

// ObjectRef's null semantics and wrapper slots on a small root class of its own.
namespace
{
    struct Node
    {
        virtual ~Node();
        helix::u64 id = 0;
        helix::u64 wrapper = 0;
    };

    struct Leaf : Node
    {
    };

    helix::Array<Node*> gNodes;
    helix::u64 gNext = 0;
    helix::Array<helix::u64> gReleased;

    Node::~Node()
    {
        helix::scripting::ReleaseWrapper(wrapper);
        for (helix::usize i = 0; i < gNodes.Size(); i++)
            if (gNodes[i] == this)
            {
                gNodes.RemoveAt(i);
                break;
            }
    }

    template <class T>
    T* Make()
    {
        T* t = new T();
        t->id = ++gNext;
        gNodes.Push(t);
        return t;
    }
}

template <>
struct helix::scripting::ObjectHandles<Node>
{
    static u64 ToId(const Node* n) { return n->id; }
    static Node* FromId(u64 id)
    {
        for (Node* n : gNodes)
            if (n->id == id)
                return n;
        return nullptr;
    }
    static TypeId TypeOf(const Node*) { return TypeId(); }
    static u64& WrapperOf(Node* n) { return n->wrapper; }
};

using namespace helix;
using namespace helix::scripting;

HX_TEST(ObjectRef, DestroyedObjectsAreNull)
{
    Leaf* leaf = Make<Leaf>();
    Node* other = Make<Node>();
    ObjectRef<Node, Leaf> a(leaf);
    ObjectRef<Node> base = a;
    ObjectRef<Node> b(other);
    ObjectRef<Node> none;
    HX_CHECK(a && a.Get() == leaf && base.Get() == leaf && !(a == nullptr));
    HX_CHECK(a == base && base == a && !(a == b) && a != b && none == nullptr && !none);
    delete leaf;
    // Destroyed: null, equal to null and to every other null reference, never a dangling pointer.
    HX_CHECK(!a && a == nullptr && a.Get() == nullptr && a.Id() != 0);
    HX_CHECK(a == none && none == base && !(a == b));
    delete other;
    HX_CHECK(b == a);
}

HX_TEST(ObjectRef, WrappersAreReleasedWithTheObject)
{
    SetManagedHandleRelease([](u64 handle) { gReleased.Push(handle); });
    Node* n = Make<Node>();
    ObjectHandles<Node>::WrapperOf(n) = 77;
    Node* empty = Make<Node>();
    delete empty;
    HX_CHECK(gReleased.Empty());
    delete n;
    HX_CHECK(gReleased.Size() == 1 && gReleased[0] == 77);
    SetManagedHandleRelease(nullptr);
    u64 slot = 5;
    ReleaseWrapper(slot);
    HX_CHECK(slot == 0 && gReleased.Size() == 1);
    gReleased.Clear();
}
