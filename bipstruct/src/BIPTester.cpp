/*
**
**
** File name:
** Author:
**
** DESCRIPTION
**
*/

// SYSTEM INCLUDES
//#include <...>

// APPLICATION INCLUDES
#include "BIPTester.h"

// EXTERNAL FUNCTIONS
// EXTERNAL VARIABLES
// CONSTANTS
// STRUCTS
// NAMESPACE USAGE
// STATIC VARIABLE INITIALIZATIONS
int
main()
{
    using Shared::MyStruct;
    Shared::Segment mf(Shared::bip::open_or_create, "test.bin", 65536*2);
    auto mgr = mf.get_segment_manager();

    auto& db = *mf.find_or_construct<Shared::Database>("complex")(mgr);

    // Issues with brace-enclosed initializer list
    using Bytes = std::initializer_list<uint8_t>;

    // More magic: piecewise construction protocol :)
    static constexpr std::piecewise_construct_t pw{};
    using std::forward_as_tuple;

    // @JC Debugging - try constructing with explicit args
    MyStruct tempStruct(
        std::allocator_arg,
        db.get_allocator(), 1, 2,
        std::initializer_list<uint8_t>{1, 2, 3});

    //! @JC Debugging - Hmm... this does not work
    //! @JC Do not know why if piecewise_construct
    //! @JC below works????
    //MyStruct tempStruct1(
    //    std::allocator_arg,
    //    db.get_allocator(),
    //    forward_as_tuple(1, 2, Bytes{ 1, 2 }));
    db.emplace(pw, forward_as_tuple("one"), forward_as_tuple(1, 2, Bytes{ 1, 2 }));
    db.emplace(pw, forward_as_tuple("two"), forward_as_tuple(2, 3, Bytes{ 4 }));
    db.emplace(pw, forward_as_tuple("three"), forward_as_tuple(3, 4, Bytes{ 5, 8 }));

    std::cout << "\n=== Before updates\n" << db << std::endl;

    // Clumsy:
    db[Shared::String("one", mgr)] = MyStruct{ std::allocator_arg, mgr, 1, 20, Bytes{ 7, 8, 9 } };

    // As efficient or better, and less clumsy:
    auto insert_or_update = [&db](auto&& key, auto&& ... initializers) -> MyStruct & {
        // Be careful not to move twice: https://en.cppreference.com/w/cpp/container/map/emplace
        // > The element may be constructed even if there already is an element
        // > with the key in the container, in which case the newly constructed
        // > element will be destroyed immediately.
        // @JC If initializer (C++17)
        if (auto insertion = db.emplace(pw, forward_as_tuple(key), std::tie(initializers...)); insertion.second) {
            return insertion.first->second;
        } else {
            // @JC note initializers is parameter pack
            return insertion.first->second = MyStruct(
                std::allocator_arg, db.get_allocator(),
                std::forward<decltype(initializers)>(initializers)...); // forwarding ok here
        }
    };

    insert_or_update("one", 1, 20, Bytes{7, 8, 9});
    insert_or_update("two", 2, 30, Bytes{});
    insert_or_update("nine", 9, 100, Bytes{ 5, 6 });

    // partial updates:
    db.at(Shared::String("nine", mgr)).data.push_back(42);

    // For more efficient key lookups in the case of unlikely insertion, use
    // heterogeneous comparer, see https://stackoverflow.com/a/27330042/85371

    std::cout << "\n=== After updates\n" << db << std::endl;
}
