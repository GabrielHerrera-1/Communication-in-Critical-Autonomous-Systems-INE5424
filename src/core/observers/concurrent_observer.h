
#pragma once

#include <queue>
#include <map>
#include <vector>
#include "../posix_semaphore.h"
#include <iterator>
#include <mutex>
#include <algorithm>

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

    virtual ~Concurrent_Observer() {}

    // virtual: subclasses (ex.: SmartData, como observer do Communicator)
    // sobrescrevem update() para interpretar a mensagem na hora, em vez de so
    // empilhar. O notify() do Concurrent_Observed chama via Concurrent_Observer*,
    // entao precisa ser virtual para o override ser despachado.
    virtual void update(C c, D * d) {
        // lock_guard em vez de lock/unlock manual: se funcao lançar exceção,
        // o destrutor do lock_guard garante que o mutex é destravado
        std::lock_guard<std::mutex> lock(_mtx);
        _data.push(d);
        _semaphore.v();
    }
    // timeout_ms < 0 bloqueia indefinidamente; >= 0 espera ate o timeout e
    // devolve nullptr se esgotar (usado pelo consumidor: receive_response).
    D * updated(int timeout_ms = -1) {
        if (timeout_ms < 0) {
            _semaphore.p();
        } else if (!_semaphore.p_for(timeout_ms)) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(_mtx);
        if (_data.empty()) return nullptr;
        D* r = _data.front();
        _data.pop();
        return r;
    }
private:
    Semaphore _semaphore;
    std::mutex _mtx;
    std::queue<D*> _data;
};

template<typename D, typename C>
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
        std::lock_guard<std::mutex> lock(_mtx);
        _observers.push_back({o, c});
    }

    void detach(Concurrent_Observer<D, C> * o, C c) {
        std::lock_guard<std::mutex> lock(_mtx);
        // tira os elementos que dão true no lambda da range begin - end (a range não é alterada pelo movimento dos elementos)
        auto it = std::remove_if(_observers.begin(), _observers.end(),
        [&](const Concurrent_Observer_Entry& e) {
            return e.observer == o && e.condition == c;
        });
        // it é o priemeiro iterator deposi do end "antigo"
        // agora apagamos tudo fora da range antiga
        _observers.erase(it, _observers.end());
    }

    bool notify(C c, D * d) {
        // snapshot: mesma razão do Conditionally_Data_Observed
        // copia o vetor com lock, solta lock, itera na cópia sem risco de deadlock
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

    // true se ha algum observer anexado para a condicao c. O Communicator usa
    // isto para decidir entre "unmarshal + notify" (ha SmartData escutando) e
    // "empilhar o buffer para receive()" (componente legado, sem observers).
    bool has_observer(C c) {
        std::lock_guard<std::mutex> lock(_mtx);
        for (auto& e : _observers) {
            if (e.condition == c) return true;
        }
        return false;
    }

private:
    Observers _observers;
    std::mutex _mtx;
};
