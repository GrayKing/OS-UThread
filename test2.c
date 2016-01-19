#include <stdio.h>
#include "uthread.h"

void * thread( void * arg )
{
	int cnt = 0 ; 
	while (cnt < 10000000) {
		//sleep(1);
		if ( cnt % 1000000 == 0 ) printf("hello, i am child thread named %d call for the %d times.\n",uthread_self(),cnt);
		//uthread_yield();
		cnt ++ ; 
		//if ( cnt == 3 ) uthread_exit();
	}
	return NULL ; 
}

int main()
{
	uthread_initial();
	int i ; 
	printf("hello!\n");
	tid_t tid ; 
	for ( i = 0 ; i < 3 ; i ++ ) {
		uthread_create( &tid , thread , NULL ) ; 
		if ( i != 2 ) uthread_detach( tid ) ; 
	}
	printf("hello!!!\n");
	uthread_join(tid,NULL);
	printf("hello!\n");
	return 0 ; 
}
