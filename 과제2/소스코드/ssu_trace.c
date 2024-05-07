#include "types.h"
#include "user.h"

int main(int argc, char**  argv){
	if(argc<3){
		printf(1,"argc must be 3 or greater\n");
		exit();
	}
	int mask=atoi(argv[1]);
	if(mask==0){
		printf(1,"argv[1] must be an Integer\n");
		exit();
	}
	char** argv_for_next;
	argv_for_next=(char**)malloc(sizeof(char*)*(argc-1));
	for(int i=0;i<argc-2;i++){
		argv_for_next[i]=argv[2+i];
	}
	argv_for_next[argc-2]=0;
	int pid=fork();
	if(pid<0){ //fork()가 0보다 작으면 실패임.
		printf(1,"init: fork failed\n");
		exit();
	}
	if(pid==0){ // 이 자식 프로세스에 대해서만 mask를 설정.
		// 즉, ssu_trace쉘 명령어로 실행시킬 명령어가 실행되는 과정에서의 
		// 시스템콜만 추적함.
		trace(mask);
		exec(argv[2],argv_for_next);
	}
	wait();
	exit();
}
