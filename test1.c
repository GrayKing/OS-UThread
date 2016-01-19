#include "uthread.h"

void * thread( void * arg )
{
	while (1) {
		sleep(1);
		printf("hello\n");
		uthread_yield();
	}
}

int main()
{
	int i ; 
	for ( i = 0 ; i < 3 ; i ++ ) {
		tid_t tid ; 
		uthread_create( &tid , thread , NULL ) ; 
	}
	return 0 ; 
}