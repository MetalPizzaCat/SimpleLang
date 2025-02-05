#include "Machine.hpp"
#include <iostream>
#include <vector>
GobLang::Machine::Machine(Compiler::ByteCode const &code)
{
    m_constInts = code.ints;
    m_constStrings = code.ids;
    m_operations = code.operations;
}
void GobLang::Machine::addFunction(FunctionValue const &func, std::string const &name)

{
    m_globals[name] = MemoryValue{.type = Type::NativeFunction, .value = func};
}
void GobLang::Machine::step()
{
    if (m_programCounter >= m_operations.size())
    {
        return;
    }
    switch ((Operation)m_operations[m_programCounter])
    {
    case Operation::Add:
        _addInt();
        break;
    case Operation::Sub:
        _subInt();
        break;
    case Operation::Call:
        _call();
        break;
    case Operation::Set:
        _set();
        collectGarbage();
        break;
    case Operation::Get:
        _get();
        break;
    case Operation::GetLocal:
        _getLocal();
        break;
    case Operation::SetLocal:
        _setLocal();
        collectGarbage();
        break;
    case Operation::PushConstInt:
        _pushConstInt();
        break;
    case Operation::PushConstChar:
        _pushConstChar();
        break;
    case Operation::PushConstString:
        _pushConstString();
        break;
    case Operation::GetArray:
        _getArray();
        break;
    case Operation::SetArray:
        _setArray();
        collectGarbage();
        break;
    case Operation::Jump:
        _jump();
        return; // this uses return because we want to avoid advancing the counter after jup
    case Operation::JumpIfNot:
        _jumpIf();
        return;
    case Operation::PushTrue:
        m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = true});
        break;
    case Operation::PushFalse:
        m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = false});
        break;
    case Operation::Equals:
        _eq();
        break;
    case Operation::NotEq:
        _neq();
        break;
    case Operation::Less:
        _less();
        break;
    case Operation::More:
        _more();
        break;
    case Operation::Not:
        _not();
        break;
    case Operation::And:
        _and();
        break;
    case Operation::Or:
        _or();
        break;
    case Operation::LessOrEq:
        _lessOrEq();
        break;
    case Operation::MoreOrEq:
        _moreOrEq();
        break;
    case Operation::Negate:
        _negate();
        break;
    case Operation::ShrinkLocal:
        _shrink();
        collectGarbage();
        break;
    case Operation::End:
        m_forcedEnd = true;
        break;
    default:
        std::cerr << "Invalid op code: " << (int32_t)m_operations[m_programCounter] << std::endl;
        break;
    }
    m_programCounter++;
}

void GobLang::Machine::printGlobalsInfo()
{
    for (std::map<std::string, MemoryValue>::iterator it = m_globals.begin(); it != m_globals.end(); it++)
    {
        std::cout << it->first << "(" << typeToString(it->second.type) << ")" << " = " << valueToString(it->second) << std::endl;
    }
}

void GobLang::Machine::printVariablesInfo()
{
    std::cout << "Local(" << m_variables.size() << "):" << std::endl;
    for (std::vector<MemoryValue>::iterator it = m_variables.begin(); it != m_variables.end(); it++)
    {
        std::cout << it - m_variables.begin() << ": " << typeToString(it->type) << " = " << valueToString(*it) << std::endl;
    }
}

void GobLang::Machine::printStack()
{
    std::cout << "Stack(" << m_operationStack.size() << "):" << std::endl;
    for (std::vector<MemoryValue>::reverse_iterator it = m_operationStack.rbegin(); it != m_operationStack.rend(); it++)
    {
        std::cout << it - m_operationStack.rbegin() << ": " << typeToString(it->type) << " = " << valueToString(*it) << std::endl;
    }
}

GobLang::MemoryValue *GobLang::Machine::getStackTop()

{
    if (m_operationStack.empty())
    {
        return nullptr;
    }
    else
    {
        return &m_operationStack[m_operationStack.size() - 1];
    }
}

GobLang::MemoryValue *GobLang::Machine::getStackTopAndPop()
{
    if (m_operationStack.empty())
    {
        return nullptr;
    }
    else
    {
        MemoryValue *m = new MemoryValue(m_operationStack[m_operationStack.size() - 1]);
        m_operationStack.pop_back();
        return m;
    }
}

