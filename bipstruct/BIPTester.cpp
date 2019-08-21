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
    // @JC Note changed from coliru's managed_mapped_file
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

    template <typename T> using Vec =
        boost::container::vector<T, ScopedAlloc<T>>;

    template <typename K, typename T> using Map =
        map<K, T, std::less<K>, ScopedAlloc<typename map<K, T>::value_type>>;

    //! This is user struct that I want to manage in shared memory
    struct MyStruct {
        using allocator_type = ScopedAlloc<char>;

        explicit MyStruct(allocator_type alloc) : data(alloc) {}
        //MyStruct(MyStruct const&) = default;
        //MyStruct(MyStruct&&) = default;
        //MyStruct& operator=(MyStruct const&) = default;
        //MyPodStruct& operator=(MyStruct&&) = default;

        // Move constructor with allocator_arg_t
        MyStruct(std::allocator_arg_t, allocator_type, MyStruct&& rhs)
            : MyStruct(std::move(rhs))
        {}

        // copy constructor with allocator_arg_t
        MyStruct(std::allocator_arg_t, allocator_type, const MyStruct& rhs)
            : MyStruct(rhs)
        {}

        template <typename I, typename A = Alloc<char>>
        MyStruct(std::allocator_arg_t, A alloc, int a, int b, I && init)
            : MyStruct(a, b, Vec<uint8_t>(std::forward<I>(init), alloc)) { }

        int a = 0; // simplify default constructor using NSMI
        int b = 0;
        Vec<uint8_t> data;

    private:
        //! Hidden  pr
        explicit MyStruct(int a, int b, Vec<uint8_t> data)
            : a(a)
            , b(b)
            , data(std::move(data))
        {}
    };

    using Database = Map<String, MyStruct>;

    static inline std::ostream& operator<<(std::ostream& os, Database const& db) {
        os << "db has " << db.size() << " elements:";

        for (auto& [k, v] : db) {
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

int main() {

    using Shared::MyStruct;
    Shared::Segment mf(bip::open_or_create, "test.bin", 65536);
    auto mgr = mf.get_segment_manager();

    auto& db = *mf.find_or_construct<Shared::Database>("complex")(mgr);

    // Issues with brace-enclosed initializer list
    using Bytes = std::initializer_list<uint8_t>;

    // More magic: piecewise construction protocol :)
    static constexpr std::piecewise_construct_t pw{};
    using std::forward_as_tuple;
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
