#ifndef KRX_KDK_SOFT_COMPAT_H
#define KRX_KDK_SOFT_COMPAT_H

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define elementsof(x) (sizeof(x) / sizeof((x)[0]))
#define ROUNDUP(addr, align) (((addr) + align - 1) & ~(align - 1))
#define ROUNDDOWN(addr, align) ((((uintptr_t)addr)) & ~(align - 1))
#define PGROUNDUP(addr) ROUNDUP(addr, PGSIZE)
#define PGROUNDDOWN(addr) ROUNDDOWN(addr, PGSIZE)

typedef enum ipl { kIPL0, kIPLDPC } ipl_t;

extern __thread ipl_t	 SIM_ipl;
extern __thread uint64_t SIM_cr3;
extern __thread void	*SIM_vmps;

static inline ipl_t
splget()
{
	return SIM_ipl;
}

#define kprintf(...) printf(__VA_ARGS__)
#define kassert(...) assert(__VA_ARGS__)
#define kfatal(...)                                  \
	({                                           \
		printf("FATAL ERROR: " __VA_ARGS__); \
		for (;;)                             \
			;                            \
	})

#define KSPINLOCK_INITIALISER PTHREAD_MUTEX_INITIALIZER
typedef pthread_mutex_t kspinlock_t;

static inline ipl_t
ke_spinlock_acquire(kspinlock_t *lock)
{
	ipl_t ipl = SIM_ipl;
	pthread_mutex_lock(lock);
	return ipl;
}

static inline ipl_t
ke_spinlock_release(kspinlock_t *lock, ipl_t ipl)
{
	pthread_mutex_unlock(lock);
	SIM_ipl = ipl;
	return ipl;
}

static inline bool
ke_spinlock_held(kspinlock_t *lock)
{
	return true;
}

typedef enum kwaitstatus {
	/*! the wait condition was met */
	kKernWaitStatusOK,
	/*! the wait timed out */
	kKernWaitStatusTimedOut,
} kwaitstatus_t;

#define KMUTEX_INITIALISER PTHREAD_MUTEX_INITIALIZER
typedef pthread_mutex_t kmutex_t;

typedef struct {
	pthread_mutex_t mutex;
	pthread_cond_t	cond;
	bool		state;
} kevent_t;

static inline void
nanosecs_to_timespec(struct timespec /* out */ *ts, int64_t nanosecs)
{
	clock_gettime(CLOCK_REALTIME, ts);

	ts->tv_sec += nanosecs / 1000000000;
	ts->tv_nsec += nanosecs % 1000000000;

	if (ts->tv_nsec >= 1000000000) {
		ts->tv_sec += 1;
		ts->tv_nsec -= 1000000000;
	}
}

static inline void
ke_event_init(kevent_t *event, bool signalled)
{
	pthread_mutex_init(&event->mutex, NULL);
	pthread_cond_init(&event->cond, NULL);
	event->state = false;
}

static inline void
ke_event_signal(kevent_t *event)
{
	pthread_mutex_lock(&event->mutex);
	event->state = true;
	pthread_cond_broadcast(&event->cond);
	pthread_mutex_unlock(&event->mutex);
}

static inline void
ke_event_clear(kevent_t *event)
{
	pthread_mutex_lock(&event->mutex);
	event->state = false;
	pthread_mutex_unlock(&event->mutex);
}

static inline kwaitstatus_t
ke_event_wait(kevent_t *event, int64_t nanosecs)
{
	kwaitstatus_t r = kKernWaitStatusTimedOut;
	int	      ret;

	pthread_mutex_lock(&event->mutex);
	while (!event->state) {
		if (nanosecs == -1) {
			ret = pthread_cond_wait(&event->cond, &event->mutex);
			if (ret == 0)
				r = kKernWaitStatusOK;
		} else {
			struct timespec ts;
			nanosecs_to_timespec(&ts, nanosecs);
			ret = pthread_cond_timedwait(&event->cond,
			    &event->mutex, &ts);
			if (ret == 0)
				r = kKernWaitStatusOK;
			else if (ret == 1)
				break;
		}
	}

	pthread_mutex_unlock(&event->mutex);
	return r;
}

static inline int
ke_wait(kmutex_t *mutex, const char *reason, bool isuserwait, bool alertable,
    int64_t timeout)
{
	int r;
	if (timeout == -1) {
		pthread_mutex_lock(mutex);
		return kKernWaitStatusOK;
	} else if (timeout == 0) {
		r = pthread_mutex_trylock(mutex);
	} else {
		struct timespec ts;
		nanosecs_to_timespec(&ts, timeout);
		r = pthread_mutex_timedlock(mutex, &ts);
	}
	return r == 0 ? kKernWaitStatusOK : kKernWaitStatusTimedOut;
}

static inline void
ke_mutex_release(kmutex_t *mutex)
{
	pthread_mutex_unlock(mutex);
}

#define kmem_alloc(SIZE) malloc(SIZE)
#define kmem_free(PTR, SIZE) free(PTR)

#endif /* KRX_KDK_SOFT_COMPAT_H */
