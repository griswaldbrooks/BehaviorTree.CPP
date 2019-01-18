#ifndef BLACKBOARD_H
#define BLACKBOARD_H

#include <iostream>
#include <string>
#include <memory>
#include <stdint.h>
#include <unordered_map>
#include <mutex>
#include <sstream>

#include "behaviortree_cpp/utils/safe_any.hpp"
#include "behaviortree_cpp/exceptions.h"

namespace BT
{

/**
 * @brief The Blackboard is the mechanism used by BehaviorTrees to exchange
 * typed data.
 */
class Blackboard
{
public:
    typedef std::shared_ptr<Blackboard> Ptr;

protected:
    // This is intentionally protected. Use Blackboard::create instead
    Blackboard(Blackboard::Ptr parent): parent_bb_(parent)
    {}

public:

    /** Use this static method to create an instance of the BlackBoard
    *   to share among all your NodeTrees.
    */
    static Blackboard::Ptr create(Blackboard::Ptr parent = {})
    {
        return std::shared_ptr<Blackboard>( new Blackboard(parent) );
    }

    virtual ~Blackboard() = default;

    /**
     * @brief The method getAny allow the user to access directly the type
     * erased value.
     *
     * @return the pointer or nullptr if it fails.
     */
    const SafeAny::Any* getAny(const std::string& key) const
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if( auto parent = parent_bb_.lock())
        {
            auto remapping_it = internal_to_external_.find(key);
            if( remapping_it != internal_to_external_.end())
            {
                return parent->getAny( remapping_it->second );
            }
        }
        auto it = storage_.find(key);
        return ( it == storage_.end()) ? nullptr : &(it->second.value);
    }

    SafeAny::Any* getAny(const std::string& key)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if( auto parent = parent_bb_.lock())
        {
            auto remapping_it = internal_to_external_.find(key);
            if( remapping_it != internal_to_external_.end())
            {
                return parent->getAny( remapping_it->second );
            }
        }
        auto it = storage_.find(key);
        return ( it == storage_.end()) ? nullptr : &(it->second.value);
    }

    /** Return true if the entry with the given key was found.
     *  Note that this method may throw an exception if the cast to T failed.
     */
    template <typename T>
    bool get(const std::string& key, T& value) const
    {
        const SafeAny::Any* val = getAny(key);
        if (val)
        {
            value = val->cast<T>();
        }
        return (bool)val;
    }

    /**
     * Version of get() that throws if it fails.
    */
   template <typename T>
   T get(const std::string& key) const
   {
       T value;
       bool found = get(key, value);
       if (!found)
       {
           throw RuntimeError("Missing key");
       }
       return value;
   }

    /// Update the entry with the given key
    template <typename T>
    void set(const std::string& key, const T& value)
    {
        std::unique_lock<std::mutex> lock(mutex_);

        auto it = storage_.find(key);
        if( it != storage_.end() ) // already there. check the type
        {
            const auto locked_type = it->second.locked_port_type;

            // TODO check isNumber
            if( locked_type && locked_type != &typeid(T) )
            {
                char buffer[1024];
                sprintf(buffer, "Blackboard::set() failed: once declared, the type of a port shall not change. "
                                "Declared type [%s] != current type [%s]",
                        BT::demangle( locked_type->name() ).c_str(),
                        BT::demangle( typeid(T).name() ).c_str() );
                throw LogicError( buffer );
            }
        }
        else{ // create for the first time without type_lock
            it = storage_.insert( {key, Entry()} ).first;
        }

        if( auto parent = parent_bb_.lock())
        {
            auto remapping_it = internal_to_external_.find(key);
            if( remapping_it != internal_to_external_.end())
            {
                parent->set( remapping_it->second, value );
                return;
            }
        }
        it->second.value =  SafeAny::Any(value);
    }

//    /// Return true if the BB contains an entry with the given key.
//    bool contains(const std::string& key) const
//    {
//        std::unique_lock<std::mutex> lock(mutex_);
//        return (storage_.find(key) != storage_.end());
//    }

    void setPortType(std::string key, const std::type_info* new_type)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = storage_.find(key);
        if( it == storage_.end() )
        {
            storage_.insert( { key, Entry(new_type)} );
        }
        else{
            auto old_type = it->second.locked_port_type;
            if( old_type && old_type != new_type )
            {
                char buffer[1024];
                sprintf(buffer, "Blackboard::set() failed: once declared, the type of a port shall not change. "
                                "Declared type [%s] != current type [%s]",
                        BT::demangle( old_type->name() ).c_str(),
                        BT::demangle( new_type->name() ).c_str() );
                throw LogicError( buffer );
            }
        }
    }

    const std::type_info* portType(const std::string& key)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = storage_.find(key);
        if( it == storage_.end() )
        {
            return nullptr;
        }
        return it->second.locked_port_type;
    }

  private:

    struct Entry{
        SafeAny::Any value;
        const std::type_info* locked_port_type;

        Entry(const std::type_info* type = nullptr):
            locked_port_type(type)
        {}

        Entry(SafeAny::Any&& other_any, const std::type_info* type = nullptr):
            value(std::move(other_any)),
            locked_port_type(type)
        {}
    };

    mutable std::mutex mutex_;
    std::unordered_map<std::string, Entry> storage_;
    std::weak_ptr<Blackboard> parent_bb_;
    std::unordered_map<std::string,std::string> internal_to_external_;
};

} // end namespace

#endif   // BLACKBOARD_H
