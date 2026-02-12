#ifndef KRAKATOA_CONCATENATE_H
#define KRAKATOA_CONCATENATE_H
#include <sstream>
#include <string>
#include <type_traits>

// Helper function to convert a single value to a string
template<typename T>
std::string ToString(T&& value) {
    std::stringstream ss;
    ss << std::boolalpha << std::forward<T>(value);
    return ss.str();
}

// Base case for the variadic function template
inline std::string Concatenate() {
    return "";
}

/**
 * Concatenates a lot of things and returns a std::string. Use it to concatenate text or numbers
 * instead of the std::stringstream boilerplate.
 * */
template<typename T, typename... Args>
std::string Concatenate(T&& first, Args&&... args) {
    return ToString(std::forward<T>(first)) + Concatenate(std::forward<Args>(args)...);
}

#endif //KRAKATOA_CONCATENATE_H
