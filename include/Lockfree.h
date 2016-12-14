//-------------------------------------------------------------------------------------------------
// File : Lockfree.h
// Desc : Lock-Free Container.
// Copyright(c) Project Asura. All right reserved.
//-------------------------------------------------------------------------------------------------
#pragma once


//-------------------------------------------------------------------------------------------------
// Includes
//-------------------------------------------------------------------------------------------------
#include <cstdint>
#if defined(_WIN32) || defined(WIN32) || defined(WIN64)
    #include <Windows.h>
#endif


///////////////////////////////////////////////////////////////////////////////////////////////////
// Node structure
///////////////////////////////////////////////////////////////////////////////////////////////////
template<typename T>
struct Node
{
    T                   Value;  //!< 値です.
    volatile Node<T>*   pNext;  //!< 次のノードへのポインタです.
};

template<typename T> bool Cas (volatile Node<T>** ptr, Node<T>* oldValue, Node<T>* newValue);
template<typename T> bool Cas2(volatile Node<T>** ptr, Node<T>* old1, uint32_t old2, Node<T>* new1, uint32_t new2);


///////////////////////////////////////////////////////////////////////////////////////////////////
// LockfreeStack class
///////////////////////////////////////////////////////////////////////////////////////////////////
template<typename T>
class LockfreeStack
{
public:
    LockfreeStack()
    : m_pHead   (nullptr)
    , m_Pops    (0)
    { /* DO_NOTHING */ }

    ~LockfreeStack()
    { /* DO_NOTHING */ }

    bool push(const T& value);
    bool pop(T& value);

private:
    volatile Node<T>*   m_pHead;
    volatile uint32_t   m_Pops;

    LockfreeStack   (const LockfreeStack&) = delete;
    void operator = (const LockfreeStack&) = delete;
};


///////////////////////////////////////////////////////////////////////////////////////////////////
// LockfreeQueue class
///////////////////////////////////////////////////////////////////////////////////////////////////
template<typename T>
class LockfreeQueue
{
public:
    LockfreeQueue()
    : m_pHead   (nullptr)
    , m_pTail   (nullptr)
    , m_Pops    (0)
    , m_Pushes  (0)
    { /* DO_NOTHING */ }

    ~LockfreQueue()
    { /* DO_NOTHING */ }

    bool push(const T& value);
    bool pop(T& value);

private:
    volatile Node<T>*   m_pHead;
    volatile Node<T>*   m_pTail;
    volatile uint32_t   m_Pops;
    volatile uint32_t   m_Pushes;

    LockfreeQueue   (const LockfreeQueue&) = delete;
    void operator = (const LockfreeQueue&) = delete;
};



#if defined(_WIN32) || defined(WIN32) || defined(WIN64)
template<typename T>
inline bool Cas(volatile Node<T>** ptr, Node<T>* oldValue, Node<T>* newValue)
{
    return InterlockedCompareExchange(
        reinterpret_cast<uint32_t volatile*>(ptr),
        reinterpret_cast<uint32_t>(newValue),
        reinterpret_cast<uint32_t>(oldValue)) == reinterpret_cast<uint32_t>(oldValue);
}

template<typename T>
inline bool Cas2(volatile Node<T>** ptr, Node<T>* old1, uint32_t old2, Node<T>* new1, uint32_t new2)
{
    LONG64 compared = reinterpret_cast<LONG>(old1) | (static_cast<LONG64>(old2) << 32);
    LONG64 exchange = reinterpret_cast<LONG>(new1) | (static_cast<LONG64>(new2) << 32);

    return InterlockedCompareExchange64(
        reinterpret_cast<volatile LONG64*>(ptr), exchange, compared) == compared;
}

#else
template <typename T>
inline bool Cas(volatile Node<T>** ptr, Node<T>* oldValue, Node<T>* newValue)
{
    return __sync_bool_compare_and_swap(
        reinterpret_cast<uint32_t volatile*>(ptr),
        static_cast<uint32_t>(oldValue),
        static_cast<uint32_t>(newValue));
}

template <typename T>
inline bool Cas2(volatile Node<T>** ptr, Node<T>* old1, uint32_t old2, Node<T>* new1, uint32_t new2)
{
    uint64_t compared = reinterpret_cast<uint32_t>(old1) | (static_cast<uint64_t>(old2) << 32);
    uint64_t exchange = reinterpret_cast<uint32_t>(new1) | (static_cast<uint64_t>(new2) << 32);

    return __sync_bool_compare_and_swap(
        reinterpret_cast<volatile uint64_t*>(ptr), compared, exchanged);
}
#endif


template<typename T>
inline bool LockfreeStack::push(const T& value)
{
    auto node = new(std::nothrow) Node<T>();
    if (node == nullptr)
    { return false; }

    node->Value = value;
    node->pNext = nullptr;

    while (true)
    {
        node->pNext = m_pHead;
        if (!Cas(&m_pHead, node->pNext, node))
        { break; }
    }

    return true;
}

template<typename T>
inline bool LockfreeStack::pop(T& value)
{
    while (true)
    {
        Node<T>* pHead = m_pHead;
        uint32_t pops  = m_Pops;
        if (pHead == nullptr)
        { return false; }

        Node<T>* pNext = pHead->pNext;
        if (Cas2(&m_pHead, pHead, pops, pNext, pops + 1))
        {
            value = pHead->Value;
            delete pHead;
            return true;
        }
    }
}

template<typename T>
inline bool LockfreeQueue::push(const T& value)
{
    auto node = new(std::nothrow) Node<T>();
    if (node == nullptr)
    { return false; }

    node->Value = value;
    node->pNext = nullptr;

    uint32_t pushes = 0;
    Node<T>* pTail = nullptr;

    while (true)
    {
        pushes = m_Pushes;
        pTail = m_pTail;

        if (Cas(&(m_pTail->pNext), reinterpret_cast<Node<T>*>(nullptr), node))
        { break; }
        else
        { Cas2(&m_pTail, pTail, pushes, m_pTail->pNext, pushes + 1); }
    }

    Cas2(&m_pTail, pTail, pushes, pNode, pushes + 1);
}

template<typename T>
inline bool LockfreeQueue::pop(T& value)
{
    Node<T>* pHead = nullptr;

    while (true)
    {
        uint32_t pops = m_Pops;
        uint32_t pushes = m_Pushes;
        pHead = m_pHead;
        Node<T>* pNext = pHead->pNext;

        if (pops != m_Pops)
        { continue; }

        if (pHead == m_pTail)
        {
            if (pNext == nullptr)
            {
                pHead = nullptr;
                break;
            }

            Cas2(&m_pTail, pHead, pushes, pNext, pushes + 1);
        }
        else if (pNext != nullptr)
        {
            value = pNext->Value;
            if (Cas2(&m_pHead, pHead, pops, pNext, pops + 1))
            { break; }
        }
    }

    if (pHead != nullptr)
    {
        delete pHead;
        return true;
    }

    return false;
}
