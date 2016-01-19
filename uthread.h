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

#define UTHREAD_STATUS_RUNNABLE 	1 
#define UTHREAD_STATUS_NOTRUNNABLE  0 
#define UTHREAD_STATUS_FINISHED 	2
#define UTHREAD_STATUS_WAITING 		3

#define UTHREAD_PROPERTY_JOINNABLE  0 
#define UTHREAD_PROPERTY_DETACHED	1

typedef unsigned tid_t ;  

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
	
//Need Verify 
void uthread_initial();
int  uthread_create(tid_t* tid_p, void* (*func)(void*), void* arg ); 
void uthread_exit();
int  uthread_join( tid_t tid );
void uthread_yield();
void uthread_detach();
tid_t uthread_self(); 


//static functions for special purpose
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
}

static uthread_t* uthread_new() 
{
	uthread_t * tmp = (  uthread_t* ) malloc ( sizeof(uthread_t) ) ; 
	memset( tmp , 0 , sizeof( uthread_t ) ) ; 
	tmp->stack = ( char * ) malloc ( UTHREAD_STACK_SIZE ) ; 
	getcontext( &tmp->context ) ; 
	tmp->property = UTHREAD_PROPERTY_JOINNABLE ; 
	tmp->id = -1 ; // mark the uthread as invalid
	return tmp ; 
}	

void uthread_initial()
{
	if ( uthread_initialized == 1 ) return ;
	uthread_initialized = 1 ; 
	printf("check point 1.\n"); 	//debug jtc
	
	//set the time interrupt handler 
	struct sigaction sa_timer ; 
	sa_timer.sa_flags = 0 ; 
	sa_timer.sa_sigaction = uthread_timer_handler ; 

	sigaction( SIGVTALRM , &sa_timer , NULL );

	//initialize the pool , set all the slots to null  
	memset( uthread_pool , 0 , sizeof(uthread_pool) ) ;

	//initialize the main thread at slot 0.  
	uthread_pool[0] = uthread_new() ; 
	uthread_pool[0]->id = 0 ; 
	getcontext( &uthread_pool[0]->context ) ; 
	
	uthread_scheduler = uthread_new() ; 
	uthread_scheduler->id = 128 ; 
	uthread_scheduler->context.uc_stack.ss_sp = uthread_scheduler->stack ; 
	uthread_scheduler->context.uc_stack.ss_size = UTHREAD_STACK_SIZE ; 
	uthread_scheduler->context.uc_stack.ss_flags = 0 ; 
	uthread_scheduler->context.uc_link = NULL ; 
	//masked the signal while it is in the scheduler 
	sigaddset( & uthread_scheduler->context.uc_sigmask , SIGVTALRM ); 

	makecontext( &uthread_scheduler->context , (void (*)(void))uthread_sched , 0 ) ; 
	uthread_pool[0]->status = UTHREAD_STATUS_RUNNABLE ; 

	//change the behaviour that the scheduler is the super thread 
	//when main thread ends, it turns to scheduler 
	uthread_pool[0]->context.uc_link = &uthread_scheduler->context ; 	
	
	//set up the timer 
	struct itimerval timer_itval, old_timer_itval;
	timer_itval.it_value.tv_sec = 0;
	timer_itval.it_value.tv_usec = 50000;
	timer_itval.it_interval.tv_sec = 0;
	timer_itval.it_interval.tv_usec = 50000; 	
	setitimer(ITIMER_VIRTUAL, &timer_itval, &old_timer_itval);

	swapcontext( &uthread_pool[0]->context , &uthread_scheduler->context);	

	printf("check point 2.\n"); 	//debug jtc
}

