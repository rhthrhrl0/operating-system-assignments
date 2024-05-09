# operating-system-assignments
운영체제 수업 과목 과제 코드 및 보고서 정리

# 과제1. xv6 설치

과제 1번은 비교적 간단한 helloworld라는 쉘 프로그램을 만들고,
init.c 라는 xv6 초기 부팅 동작 코드 파일을 수정해서, os가 쉘을 띄우기 전에 로그인 과정을 추가함.

# 과제2. PCB 구조체 수정해서 trace 시스템 콜 만들기

과제 2번은 xv6 os의 PCB 블록인 proc 구조체에 int mask 필드를 추가해서, trace 시스템콜로 
이 mask 값을 조정할 수 있고, syscall.c에 있는 syscall 함수에서 실행한 시스템 콜 함수들의 수행완료 이후
이 프로세스가 trace 시스템 콜로 등록한 시스템 콜이였다면,그 시스템 콜의 이름과 반환값을 화면에 출력시키는 것입니다.

또한, 이 프로세스가 생성한 자식 프로세스 또한 부모 프로세스의 mask값을 물려받아야 하므로, proc.c의 fork함수를 
일부 수정했습니다.

이 외에도 xv6의 malloc 과 morecore 를 분석해서 xv6 운영체제의 메모리 페이지 할당 최소 단위가 
4096 바이트(즉,4KB)이라는 것을 알아냈습니다.

### 학습한 것
1. 시스템 콜을 직접 추가하기 위해, 시스템 콜의 호출 과정과 인터럽트 및 트랩에 대해 학습함.
   1. 트랩이 실행되는 과정에서 매크로를 통해 eax 레지스터의 값이 수정되는 코드를 확인함.
2. trace 시스템 콜을 만들기 위해, proc 구조체에 int mask 변수를 추가함.
   1. fork 함수에서 자식 프로세스에게 mask값을 물려받도록 하는 코드 추가
   2. syscall.c의 syscall 함수가 처리되는 과정에서 시스템 콜 실행 이후 해당 프로세스의 mask값을 확인해서 시스템 콜 이름과 반환값 출력하는 로직 추가
3. malloc 의 할당 과정 분석 및 새로운 메모리 페이지 할당시 4KB 단위로 할당하는 것을 확인함.
