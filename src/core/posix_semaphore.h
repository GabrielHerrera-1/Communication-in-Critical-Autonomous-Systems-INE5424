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
#include <ctime>
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
        // p() com timeout em ms: true se decrementou, false se esgotou o tempo.
        // usado por updated(timeout) para drenar a fila sem bloquear pra sempre.
        bool p_for(int timeout_ms) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec  += timeout_ms / 1000;
            ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
            while (sem_timedwait(&_sem, &ts) != 0) {
                if (errno == EINTR) continue; // reinicia se interrompido por sinal
                return false;                 // ETIMEDOUT ou erro
            }
            return true;
        }
        void v() { // incrementa o contador
            sem_post(&_sem);
        }
    private:
        sem_t _sem;
};
