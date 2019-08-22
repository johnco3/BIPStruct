/*
**
**
** File name:
** Author:
**
** DESCRIPTION
**
*/

#pragma once

// SYSTEM INCLUDES
#include <iostream>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/container/scoped_allocator.hpp>

// APPLICATION INCLUDES
// DEFINES
// EXTERNAL FUNCTIONS
// EXTERNAL VARIABLES
// CONSTANTS
// STRUCTS
// TYPEDEFS
// FORWARD DECLARATIONS

namespace Shared {
    namespace bip = boost::interprocess;

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
        //! @JC see constructible__idp55849760.html
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
        MyStruct(std::allocator_arg_t, A alloc, int a, int b, I && init)
            : MyStruct(a, b, Vec<uint8_t>(std::forward<I>(init), alloc))
        {}

        //! Publicly accessible struct members.
        int a = 0;
        int b = 0;
        //! Nested IPC container of POD data
        Vec<uint8_t> data;
    private:
        //! Hidden  constructor (@JC not sure why - to hide the
        //! allocator as it is not the first arg?).
        explicit MyStruct(int a, int b, Vec<uint8_t> data)
            : a{ a }
            , b{ b }
            , data{ std::move(data) }
        {}
    };

    //! This is the shared IPC alias.
    using Database = Map<String, MyStruct>;

    static inline std::ostream& operator<<(
        std::ostream& os, Database const& db) {
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

