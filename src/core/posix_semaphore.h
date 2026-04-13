// wrapper do semaforo nativo do c para facilitar o uso

// sem o wrapper (C puro):
// sem_t sem;
// sem_init(&sem, 0, 0);
// sem_wait(&sem);
// sem_post(&sem);
// sem_destroy(&sem);
// // com o wrapper:
// Semaphore sem;
// sem.p();
// sem.v();
// destrutor limpa automaticamente

#pragma once

#include <semaphore.h>
#include <cerrno>

class Semaphore {
    public:
        Semaphore(int initial = 0) { // 0 é o valor padrão se não passar parâmetro
            sem_init(&_sem, 0, initial); // 0 ali indica que o semáforo é compartilhado entre threads do mesmo processo
        }
        ~Semaphore() { // chamado automaticamente quando o objeto sair do escopo (quando função termina por ex)
            sem_destroy(&_sem);
        }
        void p() { // decrementa o contador
            sem_wait(&_sem);
        }
        bool try_p() {
            while (sem_trywait(&_sem) < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            return true;
        }
        void v() { // incrementa o contador
            sem_post(&_sem);
        }
    private:
        sem_t _sem;
};
