
#pragma once

#include <queue>
#include <map>
#include <vector>
#include "../posix_semaphore.h"
#include <iterator>
#include <mutex>
#include <algorithm>

template <typename T, typename Condition = void>
class Conditionally_Data_Observed;

template <typename T, typename Condition = void>
class Conditional_Data_Observer{
    friend class Conditionally_Data_Observed<T, Condition>;
public:
    typedef T Observed_Data;
    typedef Condition Observing_Condition;

    Conditional_Data_Observer() {}

    ~Conditional_Data_Observer() {}

    // = 0 torna a classe abstrata. primeiro parametro é o observed que chamou o update
    virtual void update(Condition c, T * t) = 0;
};

template <typename T, typename Condition>
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
        // lock_guard: trava o mutex e destrava automaticamente ao sair do escopo
        // necessario porque attach pode ser chamado da thread principal enquanto notify roda no recv_loop
        std::lock_guard<std::mutex> lock(_mtx);
        _observers.push_back({o, c});
    }

    void detach(Conditional_Data_Observer<T, Condition> * o, Condition c) {
        std::lock_guard<std::mutex> lock(_mtx);
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
        // snapshot: copia o vetor com o lock, solta o lock, itera na cópia
        // evita deadlock caso update() chame detach(). sem snapshot, detach() tentaria
        // travar o mesmo mutex que notify() já segura
        Observers snapshot;
        {
            std::lock_guard<std::mutex> lock(_mtx);
            snapshot = _observers;
        }
        bool notified = false;
        for (auto& e : snapshot){
            if (e.condition == c){
                e.observer->update(c, d);
                notified = true;
            }
        }
        return notified;
    }
private:
    Observers _observers;
    std::mutex _mtx; // protege _observers contra acesso concorrente entre attach/detach/notify
};
