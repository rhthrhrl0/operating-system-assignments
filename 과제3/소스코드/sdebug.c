#include "types.h"
#include "stat.h"
#include "user.h"

#define PNUM 5 // 프로세스 개수
#define PRINT_CYCLE 10000000 // 프로세스의 정보를 출력하는 주기.
#define TOTAL_COUNTER 500000000 //프로세스가 종료할 때 counter값

void sdebug_func(void)
{
	int n,pid;
	
	printf(1,"start sdebug command\n");

	for(n=0;n<PNUM;n++) // 자식 프로세스 PNUM개 만큼 생성하기 위한 반복문. 부모 프로세스만 돌거임.
	{
		pid=fork();
		if(pid<0)
			break;
		if(pid==0)
		{ // 자식 프로세스 진입.
			int start_ticks=uptime();	// 만들어진 자식 프로세스의 시작 시간 측정.
			weightset(n+1);	// 만들어진 자식 프로세스 5개 중 몇번째로 만들어졌는지에 따라 weight를 재지정 받음.

			int end_ticks=0;	// 자식 프로세스의 출력 시간 측정을 위한 변수

			long long counter=0;	// TOTAL_COUNTER 만큼 반복문을 돌 변수.
			long long print_counter=PRINT_CYCLE;	// 출력 시점을 위한 변수.

			int first=1;	// 한번 출력 여부에 대한 변수.

			while(counter<=TOTAL_COUNTER)
			{
				print_counter--;
				counter++;

				if(print_counter==0)	// 이게 0이 되면, 출력해야 하는 주기임.
				{	
					if(first) // 출력한 적 있으면 넘어감.	
					{
						end_ticks=uptime();
						printf(1,"PID: %d, WEIGHT: %d, ", getpid(), n+1); // pid정보와 weight변수 출력.
						printf(1,"TIMES : %d ms\n", (end_ticks-start_ticks)*10);	// 첫 출력까지 걸린 시간 출력.
						first=0; // 출력한 적이 있음을 표시.
					}
					print_counter=PRINT_CYCLE; // 다시 출력 주기 측정을 위해 PRINT_CYCLE로 돌려줌.
				}
			}

		 	printf(1,"PID: %d terminated\n", getpid()); // 자식 프로세스가 종료되기 전에 출력함.
			exit();
		}
	}

	// 여기부터는 부모 프로세스만 접근하게 될 수 있는 코드임.
	if(n!=PNUM || pid<=0){ // 정상적으로 자식 5개 만들었는지, 부모프로세스가 맞는지.
		printf(1,"fork-error...\n");
		exit();
	}

	// 딱 자기 프로세스 개수만큼 기다림.
	for(;n>0;n--){
		if((pid=wait())<0){	// wait()로부터 음수 반환은 기다리는 자식 프로세스가 없음을 의미함.
			break;
		}
	}

	// 정상적으로 PNUM번 wait()했다면, 이제 기다리는 자식 없어야 하므로 wait()로부터 음수를 반환받으면 문제있는거임.
	if(wait()!=-1){
		printf(1,"error.... wait too many\n");
		exit();
	}

	printf(1,"end of sdebug command\n");
}

int main(void)
{
	sdebug_func();
	exit();
}
