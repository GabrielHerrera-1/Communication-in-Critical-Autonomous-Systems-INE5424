
#pragma once

#include <queue>
#include <map>
#include "semaphore.h"
#include <iterator>
#include <mutex>
#include <algorithm>

template <typename T, typename Condition = void>
class Conditionally_Data_Observed;

template <typename T, typename Condition = void>
class Conditional_Data_Observer{
    friend class Conditionally_Data_Observed<T, Condition>;
public:

    Conditional_Data_Observer() {}

    ~Conditional_Data_Observer() {}

    virtual void update(Condition c, T * t);
};

template <typename T, typename Condition = void>
class Conditionally_Data_Observed{
        friend class Conditional_Data_Observer<T, Condition>;

public:

    struct Conditional_Data_Observer_Entry {
        Conditional_Data_Observer<T, Condition>* observer;
        Condition condition;
    };
    
    typedef std::vector<Conditional_Data_Observer_Entry> Observers;

public:

    Conditionally_Data_Observed(){
        _observers.reserve(32);
    }

    ~Conditionally_Data_Observed() {}

    void attach(Conditional_Data_Observer<T, Condition> * o, Condition c) {
        _observers.push_back({o, c});

    }
 
    void detach(Conditional_Data_Observer<T, Condition> * o, Condition c) {
        // tira os elementos que dão true no lambda da range begin - end (a range não é alterada pelo movimento dos elementos)
        auto it = std::remove_if(_observers.begin(), _observers.end(),
        [&](const Conditional_Data_Observer_Entry& e) { 
            return e.observer == o && e.condition == c; 
        });
        // it é o priemeiro iterator deposi do end "antigo"
        // agora apagamos tudo fora da range antiga
        _observers.erase(it, _observers.end());
        
    }
    
    bool notify(Condition c, T * d) {
        // caso seja garantido qeu atach/detach sejam so realizados durante o "startup" seria possível tirar o safe guard, mas como não é
        bool notified = false;
        for (auto& e : _observers){
            if (e.condition == c){
                e.observer->update(c, d);
                notified = true;
            }
        }
        return notified;
        
    }
private:
    Observers _observers;
};


// both the observer and observed use mutex to be thread safe
template<typename D, typename C = void>
class Concurrent_Observed;

template<typename D, typename C = void>
class Concurrent_Observer
{
    friend class Concurrent_Observed<D, C>;
public:
    typedef D Observed_Data;
    typedef C Observing_Condition;
public:

    Concurrent_Observer(): _semaphore(0) {}

    ~Concurrent_Observer() {}

    void update(C c, D * d) {
        _mtx.lock();
        _data.push(d);
        _mtx.unlock();
        
        _semaphore.v();
    }
    D * updated() {
        _semaphore.p();

        _mtx.lock();
        D* r = _data.front();
        _data.pop();
        _mtx.unlock();
        return r;
    }
private:
    Semaphore _semaphore;
    std::mutex _mtx;
    std::queue<D*> _data;
};

template<typename D, typename C = void>
class Concurrent_Observed
{   
    friend class Concurrent_Observer<D, C>;

public:

    struct Concurrent_Observer_Entry {
        Concurrent_Observer<D, C>* observer;
        C condition;
    };
    typedef D Observed_Data;
    typedef C Observing_Condition;
    // *intrusive linked list (mas por agora vector vai funcionar bem)
    typedef std::vector<Concurrent_Observer_Entry> Observers;

public:

    Concurrent_Observed(){
        // evita alocações (só se não passar de 32 itens durante a execução)
        _observers.reserve(32);
    }

    ~Concurrent_Observed() {}

    void attach(Concurrent_Observer<D, C> * o, C c) {
        _mtx.lock();
        _observers.push_back({o, c});
        _mtx.unlock();
    }
 
    void detach(Concurrent_Observer<D, C> * o, C c) {
        _mtx.lock();
        // tira os elementos que dão true no lambda da range begin - end (a range não é alterada pelo movimento dos elementos)
        auto it = std::remove_if(_observers.begin(), _observers.end(),
        [&](const Concurrent_Observer_Entry& e) { 
            return e.observer == o && e.condition == c; 
        });
        // it é o priemeiro iterator deposi do end "antigo"
        // agora apagamos tudo fora da range antiga
        _observers.erase(it, _observers.end());
        _mtx.unlock();
        
    }
    
    bool notify(C c, D * d) {
        // caso seja garantido qeu atach/detach sejam so realizados durante o "startup" seria possível tirar o safe guard, mas como não é
        _mtx.lock();
        bool notified = false;
        for (auto& e : _observers){
            if (e.condition == c){
                e.observer->update(c, d);
                notified = true;
            }
        }
        _mtx.unlock();
        return notified;
        
    }
private:
    Observers _observers;
    std::mutex _mtx;
};