GobLang::ArrayNode *GobLang::Machine::createArrayOfSize(int32_t size)
{
    ArrayNode *node = new ArrayNode(size);
    m_memoryRoot->pushBack(node);
    return node;
}

GobLang::StringNode *GobLang::Machine::createString(std::string const &str, bool alwaysNew)
{
    MemoryNode *root = m_memoryRoot;
    StringNode *node = nullptr;
    // avoid making instance for each call, check if there is anything that uses this already
    while (root != nullptr && !alwaysNew)
    {
        if (StringNode *strNode = dynamic_cast<StringNode *>(root); strNode != nullptr && strNode->getString() == str)
        {
            node = strNode;
            break;
        }
        root = root->getNext();
    }
    if (node == nullptr)
    {
        node = new StringNode(str);
        m_memoryRoot->pushBack(node);
    }
    return node;
}

void GobLang::Machine::popStack()
{
    m_operationStack.pop_back();
}

void GobLang::Machine::pushToStack(MemoryValue const &val)
{
    m_operationStack.push_back(val);
}

void GobLang::Machine::setLocalVariableValue(size_t id, MemoryValue const &val)
{
    if (id >= m_variables.size())
    {
        m_variables.resize(id + 1);
    }
    if (val.type == Type::MemoryObj)
    {
        std::get<MemoryNode *>(val.value)->increaseRefCount();
    }
    if (m_variables[id].type == Type::MemoryObj)
    {
        std::get<MemoryNode *>(m_variables[id].value)->decreaseRefCount();
    }
    m_variables[id] = val;
}

GobLang::MemoryValue *GobLang::Machine::getLocalVariableValue(size_t id)
{
    if (m_variables.size() < id)
    {
        return nullptr;
    }
    return &m_variables[id];
}

void GobLang::Machine::shrinkLocalVariableStackBy(size_t size)
{
    for (size_t i = 0; i < size; i++)
    {
        size_t ind = m_variables.size() - i - 1;
        if (m_variables[ind].type == Type::MemoryObj)
        {
            std::get<MemoryNode *>(m_variables[ind].value)->decreaseRefCount();
        }
    }
    // possibly add m_variables.resize(m_variables.size() - size)
}

void GobLang::Machine::createVariable(std::string const &name, MemoryValue const &value)
{
    m_globals[name] = value;
}

void GobLang::Machine::collectGarbage()
{
    MemoryNode *prev = m_memoryRoot;
    MemoryNode *curr = m_memoryRoot->getNext();
    while (curr != nullptr)
    {
        if (!curr->isDead())
        {
            prev = curr;
            curr = curr->getNext();
            continue;
        }
        // if we are deleting then prev should stay the same while
        // curr gets deleted
        MemoryNode *del = curr;
        prev->eraseNext();
        curr = prev->getNext();

        delete del;
    }
}

GobLang::Machine::~Machine()
{
    MemoryNode *root = m_memoryRoot->getNext();
    while (root != nullptr)
    {
        MemoryNode *del = root;
        root = root->getNext();
        delete del;
    }
    delete m_memoryRoot;
}

GobLang::ProgramAddressType GobLang::Machine::_getAddressFromByteCode(size_t start)
{
    ProgramAddressType reconAddr = 0x0;
    for (int32_t i = 0; i < sizeof(ProgramAddressType); i++)
    {
        ProgramAddressType offset = (sizeof(ProgramAddressType) - i - 1) * 8;
        reconAddr |= (ProgramAddressType)(m_operations[start + i]) << offset;
    }
    return reconAddr;
}

void GobLang::Machine::_jump()
{
    ProgramAddressType dest = _getAddressFromByteCode(m_programCounter + 1);
    m_programCounter = dest;
}

