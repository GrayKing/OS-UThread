/*
	ID: 1300012771
	Name: Tiancheng Jin
	Time: 00:04 1/20/2016 
	A naive user level thread library.
	Copyright ? What the hell ? 
*/
#ifndef USER_THREAD_LIBRARY
#define USER_THREAD_LIBRARY 

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/select.h>
#include <string.h>
#include <error.h>

#define UTHREAD_THREAD_MAX 128 
#define UTHREAD_STACK_SIZE 1024*128 

#define UTHREAD_STATUS_NOTRUNNABLE  	0 
#define UTHREAD_STATUS_RUNNABLE 	1 
#define UTHREAD_STATUS_FINISHED 	2
#define UTHREAD_STATUS_WAITING 		3

#define UTHREAD_PROPERTY_JOINNABLE  	0 
#define UTHREAD_PROPERTY_DETACHED	1

typedef unsigned tid_t ;  

/*  basic data structure for thread,  
    id is the thread's id, which can be ehanced in the future.( using the 32bit ) 
    Runs stores the time slices that this thread is take. 
    Stack is the allocated space for the stack ( size is confined as UTHREAD_STACK_SIZE ).
    "context" is the ucontext_t type for the thread's context.
    Status represents the current status of the thread , which can be divided into four kinds.
    Waitfor just for the "uthread_join".
    Property is only for the "uthread_detached".
    rtn_value used for the "uthread_join" to get the finished thread's return value.
    wait_value is used for the "uthread_join" to store the return value for the waitting thread.
*/

struct uthread 
{
	tid_t id;
	unsigned runs ; 
	char* stack ;
	ucontext_t context;
	unsigned status ; 
	tid_t waitfor ; 
	unsigned property ; 
	void* rtn_value ; 
	void* wait_value ; 
};

typedef struct uthread uthread_t;


static uthread_t* uthread_pool[UTHREAD_THREAD_MAX] ;  
static uthread_t* uthread_scheduler = NULL ; 
static int uthread_initialized = 0 ; 
static unsigned current_tid = 0 ; 
	
/*---------------------------------export functions interfaces---------------------------------*/ 

void uthread_initial();
/*  uthread_initial() need to explicit marked before use the other functions, it build up 
    the foundation for all the other functions. */

int  uthread_create(tid_t* tid_p, void* (*func)(void*), void* arg ); 
/*  uthread_create is liked the pthread_create, create a thread, func is the start function 
    , and tid_p to store the thread's id, arg is the standard start function's input(void*) */

void uthread_exit();
/*  explict exit the current thread, a little bit different from the others. */

int  uthread_join( tid_t tid , void ** retval);
/*  "uthread_join" force the callee thread block until the target thread finish it's job. */

void uthread_yield();
/*  "uthread_yield" release the control of the time slice and give it back to the scheduler. */
 
void uthread_detach();
/*  "uthread_detach" changed the thread's property to "UTHREAD_PROPERTY_DETACHED", which make the 
    scheduler to rear after it is finished. "uthread_join" for a detached thread is abandoned. */

tid_t uthread_self(); 
/*  "uthread_self" return the "current_id"( because in this library there is only on thread can 
    execute at the same time. so we use the current_id to store for this :P ). */ 

//static functions for special purpose. cannot be used outside the files.
static void uthread_pool_delete( tid_t tid );
static void uthread_sched();
static uthread_t* uthread_new();
static void uthread_sched();
static void uthread_timer_handler( int signum, siginfo_t* info, void* ucp);  

static void uthread_pool_delete( tid_t tid ) 
{
	if ( !uthread_pool[tid] ) return ;
	free( uthread_pool[tid]->stack ) ;  
	free( uthread_pool[tid] ); 
	uthread_pool[tid] = NULL ;
	// clean the slot and release the memeory.
}

static uthread_t* uthread_new() 
{
	uthread_t * tmp = (  uthread_t* ) malloc ( sizeof(uthread_t) ) ; 
	memset( tmp , 0 , sizeof( uthread_t ) ) ; 
	tmp->stack = ( char * ) malloc ( UTHREAD_STACK_SIZE ) ; 
	//prepare the thread's stack.
	getcontext( &tmp->context ) ; 
	tmp->property = UTHREAD_PROPERTY_JOINNABLE ; 
	tmp->status   = UTHREAD_STATUS_NOTRUNNABLE ; 
	tmp->id = -1 ; 
	// mark the uthread as invalid
	return tmp ; 
}	

