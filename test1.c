#include <stdio.h>
#include "uthread.h"

void * thread( void * arg )
{
	int cnt = 0 ; 
	while (cnt < 5) {
		sleep(1);
		printf("hello, i am child thread named %d.\n",uthread_self());
		uthread_yield();
		cnt ++ ; 
	}
}

int main()
{
	uthread_initial();
	int i ; 
	printf("hello!\n");
	tid_t tid ; 
	for ( i = 0 ; i < 3 ; i ++ ) {
		uthread_create( &tid , thread , NULL ) ; 
		uthread_detach( tid ) ; 
	}
	return 0 ; 
}