void GobLang::Machine::_jumpIf()
{
    ProgramAddressType dest = _getAddressFromByteCode(m_programCounter + 1);
    m_programCounter += sizeof(ProgramAddressType);
    MemoryValue a = *m_operationStack.rbegin();
    if (a.type == Type::Bool)
    {
        if (!std::get<bool>(a.value))
        {
            m_programCounter = dest;
        }
        else
        {
            m_programCounter++;
        }
    }
    else
    {
        throw RuntimeException(std::string("Invalid data type passed to condition check. Expected bool got: ") + typeToString(a.type));
    }
}

void GobLang::Machine::_addInt()
{
    MemoryValue a = m_operationStack[m_operationStack.size() - 1];
    MemoryValue b = m_operationStack[m_operationStack.size() - 2];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    Value c = std::get<int32_t>(a.value) + std::get<int32_t>(b.value);
    m_operationStack.push_back(MemoryValue{.type = Type::Int, .value = c});
}

void GobLang::Machine::_subInt()
{
    MemoryValue a = m_operationStack[m_operationStack.size() - 1];
    MemoryValue b = m_operationStack[m_operationStack.size() - 2];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    Value c = std::get<int32_t>(b.value) - std::get<int32_t>(a.value);
    m_operationStack.push_back(MemoryValue{.type = Type::Int, .value = c});
}

void GobLang::Machine::_set()
{
    // (name val =)
    MemoryValue val = m_operationStack[m_operationStack.size() - 1];
    MemoryValue name = m_operationStack[m_operationStack.size() - 2];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    StringNode *memStr = dynamic_cast<StringNode *>(std::get<MemoryNode *>(name.value));
    if (memStr != nullptr)
    {
        if (val.type == Type::MemoryObj)
        {
            std::get<MemoryNode *>(val.value)->increaseRefCount();
        }
        if (m_globals.count(memStr->getString()) > 0 && m_globals[memStr->getString()].type == Type::MemoryObj)
        {
            std::get<MemoryNode *>(m_globals[memStr->getString()].value)->decreaseRefCount();
        }
        m_globals[memStr->getString()] = val;
    }
}

void GobLang::Machine::_get()
{
    MemoryValue name = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    assert(std::holds_alternative<MemoryNode *>(name.value));
    StringNode *memStr = dynamic_cast<StringNode *>(std::get<MemoryNode *>(name.value));
    if (memStr != nullptr)
    {
        if (m_globals.count(memStr->getString()) < 1)
        {
            throw RuntimeException(std::string("Attempted to get variable '" + memStr->getString() + "', which doesn't exist"));
        }
        m_operationStack.push_back(m_globals[memStr->getString()]);
    }
}

void GobLang::Machine::_setLocal()
{
    MemoryValue val = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_programCounter++;
    uint8_t id = m_operations[m_programCounter];
    setLocalVariableValue(id, val);
}

void GobLang::Machine::_getLocal()
{
    m_programCounter++;
    uint8_t id = m_operations[m_programCounter];
    if (MemoryValue *val = getLocalVariableValue(id); val != nullptr)
    {
        m_operationStack.push_back(*val);
    }
    else
    {
        throw RuntimeException(std::string("Attempted to retrieve value of variable ") + std::to_string(id) + ", but no variable uses this id");
    }
}

void GobLang::Machine::_call()
{
    MemoryValue func = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    if (std::holds_alternative<FunctionValue>(func.value))
    {
        std::get<FunctionValue>(func.value)(this);
    }
    else
    {
        throw RuntimeException("Attempted to call a function, but top of the stack doesn't contain a function");
    }
}

void GobLang::Machine::_pushConstInt()
{
    m_operationStack.push_back(MemoryValue{.type = Type::Int, .value = m_constInts[(size_t)m_operations[m_programCounter + 1]]});
    m_programCounter++;
}

void GobLang::Machine::_pushConstChar()
{
    m_operationStack.push_back(MemoryValue{.type = Type::Char, .value = (char)m_operations[m_programCounter + 1]});
    m_programCounter++;
}

void GobLang::Machine::_pushConstString()
{
    MemoryNode *root = m_memoryRoot;
    std::string &str = m_constStrings[(size_t)m_operations[m_programCounter + 1]];
    // we always create a new string object because otherwise each variable will share same pointer to constant string which can be altered
    StringNode *node = createString(str, true);

    m_programCounter++;
    m_operationStack.push_back(MemoryValue{.type = Type::MemoryObj, .value = node});
}

