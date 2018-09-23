#ifndef __C_CONCURRENT_OBJECT_CACHE_H_INCLUDED__
#define __C_CONCURRENT_OBJECT_CACHE_H_INCLUDED__

#include "CObjectCache.h"
#include "../source/Irrlicht/FW_Mutex.h"

namespace irr { namespace core
{

namespace impl
{
    struct CConcurrentObjectCacheBase
    {
        CConcurrentObjectCacheBase() = default;
        // explicitely making concurrent caches non-copy-and-move-constructible and non-copy-and-move-assignable
        CConcurrentObjectCacheBase(const CConcurrentObjectCacheBase&) = delete;
        CConcurrentObjectCacheBase(CConcurrentObjectCacheBase&&) = delete;
        CConcurrentObjectCacheBase& operator=(const CConcurrentObjectCacheBase&) = delete;
        CConcurrentObjectCacheBase& operator=(CConcurrentObjectCacheBase&&) = delete;

        struct
        {
            void lockRead() const { FW_AtomicCounterIncr(ctr); }
            void unlockRead() const { FW_AtomicCounterDecr(ctr); }
            void lockWrite() const { FW_AtomicCounterBlock(ctr); }
            void unlockWrite() const { FW_AtomicCounterUnBlock(ctr); }

        private:
            mutable FW_AtomicCounter ctr = 0;
        } m_lock;
    };

    template<typename CacheT>
    class CMakeCacheConcurrent : private impl::CConcurrentObjectCacheBase, private CacheT
    {
        using BaseCache = CacheT;
        using K = typename BaseCache::KeyType_impl;
        using T = typename BaseCache::CachedType;

        // F must be zero-args, non-void return-type callable
        template<typename F, typename R = typename std::result_of<F>::type>
        R threadSafeRead(F _f) const
        {
            this->m_lock.lockRead();
            const R r = static_cast<const BaseCache&>(*this).*_f();
            this->m_lock.unlockRead();
            return r;
        }

    public:
        using IteratorType = typename BaseCache::IteratorType;
        using ConstIteratorType = typename BaseCache::ConstIteratorType;
        using RevIteratorType = typename BaseCache::RevIteratorType;
        using ConstRevIteratorType = typename BaseCache::ConstRevIteratorType;
        using RangeType = typename BaseCache::RangeType;
        using ConstRangeType = typename BaseCache::ConstRangeType;
        using PairType = typename BaseCache::PairType;
        using MutablePairType = typename BaseCache::MutablePairType;

        using BaseCache::BaseCache;

        // writing/reading from/to retrieved iterators is not thread safe and iterators can become invalidated at any time!
        inline ConstIteratorType begin() const { return threadSafeRead(CacheBase::begin); }
        inline ConstIteratorType end() const { return threadSafeRead(CacheBase::end); }
        inline ConstIteratorType cbegin() const { return threadSafeRead(CacheBase::cbegin); }
        inline ConstIteratorType cend() const { return threadSafeRead(CacheBase::cend); }
        inline ConstRevIteratorType crbegin() const { return threadSafeRead(CacheBase::crbegin); }
        inline ConstRevIteratorType crend() const { return threadSafeRead(CacheBase::crend); }

        template<typename RngT>
        static bool isNonZeroRange(const RngT& _rng) { return BaseCache::isNonZeroRange(_rng); }

        inline bool insert(const K& _key, T* _val)
        {
            this->m_lock.lockWrite();
            const bool r = BaseCache::insert(_key, _val);
            this->m_lock.unlockWrite();
            return r;
        }

        inline bool contains(const T* _object) const
        {
            this->m_lock.lockRead();
            const bool r = BaseCache::contains(_object);
            this->m_lock.unlockRead();
            return r;
        }

        inline size_t getSize() const
        {
            this->m_lock.lockRead();
            const size_t r = BaseCache::getSize();
            this->m_lock.unlockRead();
            return r;
        }

        inline void clear()
        {
            this->m_lock.lockWrite();
            BaseCache::clear();
            this->m_lock.unlockWrite();
        }

        //! Returns true if had to insert
        bool swapObjectValue(const K& _key, const const T* _obj, T* _val)
        {
            this->m_lock.lockWrite();
            bool r = BaseCache::swapObjectValue(_key, _obj, _val);
            this->m_lock.unlockWrite();
            return r;
        }

        bool getKeyRangeOrReserve(typename RangeType* _outrange, const K& _key)
        {
            this->m_lock.lockWrite();
            bool r = BaseCache::getKeyRangeOrReserve(_outrange, _key);
            this->m_lock.unlockWrite();
            return r;
        }

        inline typename RangeType findRange(const K& _key)
        {
            this->m_lock.lockRead();
            typename RangeType r = BaseCache::findRange(_key);
            this->m_lock.unlockRead();
            return r;
        }

        inline const typename RangeType findRange(const K& _key) const
        {
            this->m_lock.lockRead();
            const typename RangeType r = BaseCache::findRange(_key);
            this->m_lock.unlockRead();
            return r;
        }

        inline bool removeObject(T* _object, const K& _key)
        {
            this->m_lock.lockWrite();
            const bool r = BaseCache::removeObject(_object, _key);
            this->m_lock.unlockWrite();
            return r;
        }

        inline size_t findAndStoreRange(const typename K& _key, size_t _storageSize, typename MutablePairType* _out, bool* _gotAll)
        {
            m_lock.lockRead();
            const size_t r = BaseCache::findAndStoreRange(_key, _storageSize, _out, _gotAll);
            m_lock.unlockRead();
            return r;
        }

        inline size_t findAndStoreRange(const typename K& _key, size_t _storageSize, typename MutablePairType* _out, bool* _gotAll) const
        {
            m_lock.lockRead();
            const size_t r = BaseCache::findAndStoreRange(_key, _storageSize, _out, _gotAll);
            m_lock.unlockRead();
            return r;
        }

        inline size_t outputAll(size_t _storageSize, MutablePairType* _out, bool* _gotAll) const
        {
            m_lock.lockRead();
            const size_t r = BaseCache::outputAll(_storageSize, _out, _gotAll);
            m_lock.unlockRead();
            return r;
        }
    };
}

template<
    typename K,
    typename T,
    template<typename...> class ContainerT_T = std::vector
>
using CConcurrentObjectCache =
    impl::CMakeCacheConcurrent<
        CObjectCache<K, T, ContainerT_T>
    >;

template<
    typename K,
    typename T,
    template<typename...> class ContainerT_T = std::vector
>
using CConcurrentMultiObjectCache = 
    impl::CMakeCacheConcurrent<
        CMultiObjectCache<K, T, ContainerT_T>
    >;

}}

#endif
