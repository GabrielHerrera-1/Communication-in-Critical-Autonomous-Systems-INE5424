
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
class Conditional_Data_Observer;

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

    struct Entry {
        Concurrent_Observer<D, C>* observer;
        C condition;
    };
    typedef D Observed_Data;
    typedef C Observing_Condition;
    // *intrusive linked list (mas por agora vector vai funcionar bem)
    typedef std::vector<Entry> Observers;

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
        [&](const Entry& e) { 
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