void GobLang::Machine::_getArray()
{
    MemoryValue index = m_operationStack[m_operationStack.size() - 2];
    MemoryValue array = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    if (!std::holds_alternative<MemoryNode *>(array.value))
    {
        throw RuntimeException(std::string("Attempted to get array value, but array has instead type: ") + typeToString(array.type));
    }
    if (!std::holds_alternative<int32_t>(index.value))
    {
        throw RuntimeException(std::string("Attempted to get array value, but index has instead type: ") + typeToString(array.type));
    }
    if (ArrayNode *arrNode = dynamic_cast<ArrayNode *>(std::get<MemoryNode *>(array.value)); arrNode != nullptr)
    {
        m_operationStack.push_back(*arrNode->getItem(std::get<int32_t>(index.value)));
    }
    else if (StringNode *strNode = dynamic_cast<StringNode *>(std::get<MemoryNode *>(array.value)); strNode != nullptr)
    {
        m_operationStack.push_back(MemoryValue{.type = Type::Char, .value = strNode->getCharAt(std::get<int32_t>(index.value))});
    }
}

void GobLang::Machine::_setArray()
{
    MemoryValue index = m_operationStack[m_operationStack.size() - 3];
    MemoryValue array = m_operationStack[m_operationStack.size() - 2];
    MemoryValue value = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    if (!std::holds_alternative<MemoryNode *>(array.value))
    {
        throw RuntimeException(std::string("Attempted to set array value, but array has instead type: ") + typeToString(array.type));
    }
    if (!std::holds_alternative<int32_t>(index.value))
    {
        throw RuntimeException(std::string("Attempted to set array value, but index has instead type: ") + typeToString(array.type));
    }
    MemoryNode *m = std::get<MemoryNode *>(array.value);
    if (ArrayNode *arrNode = dynamic_cast<ArrayNode *>(m); arrNode != nullptr)
    {
        arrNode->setItem(std::get<int32_t>(index.value), value);
    }
    else if (StringNode *strNode = dynamic_cast<StringNode *>(m); strNode != nullptr && value.type == Type::Char)
    {
        strNode->setCharAt(std::get<char>(value.value), std::get<int32_t>(index.value));
    }
}

void GobLang::Machine::_eq()
{
    MemoryValue a = m_operationStack[m_operationStack.size() - 2];
    MemoryValue b = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    if (a.type == b.type)
    {
        m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = areEqual(a, b)});
    }
    else
    {
        throw RuntimeException(std::string("Attempted to compare value of ") + typeToString(a.type) + " and " + typeToString(b.type));
    }
}

void GobLang::Machine::_neq()
{
    MemoryValue a = m_operationStack[m_operationStack.size() - 2];
    MemoryValue b = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    if (a.type != b.type)
    {
        throw RuntimeException(std::string("Attempted to compare value of ") + typeToString(a.type) + " and " + typeToString(b.type));
    }
    else
    {
        m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = !areEqual(a, b)});
    }
}

void GobLang::Machine::_and()
{
    MemoryValue a = m_operationStack[m_operationStack.size() - 2];
    MemoryValue b = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    if (a.type != b.type && a.type != Type::Bool)
    {
        throw RuntimeException(std::string("Attempted to 'and' values of ") + typeToString(a.type) + " and " + typeToString(b.type));
    }
    else
    {
        m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = std::get<bool>(a.value) && std::get<bool>(b.value)});
    }
}

void GobLang::Machine::_or()
{
    MemoryValue a = m_operationStack[m_operationStack.size() - 2];
    MemoryValue b = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    if (a.type != b.type && a.type != Type::Bool)
    {
        throw RuntimeException(std::string("Attempted to 'or' values of ") + typeToString(a.type) + " and " + typeToString(b.type));
    }
    else
    {
        m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = std::get<bool>(a.value) || std::get<bool>(b.value)});
    }
}