void uthread_initial()
{
	if ( uthread_initialized == 1 ) return ;
	uthread_initialized = 1 ; 
	
	//set the time interrupt handler 
	struct sigaction sa_timer ; 
	sa_timer.sa_flags = 0 ; 
	sa_timer.sa_sigaction = uthread_timer_handler ; 

	sigaction( SIGALRM , &sa_timer , NULL );

	//initialize the pool , set all the slots to null  
	memset( uthread_pool , 0 , sizeof(uthread_pool) ) ;

	//initialize the main thread at slot 0.  
	uthread_pool[0] = uthread_new() ; 
	uthread_pool[0]->id = 0 ; 
	getcontext( &uthread_pool[0]->context ) ; 
	
	//prepare the scheduler thread's stack and id etc. 
	uthread_scheduler = uthread_new() ; 
	uthread_scheduler->id = 128 ; 
	uthread_scheduler->context.uc_stack.ss_sp = uthread_scheduler->stack ; 
	uthread_scheduler->context.uc_stack.ss_size = UTHREAD_STACK_SIZE ; 
	uthread_scheduler->context.uc_stack.ss_flags = 0 ; 
	uthread_scheduler->context.uc_link = NULL ; 
	//masked the signal while it is in the scheduler 
	sigaddset( & uthread_scheduler->context.uc_sigmask , SIGALRM ); 

	makecontext( &uthread_scheduler->context , (void (*)(void))uthread_sched , 0 ) ; 
	uthread_pool[0]->status = UTHREAD_STATUS_RUNNABLE ; 

	//change the behaviour that the scheduler is the super thread 
	//when main thread ends, it turns to scheduler
 	//but it failed ( because once the main thread finished ,
        //system will halt all the other peer threads ) . 
	uthread_pool[0]->context.uc_link = &uthread_scheduler->context ; 	
	
	//set up the timer, using the Interval Timer to make it accurate. 
	//10000us(100times per user second) is the interval.
	struct itimerval timer_itval, old_timer_itval;
	timer_itval.it_value.tv_sec = 0;
	timer_itval.it_value.tv_usec = 5000;
	timer_itval.it_interval.tv_sec = 0;
	timer_itval.it_interval.tv_usec = 5000; 	
	setitimer(ITIMER_REAL, &timer_itval, &old_timer_itval);

	uthread_yield();	
	//swapcontext( &uthread_pool[0]->context , &uthread_scheduler->context);	
}


/*  We simply implemented the Round Robin method for the scheduler, indeed we can use the 
    common function pointers to meet different users's needs. */
static tid_t scheduler_Round_Robin()
{		
	tid_t i = current_tid ; 
	tid_t j = i + 1 ; 
	for ( ; j < UTHREAD_THREAD_MAX ; j ++ )
		if ( ( uthread_pool[j] ) && ( uthread_pool[j]->status == UTHREAD_STATUS_RUNNABLE ) ) 
			return j ;
	j = 0 ; 
	for ( ; j <= i ; j ++ )
		if ( ( uthread_pool[j] ) && ( uthread_pool[j]->status == UTHREAD_STATUS_RUNNABLE ) ) 
			return j ;
	return -1 ; 
}

static void uthread_sched()
{
	tid_t (*scheduler)(void) = scheduler_Round_Robin ; 
	while (1) {
		tid_t u_next = (*scheduler)(); 
		if ( u_next == -1 ) return ; 
		uthread_pool[u_next]->runs ++ ;
		current_tid = u_next ; 
		swapcontext( &uthread_scheduler->context, &uthread_pool[u_next]->context);
	
		//check all the waiting threads and wake them up and release all the resources.
		//if the thread is detached or have been reared.  
		if ( uthread_pool[u_next]->status == UTHREAD_STATUS_FINISHED ) {
			tid_t i ;
			unsigned rear_cnt = 0 ; 
			for ( i = 0 ; i < UTHREAD_THREAD_MAX ; i ++ )
				if ( uthread_pool[i] && uthread_pool[i]->status == UTHREAD_STATUS_WAITING && uthread_pool[i]->waitfor == current_tid )
				{
					uthread_pool[i]->status = UTHREAD_STATUS_RUNNABLE ; 
					uthread_pool[i]->waitfor = 0 ; 
					uthread_pool[i]->wait_value = uthread_pool[u_next]->rtn_value ; 
					rear_cnt ++ ; 
					//break ; 
				}
			if ( uthread_pool[u_next]->property == UTHREAD_PROPERTY_DETACHED || rear_cnt ) uthread_pool_delete( u_next );
		}
		 
	}
}


