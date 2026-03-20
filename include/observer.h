
#pragma once

#include <queue>
#include <map>
#include "semaphore.h"
#include <iterator>

template <typename T, typename Condition = void>
class Conditional_Data_Observer;

template <typename T, typename Condition = void>
class Conditionally_Data_Observed;
/* 
isso aqui, até onde eu entendi, é so uma "visão" se tu ver la na classe da nic
tem uns metodos atach detach, que é tipo essa isso aqui, da pra criar uma definiçãozinha com uma funções
viruais (abstratas)
*/

template<typename D, typename C = void>
class Concurrent_Observed;

template<typename D, typename C>
class Concurrent_Observer
{
    friend class Concurrent_Observed<D, C>;
public:
    typedef D Observed_Data;
    typedef C Observing_Condition;
public:

    Concurrent_Observer(): _semaphore(0) {}

    ~Concurrent_Observer() {}

    // tem que botar um mutex pra fazer um guard na queue, mas eu não tenho certeza

    void update(C c, D * d) {
        _data.push(d);
        _semaphore.v();
    }
    D * updated() {
        _semaphore.p();
        D* r = _data.front()
        return 
    }
private:
    Semaphore _semaphore;

    // aqui seria bom ter uma estrutura de dados custom, pra diminuir o tempo em runtime
    // pointer pra evitar cópia de objetos grandes
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

    // typedef Ordered_List<Concurrent_Observer<D, C>, C> Observers;
    // *implementar um ring buffer ,ou algo assim, pra diminuir o tempo em runtime
    typedef std::vector<Entry> Observers;

public:

    Concurrent_Observed() {}

    ~Concurrent_Observed() {}

    // talvez tenha que  botar um mutex pra fazer um guard em _observers, que atualmente não é exatamente thread safe

    void attach(Concurrent_Observer<D, C> * o, C c) {

        _observers.push_back({o, c})
    }
 
    void detach(Concurrent_Observer<D, C> * o, C c) {

        // aparentemente tem um jeito melhor de fazer isso com umas coisas mais modernas do c++, mas eu tenho que olhar isso com mais cuidado ainda

        for (size_t i = 0; i < _observers.size(); i++){
            Entry e = _observers.at(i);
            if (e.observer == o && e.condition == c){
                _observers.erase(i);
            }
        }
        
    }
    
    bool notify(C c, D * d) {
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