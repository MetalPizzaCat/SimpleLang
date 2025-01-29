#include "Type.hpp"

const char *SimpleLang::typeToString(Type type)
{
    switch (type)
    {
    case Type::Null:
        return "Null";
    case Type::Number:
        return "Float";
    case Type::Int:
        return "Int";
    case Type::UserData:
        return "UserData";
    case Type::MemoryObj:
        return "Object";
    case Type::NativeFunction:
        return "NativeFunction";
    }
    return nullptr;
}