#ifndef CPP14_COMPAT_H
#define CPP14_COMPAT_H

#if defined(__GNUC__) && (__GNUC__ < 5) && !defined(__clang__)
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace std {

template<typename T, typename... Args>
inline typename enable_if<!is_array<T>::value, unique_ptr<T>>::type
make_unique(Args&&... args) {
    return unique_ptr<T>(new T(forward<Args>(args)...));
}

template<typename T>
inline typename enable_if<is_array<T>::value, unique_ptr<T>>::type
make_unique(size_t n) {
    typedef typename remove_extent<T>::type U;
    return unique_ptr<T>(new U[n]());
}

} // namespace std
#endif

#endif