/*  The wrapper just wrap the function call to do some extra work,
    one is store the rtn_value, the other one is to mark the FINISHED. */

void uthread_wrapper( void* (*func)(void*), void* arg, tid_t tid) 
{
	uthread_pool[tid]->rtn_value = (* func)( arg ) ; 
	uthread_pool[tid]->status = UTHREAD_STATUS_FINISHED ; 
	uthread_yield();
}

int uthread_create(tid_t* tid_p, void* (*func)(void*), void* arg )
{
	tid_t i = 0 ; 
	for ( i = 0 ; i < UTHREAD_THREAD_MAX ; i ++ ) 
		if ( !uthread_pool[i] ) break ; 
	if ( i == UTHREAD_THREAD_MAX ) return -1 ; 

	//get an empty slot of the thread pool.
	tid_t tid = i ; 
	*tid_p = tid ; 
	
	uthread_pool[tid] = uthread_new(); 
	uthread_pool[tid]->id = tid ; 
	uthread_pool[tid]->context.uc_stack.ss_sp = uthread_pool[i]->stack ; 
	uthread_pool[tid]->context.uc_stack.ss_size = UTHREAD_STACK_SIZE ; 
	uthread_pool[tid]->context.uc_stack.ss_flags = 0 ; 
	uthread_pool[tid]->context.uc_link = &uthread_scheduler->context ; 

	makecontext( &uthread_pool[i]->context , (void (*)(void))uthread_wrapper , 3 , func , arg , tid ) ; 
	uthread_pool[tid]->status = UTHREAD_STATUS_RUNNABLE ; 

	return 0 ; 
}

tid_t uthread_self()
{
	return current_tid ; 
}

/* This is a very important part. Actually we should handler the SIGALRM signal and the 
   behaviour of the swapcontext, because swapcontext is not an atomic operation, so it can be 
   interrupt by the signal handler, which would lead to a god damn disaster.
   So we should mask it, until we finally reached the scheduler and back.
   The whole program is designed only use "uthread_yield" to swap, so it's safe for anywhere
   else. ( the scheduler is masked the SIGALRM already. ) */
void uthread_yield()
{
	sigset_t newmask,oldmask;
	sigemptyset(&newmask);
	sigaddset(&newmask,SIGALRM);
	sigprocmask(SIG_BLOCK,&newmask,&oldmask);
	swapcontext(&uthread_pool[uthread_self()]->context,&uthread_scheduler->context);
	sigprocmask(SIG_SETMASK,&oldmask,NULL);
}

void uthread_exit()
{
	uthread_pool[uthread_self()]->status = UTHREAD_STATUS_FINISHED ; 
	uthread_yield();
}

/* just change the property. */ 
void uthread_detach()
{
	uthread_pool[uthread_self()]->property = UTHREAD_PROPERTY_DETACHED ; 
}


/*  when retval is not a null pointer, we should transfer the return value 
    to this. there are also some bad behaviour we should detect and warning.*/
int uthread_join( tid_t tid , void** retval)
{
	if ( tid < 0 || tid >= UTHREAD_THREAD_MAX ) return -1 ; 
	if ( uthread_pool[tid] == NULL ) return -1 ;  
	if ( uthread_pool[tid]->status == UTHREAD_STATUS_FINISHED ) {
		uthread_pool_delete( tid ) ;
		if ( retval ) *retval = uthread_pool[tid]->rtn_value; 
		return 0 ; 
	}
	uthread_pool[uthread_self()]->status = UTHREAD_STATUS_WAITING ; 
	uthread_pool[uthread_self()]->waitfor = tid ; 
	uthread_yield();
	if ( retval ) *retval = uthread_pool[uthread_self()]->wait_value ; 
	return 0 ; 
}

static int timer_cnt = 0 ; 

void uthread_timer_handler( int signum, siginfo_t* info, void* ucp)
{
	if ( signum != SIGALRM ) return ;
	int tmp = timer_cnt ; 
	timer_cnt ++ ;  
	tid_t tid = uthread_self() ; 
	uthread_yield();
}

#endif

