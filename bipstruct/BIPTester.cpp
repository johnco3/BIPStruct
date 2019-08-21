#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/container/scoped_allocator.hpp>
#include <iostream>

namespace bip = boost::interprocess;

namespace Shared {
    // @JC Note changed from Coliru's managed_mapped_file
    // in futile attempt to fix teh read access violation.
    using Segment = bip::managed_shared_memory;
    using SMgr = Segment::segment_manager;

    template <typename T> using Alloc =
        bip::allocator<T, SMgr>;
    template <typename T> using ScopedAlloc =
        boost::container::scoped_allocator_adaptor<Alloc<T>>;
    using String = bip::basic_string<
        char, std::char_traits<char>, Alloc<char>>;

    using boost::interprocess::map;

    //! IPC Vec alias (uses special allocator).
    template <typename T> using Vec =
        boost::container::vector<T, ScopedAlloc<T>>;

    //! IPC Map alias (uses special allocator).
    template <typename K, typename T> using Map =
        map<K, T, std::less<K>, ScopedAlloc<
            typename map<K, T>::value_type>>;

    //! User struct shared across processes
    struct MyStruct {
        //! @JC see https://www.boost.org/doc/libs/1_69_0/doc/html/boost/container/constructible__idp55849760.html
        //! This nested typedef is required for the boost
        using allocator_type = ScopedAlloc<char>;

        //! Explicit default constructor.
        //! @JC modified to take const& like vector
        explicit MyStruct(const allocator_type& alloc)
            : data(alloc)
        {}

        // @JC - delete default move/copy assignment/ctors
        //MyStruct(MyStruct const&) = default;
        //MyStruct(MyStruct&&) = default;
        //MyStruct& operator=(MyStruct const&) = default;
        //MyStruct& operator=(MyStruct&&) = default;

        //! Move constructor with allocator_arg_t.
        //! @JC specialization constructible_with_allocator_prefix<X>::value is true,
        //! T must have a nested type, allocator_type and at least one constructor for
        //! which allocator_arg_t is the first parameter and allocator_type is the
        //! second parameter.
        MyStruct(std::allocator_arg_t, allocator_type, MyStruct&& rhs)
            : MyStruct(std::move(rhs))
        {}

        //! copy constructor with allocator_arg_t.
        MyStruct(std::allocator_arg_t, allocator_type, const MyStruct& rhs)
            : MyStruct(rhs)
        {}

        //! Constructor taking initializer list 'I' template argument.
        //! @JC This is constructed via the constructible_with_allocator_prefix
        //! @JC however under the covers, we are effectively a delegating
        //! @JC constructor that forwards the call to Vec's
        //! @JC constructible_with_allocator_suffix<T> constructor.
        //! @JC This is complex stuff!
        template <typename I, typename A = Alloc<char>>
        MyStruct(std::allocator_arg_t, A alloc, int a, int b, I&& init)
            : MyStruct(a, b, Vec<uint8_t>(std::forward<I>(init), alloc))
        {}

        //! Publicly accessible struct members.
        int a = 0;
        int b = 0;
        //! Nested IPC container of POD data
        Vec<uint8_t> data;
    private:
        //! Hidden  constructor (@JC not sure why - to hide the allocator as it is not the first arg?).
        explicit MyStruct(int a, int b, Vec<uint8_t> data)
            : a{ a }
            , b{ b }
            , data{ std::move(data) }
        {}
    };

    //! This is the shared IPC alias.
    using Database = Map<String, MyStruct>;

    static inline std::ostream& operator<<(std::ostream& os, Database const& db) {
        os << "db has " << db.size() << " elements:";

        for (const auto& [k, v] : db) {
            os << " {" << k << ": " << v.a << "," << v.b << ", [";
            for (unsigned i : v.data)
                os << i << ",";
            os << "]}";
        }

        return os;
    }
}

class A {
public:
    A(int)
    {}
    bool operator<(const A&) const {
        return false;
    }
};

class B {
public:
    B(int, const char*)
    {}
};

int
main()
{
    using Shared::MyStruct;
    Shared::Segment mf(bip::open_or_create, "test.bin", 65536*2);
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
        if (auto insertion = db.emplace(pw, forward_as_tuple(key), std::tie(initializers...)); insertion.second) {
            return insertion.first->second;
        } else {
            return insertion.first->second = MyStruct(
                std::allocator_arg,
                db.get_allocator(),
                std::forward<decltype(initializers)>(initializers)...); // forwarding ok here
        }
    };

    insert_or_update("two", 2, 30, Bytes{});
    insert_or_update("nine", 9, 100, Bytes{ 5, 6 });

    // partial updates:
    db.at(Shared::String("nine", mgr)).data.push_back(42);

    // For more efficient key lookups in the case of unlikely insertion, use
    // heterogeneous comparer, see https://stackoverflow.com/a/27330042/85371

    std::cout << "\n=== After updates\n" << db << std::endl;
}