static tid_t scheduler_Round_Robin()
{		
	tid_t i = current_tid ; 
	tid_t j = i + 1 ; 
	for ( ; j < UTHREAD_THREAD_MAX ; j ++ )
		if ( uthread_pool[j] && uthread_pool[j]->status == UTHREAD_STATUS_RUNNABLE ) 
			return j ;
	j = 0 ; 
	for ( ; j <= i ; j ++ )
		if ( uthread_pool[j] && uthread_pool[j]->status == UTHREAD_STATUS_RUNNABLE ) 
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
		printf("check point 3 in sched, choose %u as the next one.\n",u_next); 	//debug jtc
		swapcontext( &uthread_scheduler->context, &uthread_pool[u_next]->context);

		//TODO : check all the waiting threads and wake them up
		// 		 and release all the resources  
		if ( uthread_pool[u_next]->status == UTHREAD_STATUS_FINISHED ) {
			tid_t i ; 
			unsigned rear_cnt = 0 ; 
			for ( i = 0 ; i < UTHREAD_THREAD_MAX ; i ++ )
				if ( uthread_pool[i] && uthread_pool[i]->status == UTHREAD_STATUS_WAITING && uthread_pool[i]->waitfor == current_tid )
				{
					uthread_pool[i]->status = UTHREAD_STATUS_RUNNABLE ; 
					uthread_pool[i]->waitfor = 0 ; 
					rear_cnt ++ ; 
					break ; 
				}
			if ( uthread_pool[u_next]->property == UTHREAD_PROPERTY_DETACHED || rear_cnt ) uthread_pool_delete( u_next );
		}
	}
}

void uthread_wrapper( void* (*func)(void*), void* arg, tid_t tid) 
{
	uthread_pool[tid]->rtn_value = (* func)( arg ) ; 
	uthread_pool[tid]->status = UTHREAD_STATUS_FINISHED ; 
}

int uthread_create(tid_t* tid_p, void* (*func)(void*), void* arg )
{
	tid_t i = 0 ; 
	for ( i = 0 ; i < UTHREAD_THREAD_MAX ; i ++ ) 
		if ( !uthread_pool[i] ) break ; 
	if ( i == UTHREAD_THREAD_MAX ) return -1 ; 

	//get the slot of the thread pool.
	tid_t tid = i ; 
	*tid_p = tid ; 
	printf("check point 5 in create, choose %u as the slot.\n",tid); 	//debug jtc
	
	uthread_pool[tid] = uthread_new(); 
	uthread_pool[tid]->id = tid ; 
	uthread_pool[tid]->context.uc_stack.ss_sp = uthread_pool[i]->stack ; 
	uthread_pool[tid]->context.uc_stack.ss_size = UTHREAD_STACK_SIZE ; 
	uthread_pool[tid]->context.uc_stack.ss_flags = 0 ; 
	uthread_pool[tid]->context.uc_link = &uthread_scheduler->context ; 

	makecontext( &uthread_pool[i]->context , (void (*)(void))uthread_wrapper , 3 , func , arg , tid ) ; 
	uthread_pool[tid]->status = UTHREAD_STATUS_RUNNABLE ; 
	//swapcontext( &uthread_pool[uthread_self()]->context,&uthread_pool[tid]->context);
	//uthread_yield();	

	return 0 ; 
}

tid_t uthread_self()
{
	return current_tid ; 
}

void uthread_yield()
{
	swapcontext(&uthread_pool[uthread_self()]->context,&uthread_scheduler->context);
}

void uthread_exit()
{
	uthread_pool[uthread_self()]->status = UTHREAD_STATUS_FINISHED ; 
	uthread_yield();
}

void uthread_detach()
{
	uthread_pool[uthread_self()]->property = UTHREAD_PROPERTY_DETACHED ; 
}

int uthread_join( tid_t tid )
{
	if ( tid < 0 || tid >= UTHREAD_THREAD_MAX ) return -1 ; 
	if ( uthread_pool[tid] == NULL ) return -1 ;  
	if ( uthread_pool[tid]->property == UTHREAD_PROPERTY_DETACHED ) return -1 ;
	if ( uthread_pool[tid]->status == UTHREAD_STATUS_FINISHED ) {
		uthread_pool_delete( tid ) ; 
		return 0 ; 
	}
	printf("check point 4, thread %u watis for %u.\n",uthread_self(),tid); 	//debug jtc
	uthread_pool[uthread_self()]->status = UTHREAD_STATUS_WAITING ; 
	uthread_pool[uthread_self()]->waitfor = tid ; 
	uthread_yield();
	return 0 ; 
}

void uthread_timer_handler( int signum, siginfo_t* info, void* ucp)
{
	tid_t tid = uthread_self() ; 
	uthread_pool[tid]->context = *(struct ucontext*)ucp ; 
	setcontext( &uthread_scheduler->context ) ; 
}

#endif

