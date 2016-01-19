#include <stdio.h>
#include <pthread.h>

void * thread( void * arg )
{
	int cnt = 0 ; 
	while (cnt < 5) {
		sleep(1);
		printf("hello, i am child thread named %d.\n",(unsigned)pthread_self());
		pthread_yield();
		cnt ++ ; 
	}
}

int main()
{
	int i ; 
	printf("hello!\n");
	pthread_t pid ; 
	for ( i = 0 ; i < 3 ; i ++ ) {
		pthread_create( &pid , 0 , thread , NULL ) ; 
		pthread_detach( pid ) ; 
	}
	return 0 ; 
}