void GobLang::Machine::_less()
{
    MemoryValue a = m_operationStack[m_operationStack.size() - 2];
    MemoryValue b = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    if (a.type != b.type)
    {
        throw RuntimeException(std::string("Attempted to compare value of ") + typeToString(a.type) + " and " + typeToString(b.type));
    }
    else
    {
        switch (a.type)
        {
        case Type::Int:
            m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = std::get<int32_t>(a.value) < std::get<int32_t>(b.value)});
            break;
        case Type::Number:
            m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = std::get<float>(a.value) < std::get<int32_t>(b.value)});
            break;
        default:
            throw RuntimeException(std::string("Attempted to compare value of type ") + typeToString(a.type) + ". Only numeric types can be compared using >,<, <=, >=");
        }
    }
}

void GobLang::Machine::_more()
{
    MemoryValue a = m_operationStack[m_operationStack.size() - 2];
    MemoryValue b = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    if (a.type != b.type)
    {
        throw RuntimeException(std::string("Attempted to compare value of ") + typeToString(a.type) + " and " + typeToString(b.type));
    }
    else
    {
        switch (a.type)
        {
        case Type::Int:
            m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = std::get<int32_t>(a.value) > std::get<int32_t>(b.value)});
            break;
        case Type::Number:
            m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = std::get<float>(a.value) > std::get<int32_t>(b.value)});
            break;
        default:
            throw RuntimeException(std::string("Attempted to compare value of type ") + typeToString(a.type) + ". Only numeric types can be compared using >,<, <=, >=");
        }
    }
}

void GobLang::Machine::_lessOrEq()
{
    MemoryValue a = m_operationStack[m_operationStack.size() - 2];
    MemoryValue b = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    if (a.type != b.type)
    {
        throw RuntimeException(std::string("Attempted to compare value of ") + typeToString(a.type) + " and " + typeToString(b.type));
    }
    else
    {
        switch (a.type)
        {
        case Type::Int:
            m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = std::get<int32_t>(a.value) <= std::get<int32_t>(b.value)});
            break;
        case Type::Number:
            m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = std::get<float>(a.value) <= std::get<int32_t>(b.value)});
            break;
        default:
            throw RuntimeException(std::string("Attempted to compare value of type ") + typeToString(a.type) + ". Only numeric types can be compared using >,<, <=, >=");
        }
    }
}

void GobLang::Machine::_moreOrEq()
{
    MemoryValue a = m_operationStack[m_operationStack.size() - 2];
    MemoryValue b = m_operationStack[m_operationStack.size() - 1];
    m_operationStack.pop_back();
    m_operationStack.pop_back();
    if (a.type != b.type)
    {
        throw RuntimeException(std::string("Attempted to compare value of ") + typeToString(a.type) + " and " + typeToString(b.type));
    }
    else
    {
        switch (a.type)
        {
        case Type::Int:
            m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = std::get<int32_t>(a.value) >= std::get<int32_t>(b.value)});
            break;
        case Type::Number:
            m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = std::get<float>(a.value) >= std::get<int32_t>(b.value)});
            break;
        default:
            throw RuntimeException(std::string("Attempted to compare value of type ") + typeToString(a.type) + ". Only numeric types can be compared using >,<, <=, >=");
        }
    }
}

void GobLang::Machine::_negate()
{
    MemoryValue val = *m_operationStack.rbegin();
    m_operationStack.pop_back();
    switch (val.type)
    {
    case Type::Int:
        m_operationStack.push_back(MemoryValue{.type = Type::Int, .value = -std::get<int32_t>(val.value)});
        break;
    case Type::Number:
        m_operationStack.push_back(MemoryValue{.type = Type::Number, .value = -std::get<float>(val.value)});
        break;
    default:
        throw RuntimeException("Attempted to apply negate operation on a non numeric value");
    }
}

void GobLang::Machine::_not()
{
    MemoryValue val = *m_operationStack.rbegin();
    m_operationStack.pop_back();
    if (val.type != Type::Bool)
    {
        throw RuntimeException("Attempted to negate non boolean value");
    }
    m_operationStack.push_back(MemoryValue{.type = Type::Bool, .value = !std::get<bool>(val.value)});
}

void GobLang::Machine::_shrink()
{
    m_programCounter++;
    size_t amount = (size_t)m_operations[m_programCounter];
    shrinkLocalVariableStackBy(amount);
}
