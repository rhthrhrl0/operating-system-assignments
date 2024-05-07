#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<string.h>
#include<ctype.h>
#include<time.h>

#define BUFMAX 1024 //한 줄 입력 크기
#define PAGESTREAM 500 //페이지 스트림 크기
int split(char* string, char* seperator, char* argv[]); //프롬프트입력을 잘라줌.
void initPageSimulator(int* arr);
void optimal(int pageCount,int dataMode);
void fifo(int pageCount,int dataMode);
void lifo(int pageCount,int dataMode);
void lru(int pageCount,int dataMode);
void lfu(int pageCount,int dataMode);
void sc(int pageCount,int dataMode);
void esc(int pageCount,int dataMode);
void randStream(int* stream);
void initZero(int* arr,int size);
void printResult(int algorithm, int* pageTable,int pageCount,int hits,int pageFault);
void numbersRandCreate();
void numbersFileRead(int* pageStream,int* useBitStream, int* modifyBitStream);

time_t seed;
char* algorithmName[8]={"","Optimal","FIFO","LIFO","LRU","LFU","SC","ESC"};
FILE* fp=NULL;
FILE* resultFp=NULL;
const char* sampleFile="sample_file_20182580.txt";
const char* resultFile="result_20182580.txt";
int pageRefStringCount=0;
int optimalPf=0;

// A Queue Node (Queue is implemented using Doubly Linked List)
typedef struct QNode {
	struct QNode *prev, *next; // 각 노드 별로 자신의 앞 뒤를 가리킬 수 있음.
	unsigned pageNumber; // the page number stored in this QNode
} QNode;
  
// A Queue (A FIFO collection of Queue Nodes)
typedef struct Queue {
	unsigned count; // 현재 소유한 프레임 개수
	unsigned numberOfFrames; // 이 큐가 가질 수 있는 최대 프레임 개수
	QNode *front, *rear; // 큐의 양 끝을 가리킴. 이거 덕분에 fifo 같은 것도 가능해짐
} Queue;

// A hash (Collection of pointers to Queue Nodes)
typedef struct Hash {
	int capacity; // how many pages can be there
	QNode** array; // an array of queue nodes
} Hash;

// A utility function to create a new Queue Node. The queue Node
// will store the given 'pageNumber'
QNode* newQNode(unsigned pageNumber)
{
	// Allocate memory and assign 'pageNumber'
	QNode* temp = (QNode*)malloc(sizeof(QNode));
	temp->pageNumber = pageNumber;
	// Initialize prev and next as NULL
	temp->prev = temp->next = NULL;
	return temp;
}

// A utility function to create an empty Hash of given capacity
Hash* createHash(int capacity)
{
	// Allocate memory for hash
	Hash* hash = (Hash*)malloc(sizeof(Hash));
	hash->capacity = capacity;
	// Create an array of pointers for referring queue nodes
	hash->array = (QNode**)malloc(hash->capacity * sizeof(QNode*));
	// Initialize all hash entries as empty
	int i;
	for (i = 0; i < hash->capacity; ++i)
			hash->array[i] = NULL;
							  
	return hash;
}

Queue* createQueue(int numberOfFrames)
{
	Queue* queue = (Queue*)malloc(sizeof(Queue));
	// The queue is empty
	queue->count = 0;
	queue->front = queue->rear = NULL;
	// Number of frames that can be stored in memory
	queue->numberOfFrames = numberOfFrames;
	return queue;
}

int AreAllFramesFull(Queue* queue)
{
	return queue->count == queue->numberOfFrames;
}
  
int isQueueEmpty(Queue* queue)
{
	return queue->rear == NULL;
}
  
void LRU_deQueue(Queue* queue) // 큐의 rear를 제거해야 함.
{
	if (isQueueEmpty(queue)) // 큐가 비었다면 종료
			return;
			  
	// 큐에 노드가 한개였던 경우
	// 만약 큐의 front와 rear가 같다면, front를 일단 NULL가리키도록 수정.
	if (queue->front == queue->rear)
			queue->front = NULL;
				  
	// 큐의 rear를 삭제하기 위해 연결을 끊는 모습.
	QNode* temp = queue->rear; // free해주기 위해 일단 기억하기.
	queue->rear = queue->rear->prev; // 큐의 rear 수정. 만약 큐에 한개가 있었다면 큐의 rear는 널을 받게 될거임.
						  
	if (queue->rear) // 바뀐 큐의 rear가 널이 아니라면, 바뀐 큐의 next를 널로 설정.
			queue->rear->next = NULL;
							  
	free(temp);	// 해제.
	
	queue->count--; //큐 개수 줄임.
}

void LRU_Enqueue(Queue* queue, Hash* hash, unsigned pageNumber)
{	
	if (AreAllFramesFull(queue)) { // 큐가 꽉차서 페이지 교체를 해야한다면
			hash->array[queue->rear->pageNumber] = NULL; // 해쉬에서 가장 오래된 참조 노드의 큐의 rear를 제거.
			LRU_deQueue(queue); // 큐에서 rear를 제거시킴.
	}
			  
	QNode* temp = newQNode(pageNumber); // 참조 페이지 넘버에 대한 큐 노드를 생성.
	temp->next = queue->front; // front로 놓기 위해 이어주기.
					  
	// 만약 새로 만든 큐노드가 큐의 유일한 노드라면
	if (isQueueEmpty(queue))
			queue->rear = queue->front = temp; // 큐의 rear와 front모두 새 노드를 가리키도록.
	else // 그게 아니라 다른 노드들도 있다면 새로 만든 큐 노드를 front로 이어주는 과정.
	{
		queue->front->prev = temp; 
		queue->front = temp;
	}
	hash->array[pageNumber] = temp; // 해쉬에 새로 만든 큐노드를 추가.
	queue->count++; // 큐 개수 증가.
}

void deleteAll(Queue* q, Hash* hash){
	for(QNode* qn=q->front;qn;){
		QNode* tmp=qn;
		qn=qn->next;
		free(tmp);
	}
	free(q);

	free(hash->array);
	free(hash);
}


int main(int argc, char* argv[]){
	seed=time(NULL);
	char input[BUFMAX];
	int selectCount;
	char* argv_s[3];
	int pageSimulator[9] = {-1,1,-1,-1,-1,-1,-1,-1,-1};
	int pageFrameCount;
	int dataInputMode;
	 
	if (argc>2){
		fprintf(stderr,"ERROR: Arguments error.. maximum two\n");
		exit(1);
	}
	else if(argc==2){
		if((fp=fopen(argv[1],"r"))==NULL){
			fprintf(stderr,"fopen error for %s\n",argv[1]);
			exit(1);
		}
		int fileCount=0;
		// PAGESTREAM보다 작으면 에러처리.
		char buf[BUFMAX];
		while(fgets(buf,BUFMAX,fp)!=NULL){
			fileCount++;
		}

		if (fileCount>=PAGESTREAM){
			pageRefStringCount=fileCount;
		} else {
			fprintf(stderr,"More than 500 page strings must exist in the file\n");
			exit(1);
		}
		fseek(fp,0,SEEK_SET);
	}

	printf("A. Page Replacement 알고리즘 시뮬레이터를 선택하시오 (최대 3개)\n");
	printf("(1) Optimal (2) FIFO (3) LIFO (4) LRU (5) LFU (6) SC (7) ESC (8) All\n");
	while(1){
		initPageSimulator(pageSimulator);
		printf(">> ");
		fgets(input, sizeof(input), stdin);
		input[strlen(input) - 1] = '\0';
		selectCount = split(input, " ", argv_s);
		if(selectCount>=1 && selectCount<=3){
			int i;
			for(i=0;i<selectCount;i++){
				int hasNoDigit=0;
				for (int j = 0; argv_s[i][j] != '\0'; j++){
					if (isdigit(argv_s[i][j])==0){
						printf("숫자와 공백만 입력해주세요.\n");
						hasNoDigit=1;
						break;
					}
				}
				if(hasNoDigit)
						break;

				int tmp=atoi(argv_s[i]);
				if(tmp==0 || tmp<1 || tmp>8){
						printf("1부터 8 이내의 숫자만 입력해주세요.\n");
						break;
				}
				pageSimulator[tmp]=1;
			}
			if(i==selectCount)
				break;
		} else {
			printf("1개 이상 3개 이하로 다시 골라주세요.\n");
		}
	}
	
	printf("B. 페이지 프레임의 개수를 입력하시오.(3~10)\n");
	while(1){
		printf(">> ");
		if(scanf("%d",&pageFrameCount)<0 || pageFrameCount<3 || pageFrameCount>10){
			printf("3~10 이내의 숫자를 입력해주세요.\n");
			continue;
		}
		break;
	}

	printf("C. 데이터 입력 방식을 선택하시오. (1,2)\n");
	printf("(1) 랜덤하게 생성\n");
	printf("(2) 사용자 생성 파일 오픈\n");
	while(1){
		printf(">> ");
		if(scanf("%d",&dataInputMode)<0 || dataInputMode<1 || dataInputMode>2){
			printf("1 혹은 2의 숫자를 입력해주세요.\n");
			continue;
		}
		if(dataInputMode==1){
			pageRefStringCount=PAGESTREAM;
		} else if(dataInputMode==2 && fp==NULL){
			pageRefStringCount=PAGESTREAM;
			if((fp=fopen(sampleFile,"w+"))==NULL){
				fprintf(stderr,"fopen error for %s\n",sampleFile);
				exit(1);
			}
			numbersRandCreate();
		}
		break;
	}

	if(pageSimulator[8]==1){
		for(int i=1; i<=8;i++)
			pageSimulator[i]=1;
	}


	// 파일에 결과를 작성하기 위해 파일 오픈.
	if((resultFp=fopen(resultFile,"w+"))==NULL){
		fprintf(stderr,"fopen error for %s\n",resultFile);
		exit(1);
	}

	printf("\n");
	for(int i=1; i<=7;i++){
		if(pageSimulator[i]==1){
			switch(i){
				case 1:{
					optimal(pageFrameCount,dataInputMode);
					break;
				}	
				case 2:{
					fifo(pageFrameCount,dataInputMode);
					break;
				}
				case 3:{
					lifo(pageFrameCount,dataInputMode);
					break;
				}
				case 4:{
					lru(pageFrameCount,dataInputMode);
					break;
				}
				case 5:{
					lfu(pageFrameCount,dataInputMode);
					break;
				}
				case 6:{
					sc(pageFrameCount,dataInputMode);
					break;
				}
				case 7:{
					esc(pageFrameCount,dataInputMode);
					break;
				}
			}
		}
	}

	
	if(fp!=NULL){
		fclose(fp);
	}
	if(resultFp!=NULL){
		fclose(resultFp);
	}
	exit(0);
}

void arrayAlgorithmPrint(int algorithm,int index,int isHit,int isReplace,int refStr,int* curFrames,int pageCount){
	if (index==0){
		printf("[%s] 알고리즘\n",algorithmName[algorithm]);	
		fprintf(resultFp,"[%s] 알고리즘\n",algorithmName[algorithm]);	
	}
	printf("[%3d]번째 참조스트링: %2d )\t",index,refStr);
	fprintf(resultFp,"[%3d]번째 참조스트링: %2d )\t",index,refStr);
	for(int i=0; i<pageCount;i++){
		if (curFrames[i]<=0){
			printf("__\t");
			fprintf(resultFp,"__\t");
			continue;
		}
		printf("%2d\t",curFrames[i]);
		fprintf(resultFp,"%2d\t",curFrames[i]);
	}

	printf("=> ");
	fprintf(resultFp,"=> ");
	if(isHit){
		printf("Hit!!\n");
		fprintf(resultFp,"Hit!!\n");
	}
	else if(isReplace){
		printf("pageFault && pageReplace\n");
		fprintf(resultFp,"pageFault && pageReplace\n");
	}
	else{
		printf("pageFault\n");
		fprintf(resultFp,"pageFault\n");
	}
	
}

int search(int key, int frame_items[], int frame_occupied) 
{
	for (int i = 0; i < frame_occupied; i++)
			if (frame_items[i] == key)
					return 1;
	return 0;
}
//			  참조 스트링 ,현재프레임들,    참조스트링 총개수,찾을시작위치,현재프레임개수.
int predict(int ref_str[], int frame_items[], int refStrLen, int index, int frame_occupied){
	int result = -1, farthest = index;
	for (int i = 0; i < frame_occupied; i++) { // 현재 프레임들에 대해 하나씩 탐색.
			int j;
			for (j = index; j < refStrLen; j++) { 
					if (frame_items[i] == ref_str[j]) //지금 페이지 프레임에 있는 놈들에 대해서 가장 빨리 언제 나올지 위치 찾는 중 
					{ 
							if (j > farthest) {
									farthest = j;
									result = i;
							}
							break;
					}
			}
										  
			if (j == refStrLen) //이거보다 가장 뒤에 나올 수는  없음. 그만해도 됨.
					return i;
	}
				  
	return (result == -1) ? 0 : result;  //만약 모든 페이지들이 앞으로 나올 일이 없는 페이지들이라면, 맨 앞 놈 제거.
}

void optimal(int pageCount,int dataMode){	
	int* pageTable=(int*)malloc(sizeof(int)*pageCount);
	initZero(pageTable,pageCount);
	int* pageStream = (int*)malloc(sizeof(int)*pageRefStringCount);
	int pageFault=0;
	int distinctCount=0;

	if(dataMode==1){
		randStream(pageStream);
	} else {
		numbersFileRead(pageStream,NULL,NULL);
	}
	
	int frame_occupied = 0; //안에 존재하는 프레임 개수를 의미.
	int hits = 0;
	for (int i = 0; i < pageRefStringCount; i++) {
			if (search(pageStream[i], pageTable, frame_occupied)) { //존재하는지 확인. 
					hits++;
					// hit 출력
					arrayAlgorithmPrint(1,i,1,0,pageStream[i],pageTable,pageCount);
					continue;
			}
							  
			// 없는 경우
			pageFault++;
			if (frame_occupied < pageCount){ // 아직 페이지 테이블을 다 채우지도 않은 경우.
					pageTable[frame_occupied] = pageStream[i]; //그냥 다음 자리에 넣으면 됨.
					frame_occupied++;
					// 출력. 
					 arrayAlgorithmPrint(1,i,0,0,pageStream[i],pageTable,pageCount);
			}
			else {
					int pos = predict(pageStream, pageTable, pageRefStringCount, i + 1, frame_occupied); // 가장 늦게 쓰일 놈의 위치 반환.
					pageTable[pos] = pageStream[i]; // 페이지 교체.
					//출력. 교체됏다고 알리기.
					 arrayAlgorithmPrint(1,i,0,1,pageStream[i],pageTable,pageCount);
			}
	}
	
	printResult(1,pageTable,pageCount,hits,pageFault);
	optimalPf=pageFault; // 최적 알고리즘의 pageFault 횟수를 기억. 비교하기 위해.
	free(pageTable);
	free(pageStream);
}

void fifo(int pageCount,int dataMode){
	int* pageStream = (int*)malloc(sizeof(int)*pageRefStringCount);

	if(dataMode==1){
		randStream(pageStream);
	} else {
		numbersFileRead(pageStream,NULL,NULL);
	}

	int pageFaults = 0;
	int hits=0;
	int m, n, s;
	int* pageTable=(int*)malloc(sizeof(int)*pageCount); // pageTable 역할.
	for(m = 0; m < pageCount; m++){
			pageTable[m] = 0; // 초기화.
	}

	for(m = 0; m < pageRefStringCount; m++){ // 참조 스트링 하나씩 순회.
		s = 0; // hit 여부
		for(n = 0; n < pageCount; n++){ // 현재 페이지 테이블에 해당 참조 스트링이 있는지 검사.
			if(pageStream[m] == pageTable[n]){
		        s++; 			// hit
				hits++;
				arrayAlgorithmPrint(2,m,1,0,pageStream[m],pageTable,pageCount); // 출력.
		        pageFaults--;
			 }
		}
		pageFaults++; // 만약 위에서 hit안됐다면 상쇄가 안돼서 그대로 증가하게 됨.
																				        
		if((pageFaults <= pageCount) && (s == 0)){ // 페이지 폴트이지만 페이지 교체는 아님.
				pageTable[pageFaults-1] = pageStream[m];
				arrayAlgorithmPrint(2,m,0,0,pageStream[m],pageTable,pageCount);
		}
		else if(s == 0){ // 페이지 폴트이고 페이지 교체도 발생함.
				pageTable[(pageFaults - 1) % pageCount] = pageStream[m]; // 페이지 폴트횟수-1에 페이지 크기만큼 모듈로 연산을 수행하면 
				// fifo 정책에 맞게 순환큐 방식으로 계속 순회하는 형태로 교체하게 된다.
				arrayAlgorithmPrint(2,m,0,1,pageStream[m],pageTable,pageCount);
		}																						      
	}

	printResult(2,pageTable,pageCount,hits,pageFaults);
	free(pageStream);
	free(pageTable);
}

void lifo(int pageCount,int dataMode){
	int* pageStream = (int*)malloc(sizeof(int)*pageRefStringCount);

	if(dataMode==1){
		randStream(pageStream);
	} else {
		numbersFileRead(pageStream,NULL,NULL);
	}
	
	int pageFaults = 0;
	int hits=0;
	int frames = pageCount;
	int m, n, s, pages;
	pages = pageRefStringCount;
	int* temp=(int*)malloc(sizeof(int)*frames); // pageTable 역할.
	for(m = 0; m < frames; m++){
			temp[m] = -1;
	}

	for(m = 0; m < pages; m++){
		s = 0; // hit여부.
		for(n = 0; n < frames; n++){ // 현재 페이지 테이블에 해당 참조스트링 있는지 검사.
			if(pageStream[m] == temp[n]){
		        s++;	// hit
				hits++;
				arrayAlgorithmPrint(3,m,1,0,pageStream[m],temp,pageCount); // 출력.
		        pageFaults--;
			}
		}
		pageFaults++;
																				        
		if((pageFaults <= frames) && (s == 0)){ // 페이지 fault지만 페이지 교체는 아님.
				temp[pageFaults-1] = pageStream[m];
				arrayAlgorithmPrint(3,m,0,0,pageStream[m],temp,pageCount); // 출력.
		}
		else if(s == 0){ // 페이지 fault이고 페이지 교체도 발생.
				temp[pageCount-1] = pageStream[m]; // lifo방식은 마지막으로 들어온 것을 내보냄.
				arrayAlgorithmPrint(3,m,0,1,pageStream[m],temp,pageCount); // 출력.
		}																								      
	}

	printResult(3,temp,pageCount,hits,pageFaults);
	free(pageStream);
	free(temp);
}


void linkedListAlgorithmPrint(int algorithm,int index,int isHit,int refStr,Queue* curFrames,int pageCount){
	if (index==0){
		printf("[%s] 알고리즘\n",algorithmName[algorithm]);	
		fprintf(resultFp,"[%s] 알고리즘\n",algorithmName[algorithm]);	
	}
	printf("[%3d]번째 참조스트링: %2d )\t",index,refStr);
	fprintf(resultFp,"[%3d]번째 참조스트링: %2d )\t",index,refStr);

	int count=0;
	QNode* qn=curFrames->front;
	for(int i=0; i<pageCount; i++){
		if (qn==NULL){
			printf("__\t");
			fprintf(resultFp,"__\t");
			count++;
			continue;
		}
		printf("%2d\t",qn->pageNumber);
		fprintf(resultFp,"%2d\t",qn->pageNumber);
		qn=qn->next;
	}

	printf("=> ");
	fprintf(resultFp,"=> ");
	if(isHit){
		printf("Hit!!\n");
		fprintf(resultFp,"Hit!!\n");
	}
	else if(count==0){ // 페이지 폴트에 교체인 경우.
		printf("pageFault && pageReplace\n");
		fprintf(resultFp,"pageFault && pageReplace\n");
	}
	else{
		printf("pageFault\n");
		fprintf(resultFp,"pageFault\n");
	}
}

// 가장 오랫동안 사용되지 않은 페이지를 교체
void lru(int pageCount,int dataMode){ 
	Queue* queue = createQueue(pageCount); // 페이지 개수만큼 노드를 가질 수 있는 큐 생성. 
		// 이 큐는 가장 최근에 참조한 노드부터 차례대로 front->rear 방향으로 놓이도록 유지한다.
	Hash* hash = createHash(31); // 1~30번의 인덱스를 사용하기 위해.
	
	int* pageStream = (int*)malloc(sizeof(int)*pageRefStringCount);
	int pageFault=0;
	int hits=0;
	if(dataMode==1){
		randStream(pageStream);
	} else {
		numbersFileRead(pageStream,NULL,NULL);
	}
	
	for(int i=0;i<pageRefStringCount;i++){
		int pageNumber=pageStream[i];
		QNode* reqPage = hash->array[pageNumber]; // 해당 참조 스트링이 큐에 있는지 해쉬로 확인.
		if (reqPage == NULL){ // 없는 경우. 일단 페이지 폴트임.
				LRU_Enqueue(queue, hash, pageNumber);
				pageFault++;
				// 출력
				linkedListAlgorithmPrint(4,i,0,pageStream[i],queue,pageCount);
				continue;
		}
			  
		// 페이지 hit인 경우.
		else if (reqPage != queue->front) { // 페이지 히트인데 front에 위치하지 않는 경우. 
				// 링크드 리스트 순서를 바꿔서 최근 참조 순서를 유지시켜야 함.

				// 해당 참조 페이지를 링크드리스트에서 잠깐 제외시키고 이어줌.
				reqPage->prev->next = reqPage->next;  // 참조페이지의 앞 노드의 next를 참조페이지노드의 다음 노드로 연결.
				if (reqPage->next) // 만약 참조 페이지에게 뒷 노드가 있는 경우.
						reqPage->next->prev = reqPage->prev; // 참조페이지의 뒷 노드의 prev를 참조페이지노드의 앞 노드로 연결.
				
				// 만약 참조페이지 노드가 기존에 큐의 rear였다면 queue의 rear 수정.
				if (reqPage == queue->rear) {
						queue->rear = reqPage->prev;	
						queue->rear->next = NULL;
				}
												  
				
				// 참조 페이지의 노드를 현재 큐의 front 앞에 연결해줌.
				reqPage->next = queue->front;
				reqPage->prev = NULL;
																  
				// 기존 큐의 front노드의 prev으로 참조 페이지 노드를 놔줌.
				reqPage->next->prev = reqPage;
																		  
				// 큐의 front를 참조페이지 노드로 변경.
				queue->front = reqPage;
		} 
		
		hits++;
		// 출력
		linkedListAlgorithmPrint(4,i,1,pageStream[i],queue,pageCount);
	}
	
	printf("%s 결과: ",algorithmName[4]);
	fprintf(resultFp,"%s 결과: ",algorithmName[4]);
	for(QNode* tmp=queue->front;tmp;tmp=tmp->next){
		printf("%d",tmp->pageNumber);
		fprintf(resultFp,"%d",tmp->pageNumber);
		if(tmp->next){
			printf(", ");
			fprintf(resultFp,", ");
		}
	}
	printf("\n페이지 hit횟수: %d, 페이지 fault 횟수: %d, Optimal알고리즘의 페이지 fault 횟수:%d\n\n", hits,pageFault,optimalPf);
	fprintf(resultFp,"\n페이지 hit횟수: %d, 페이지 fault 횟수: %d, Optimal알고리즘의 페이지 fault 횟수:%d\n\n", hits,pageFault,optimalPf);
	deleteAll(queue,hash);
	free(pageStream);
}

void lfuAndScPrint(int algorithm,int index,int isHit,int isReplace,int refStr,int* curFrames,int* etcTable,int pageCount,int pointer){
	if (index==0){
		printf("[%s] 알고리즘\n",algorithmName[algorithm]);	
		fprintf(resultFp,"[%s] 알고리즘\n",algorithmName[algorithm]);	
	}
	printf("[%3d]번째 참조스트링: %2d )\t",index,refStr);
	fprintf(resultFp,"[%3d]번째 참조스트링: %2d )\t",index,refStr);
	for(int i=0; i<pageCount;i++){
		if (curFrames[i]<=0){
			if(algorithm==5){
				printf("__(c:__)\t");
				fprintf(resultFp,"__(c:__)\t");
			} else if(algorithm==6 && pointer==i){
				printf("__[r:__]\t");
				fprintf(resultFp,"__[r:__]\t");
			} else{
				printf("__(r:__)\t");
				fprintf(resultFp,"__(r:__)\t");			
			}
			continue;
		}

		if(algorithm==5){
			printf("%2d(c:%2d)\t",curFrames[i],etcTable[i]);
			fprintf(resultFp,"%2d(c:%2d)\t",curFrames[i],etcTable[i]);
		} else if(algorithm==6 && pointer==i){
			printf("%2d[r:%2d]\t",curFrames[i],etcTable[i]);
			fprintf(resultFp,"%2d[r:%2d]\t",curFrames[i],etcTable[i]);
		}else{
			printf("%2d(r:%2d)\t",curFrames[i],etcTable[i]);
			fprintf(resultFp,"%2d(r:%2d)\t",curFrames[i],etcTable[i]);
		}
	}

	printf("=> ");
	fprintf(resultFp,"=> ");
	if(isHit){
		printf("Hit!!\n");
		fprintf(resultFp,"Hit!!\n");
	}
	else if(isReplace){
		printf("pageFault && pageReplace\n");
		fprintf(resultFp,"pageFault && pageReplace\n");
	}
	else{
		printf("pageFault\n");
		fprintf(resultFp,"pageFault\n");
	}
}

// 참조 횟수가 가장 낮은 페이지를 교체
void lfu(int pageCount,int dataMode){
	
	int* pageTable = (int*)malloc(sizeof(int)*pageCount);
	initZero(pageTable,pageCount);
	int* pageCountTable=(int*)malloc(sizeof(int)*pageCount);
	initZero(pageCountTable,pageCount);
	int* pageStream = (int*)malloc(sizeof(int)*pageRefStringCount);
	int pageFault=0;
	int hits=0;

	if(dataMode==1){
		randStream(pageStream);
	} else {
		numbersFileRead(pageStream,NULL,NULL);
	}

	int flag,repindex,leastcount;
	
	for(int i=0;i<pageRefStringCount;i++){
			flag=0; // hit여부.
			for(int j=0;j<pageCount;j++){
					if(pageStream[i]==pageTable[j]){
							flag=1; // hit
							pageCountTable[j]++; //참조 횟수 증가.
							hits++;
							lfuAndScPrint(5,i,1,0,pageStream[i],pageTable,pageCountTable,pageCount,0);
							break;
					}
			}

			if(flag==0 && pageFault<pageCount){ // 페이지 폴트인데 교체 안하는 경우.
					 // 해당 참조 페이지를 추가.
					 pageTable[pageFault]=pageStream[i];
					 pageCountTable[pageFault]=1;
					 pageFault++;

					 lfuAndScPrint(5,i,0,0,pageStream[i],pageTable,pageCountTable,pageCount,0);
			}
			else if(flag==0){ // 페이지 폴트에 교체도 해야함
					 repindex=0; // 교체 당할 인덱스
					 leastcount=pageCountTable[0];
					 for(int j=1;j<pageCount;j++){
							// 참조 횟수가 적은 걸 찾는 과정.
							if(pageCountTable[j]<leastcount){ // 만약 참조횟수가 같다면, 앞에 있는걸 교체한다는 것을 알 수 있음.
									repindex=j; 		
									leastcount=pageCountTable[j];
							}
					 }

					 // 페이지 교체.
					 pageTable[repindex]=pageStream[i];
					 pageCountTable[repindex]=1;
					 pageFault++;

					 lfuAndScPrint(5,i,0,1,pageStream[i],pageTable,pageCountTable,pageCount,0);
			}
	}
	
	printResult(5,pageTable,pageCount,hits,pageFault);	
	
	free(pageTable);
	free(pageCountTable);
	free(pageStream);
}


int findAndUpdate(int x,int arr[],int second_chance[],int frames) {
	int i;
	for(i = 0; i < frames; i++){ // 여기서 frames는 현재 페이지 테이블에 들어있는 페이지의 개수임.        
		if(arr[i] == x){ 
			// 만약 찾았다면, 그냥 그 놈의 참조비트만 1로 바뀌고 그 외 아무 일도 안일어남.
			second_chance[i] = 1;
			return 1;
		}
	}
	return 0;     
}

int replaceAndUpdate(int x,int arr[],int second_chance[],int frames,int pointer){
	while(1){ // second_chance 비트가 0인걸 찾을때까지. pointer 부터 시작해서 순환큐로 찾아봄.
		if(!second_chance[pointer]) { 
			//바꿀 후보를 가리키는 포인터가 가리키는 곳의 r_bit가 0이면
			arr[pointer] = x; // 수정하고
			second_chance[pointer]=1; // 프레임에 처음 반입될때는 사용비트를 1로 설정. 이거는 피피티를 따름.
			return (pointer + 1) % frames; 
			// 그 교체한 놈의 다음을 가리키도록 포인터 위치를 리턴.
		}
		second_chance[pointer] = 0; // r_bit가 1이라면, 0으로 바꾸고 포인터 앞으로 증가.
		pointer = (pointer + 1) % frames;
	}
}

void sc(int pageCount,int dataMode){
	int* pageStream = (int*)malloc(sizeof(int)*pageRefStringCount);
	int* pageTable=(int*)malloc(sizeof(int)*pageCount); 
	initZero(pageTable,pageCount);

	int pageFault=0;
	int hits=0;

	if(dataMode==1){
		randStream(pageStream);
	} else {	
		numbersFileRead(pageStream,NULL,NULL);
	}

    int pointer, x;
	pointer = 0;
					     
	int* second_chance_r_bit=(int*)malloc(sizeof(int)*pageCount); // 현재 페이지 안에 있는 페이지들의 r비트 정보가 담긴 배열.
	initZero(second_chance_r_bit,pageCount);

	for(int i = 0; i < pageRefStringCount; i++){
		x = pageStream[i];
		if(!findAndUpdate(x,pageTable,second_chance_r_bit,pageCount)){  // hit인지 여부 확인. hit이면 second_chance 비트 1로 설정.
			pointer = replaceAndUpdate(x, pageTable, second_chance_r_bit, pageCount, pointer); // 삽입 혹은 교체.
			
			//출력
			if(pageFault<pageCount){ // 아직 페이지fault 횟수가 총 페이지 개수보다 작으면 아직 교체는 안발생함.
				lfuAndScPrint(6,i,0,0,pageStream[i],pageTable,second_chance_r_bit,pageCount,pointer);
			}else{	// 페이지 fault 횟수가 총 페이지 개수를 넘으면 교체 발생임.
				lfuAndScPrint(6,i,0,1,pageStream[i],pageTable,second_chance_r_bit,pageCount,pointer);
			}
			pageFault++;
			continue;
		}
		
		hits++;
		lfuAndScPrint(6,i,1,0,x,pageTable,second_chance_r_bit,pageCount,pointer);
	}

	printResult(6,pageTable,pageCount,hits,pageFault);

	free(pageTable);
	free(second_chance_r_bit);
	free(pageStream);
}

void escPrint(int index,int isHit,int isReplace,int refStr,int refStrU,int refStrM,int* curFrames,int* curFramesU,int* curFramesM,int pageCount,int pointer){
	if (index==0){
		printf("[%s] 알고리즘\n",algorithmName[7]);	
		fprintf(resultFp,"[%s] 알고리즘\n",algorithmName[7]);	
	}
	printf("[%3d]번째 참조스트링: %2d(u:%d,m:%d) )\t",index,refStr,refStrU,refStrM);
	fprintf(resultFp,"[%3d]번째 참조스트링: %2d(u:%d,m:%d) )\t",index,refStr,refStrU,refStrM);
	for(int i=0; i<pageCount;i++){
		if (curFrames[i]<=0){
			if(pointer==i){
				printf("__[u:_,m:_]\t");
				fprintf(resultFp,"__[u:_,m:_]\t");
			} else{
				printf("__(u:_,m:_)\t");
				fprintf(resultFp,"__(u:_,m:_)\t");			
			}
			continue;
		}

		
		if(pointer==i){
			printf("%2d[u:%d,m:%d]\t",curFrames[i],curFramesU[i],curFramesM[i]);
			fprintf(resultFp,"%2d[u:%d,m:%d]\t",curFrames[i],curFramesU[i],curFramesM[i]);
		}else{
			printf("%2d(u:%d,m:%d)\t",curFrames[i],curFramesU[i],curFramesM[i]);
			fprintf(resultFp,"%2d(u:%d,m:%d)\t",curFrames[i],curFramesU[i],curFramesM[i]);
		}
	}

	printf("=> ");
	fprintf(resultFp,"=> ");
	if(isHit){
		printf("Hit!!\n");
		fprintf(resultFp,"Hit!!\n");
	}
	else if(isReplace){
		printf("pageFault && pageReplace\n");
		fprintf(resultFp,"pageFault && pageReplace\n");
	}
	else{
		printf("pageFault\n");
		fprintf(resultFp,"pageFault\n");
	}
}

void esc(int pageCount,int dataMode){	
	int* pageStream = (int*)malloc(sizeof(int)*pageRefStringCount);
	int* useBitStream=(int*)malloc(sizeof(int)*pageRefStringCount);
	int* modifyBitStream=(int*)malloc(sizeof(int)*pageRefStringCount);
	int pageFault=0;
	int hits=0;
	if(dataMode==1){
		randStream(pageStream);
		srand(seed+1000);
		for (int i=0;i<pageRefStringCount;i++){
			modifyBitStream[i]=rand()%2; //0 혹은 1의 값만 들어가도록.
		}
		
		for (int i=0;i<pageRefStringCount;i++){
			if(modifyBitStream[i]==1){
				useBitStream[i]=1; // 0 1은 없는 거임. 
			} else {
				useBitStream[i]=rand()%2;
			}
		}

	} else {
		numbersFileRead(pageStream,useBitStream,modifyBitStream);
	}

    int pointer, x, x_u, x_m;
	pointer = 0;
	int* pageTable=(int*)malloc(sizeof(int)*pageCount);
	int* u_pageTable= (int*)malloc(sizeof(int)*pageCount);
	int* m_pageTable=(int*)malloc(sizeof(int)*pageCount);

	initZero(pageTable,pageCount);

	for(int i = 0; i < pageRefStringCount; i++){ // 참조 스트링 순회
		x = pageStream[i]; 
		x_u=useBitStream[i];
		x_m=modifyBitStream[i];

		if(pageFault<pageCount) { // 아직 페이지 다 못채웠다면
			int isIn=-1; // 찾았는지 여부
			for(int j=0;j<pageFault;j++){
				if(pageTable[j]==x){
					isIn=j; // 찾은 인덱스 기억
					break;
				}
			}

			if(isIn>=0){ 
				// 비트 교체
				u_pageTable[isIn]=x_u;
				m_pageTable[isIn]=x_m;
				hits++;
				// 페이지 히트. 출력
				escPrint(i,1,0,x,x_u,x_m,pageTable,u_pageTable,m_pageTable,pageCount,pointer);
			}else{
				// 못찾았다면 다음 위치에 넣음
				pageTable[pointer]=x;
				u_pageTable[pointer]=x_u;
				m_pageTable[pointer]=x_m;
				pageFault++; // pf 증가. 
				// 포인터도 다음으로 전진.
				pointer = (pointer + 1) % pageCount;
				// 출력해야함. 페이지 폴트만 발생. 
				escPrint(i,0,0,x,x_u,x_m,pageTable,u_pageTable,m_pageTable,pageCount,pointer);
			}
		} 
		else { // 일단 모든 페이지를 사용하고 있는 경우	
			int isIn=-1;
			for(int j=0;j<pageCount;j++){
				if(pageTable[j]==x){
					isIn=j;
					break;
				}
			}

			if(isIn>=0){
				u_pageTable[isIn]=x_u;
				m_pageTable[isIn]=x_m;
				hits++;
				// 페이지 히트. 출력.
				escPrint(i,1,0,x,x_u,x_m,pageTable,u_pageTable,m_pageTable,pageCount,pointer);
			}else{
				// 페이지 교체 해야함.
				int replaceIndex=-1;
				while(replaceIndex<0){ // 페이지 교체 위치 찾을 때까지.
					for(int k=0;k<pageCount;k++){ // 0,0  비트 찾기. 한바퀴 순회. pointer가 가리키는 지점부터 시작.
						if(u_pageTable[pointer]==0 && m_pageTable[pointer]==0){
							replaceIndex=pointer;
							pointer = (pointer + 1) % pageCount;
							break;
						}
						else{
							u_pageTable[pointer]=0;
							pointer = (pointer + 1) % pageCount;
						}
					}
				
					if(replaceIndex>=0){ // 위치 찾음. 내용 교체.			
						pageTable[replaceIndex]=x;
						u_pageTable[replaceIndex]=x_u;
						m_pageTable[replaceIndex]=x_m;
						continue;
					}

					// 1 단계 실패 후에는 수정비트를 0으로 바꿔나가면서 0,0을 찾아봄. 
					for(int k=0;k<pageCount;k++){
						if(u_pageTable[pointer]==0 && m_pageTable[pointer]==0){
							replaceIndex=pointer;
							pointer = (pointer + 1) % pageCount;
							break;
						}
						else{
							m_pageTable[pointer]=0; // 사용 비트를 0으로 바꿔나가기.
							pointer = (pointer + 1) % pageCount;
						}
					}

					if(replaceIndex>=0){			
						pageTable[replaceIndex]=x;
						u_pageTable[replaceIndex]=x_u;
						m_pageTable[replaceIndex]=x_m;
						continue;
					}


					replaceIndex=pointer;
					pageTable[replaceIndex]=x;
					u_pageTable[replaceIndex]=x_u;
					m_pageTable[replaceIndex]=x_m;
					pointer = (pointer + 1) % pageCount;
				}

				pageFault++;
				//페이지 폴트 & 페이지 교체 출력
				escPrint(i,0,1,x,x_u,x_m,pageTable,u_pageTable,m_pageTable,pageCount,pointer);
			}
		}
	}

	printf("%s 결과: ",algorithmName[7]);
	fprintf(resultFp,"%s 결과: ",algorithmName[7]);
	for(int i=0;i<pageCount;i++){
			if(pointer==i){
				printf("%2d[u:%d,m:%d]\t",pageTable[i],u_pageTable[i],m_pageTable[i]);
				fprintf(resultFp,"%2d[u:%d,m:%d]\t",pageTable[i],u_pageTable[i],m_pageTable[i]);
			}else{
				printf("%2d(u:%d,m:%d)\t",pageTable[i],u_pageTable[i],m_pageTable[i]);
				fprintf(resultFp,"%2d(u:%d,m:%d)\t",pageTable[i],u_pageTable[i],m_pageTable[i]);
			}
			if(i<pageCount-1){
					printf(", ");
					fprintf(resultFp,", ");
			}
	}
	printf("\n페이지 hit횟수: %d, 페이지 fault 횟수: %d, Optimal알고리즘의 페이지 fault 횟수:%d\n\n", hits,pageFault,optimalPf);
	fprintf(resultFp,"\n페이지 hit횟수: %d, 페이지 fault 횟수: %d, Optimal알고리즘의 페이지 fault 횟수:%d\n\n", hits,pageFault,optimalPf);
	
	free(pageTable);
	free(u_pageTable);
	free(m_pageTable);

	free(pageStream);
	free(useBitStream);
	free(modifyBitStream);
}

void randStream(int* stream){
	srand(seed);
	for(int i=0;i<PAGESTREAM;i++){
			stream[i]=(rand()%30)+1;
	}
}

void initZero(int* arr,int size){
	for(int i=0;i<size;i++){
			arr[i]=0;
	}
}

void printResult(int algorithm, int* pageTable,int pageCount,int hits,int pageFault){
	printf("%s 결과: ",algorithmName[algorithm]);
	fprintf(resultFp,"%s 결과: ",algorithmName[algorithm]);
	for(int i=0;i<pageCount;i++){
			printf("%d",pageTable[i]);
			fprintf(resultFp,"%d",pageTable[i]);
			if(i<pageCount-1){
					printf(", ");
					fprintf(resultFp,", ");
			}
	}
	
	if(algorithm==1){
		printf("\n페이지 hit횟수: %d, 페이지 fault 횟수: %d\n\n", hits, pageFault);
		fprintf(resultFp,"\n페이지 hit횟수: %d, 페이지 fault 횟수: %d\n\n", hits, pageFault);
	}else{
		printf("\n페이지 hit횟수: %d, 페이지 fault 횟수: %d, Optimal알고리즘의 페이지 fault 횟수:%d\n\n", hits,pageFault,optimalPf);
		fprintf(resultFp,"\n페이지 hit횟수: %d, 페이지 fault 횟수: %d, Optimal알고리즘의 페이지 fault 횟수:%d\n\n", hits,pageFault,optimalPf);
	}
}
		
// 입력 문자열 토크나이징 함수. string을 구분자를 기준으로 구분해서 argv이차원배열에 저장해줌.
//그리고 구분 개수 리턴
int split(char* string, char* seperator, char* argv[])
{
		int argc = 0;
		char* ptr = NULL;
		
		ptr = strtok(string, seperator);
		while (argc<3 && ptr != NULL) {
			argv[argc++] = ptr;
			ptr = strtok(NULL, " ");
		}
		
		if (argc==3 && ptr!=NULL)
				return -1;
		return argc;
}

void initPageSimulator(int* arr){
	arr[1]=1;
	for(int i=2;i<=8;i++)
		arr[i]=-1;
}

// 랜덤 파일 생성.
void numbersRandCreate(){
	int* tmpStream=(int*)malloc(sizeof(int)*PAGESTREAM);
	randStream(tmpStream);
	int* tmpUseBits=(int*)malloc(sizeof(int)*PAGESTREAM);
	int* tmpModifyBits=(int*)malloc(sizeof(int)*PAGESTREAM);
	srand(seed+1000);
	for (int i=0;i<PAGESTREAM;i++){
		tmpModifyBits[i]=rand()%2; //0 혹은 1의 값만 들어가도록.
	}
	
	for (int i=0;i<PAGESTREAM;i++){
		if(tmpModifyBits[i]==1){
			tmpUseBits[i]=1; // 0 1은 없는 거임. 
		} else {
			tmpUseBits[i]=rand()%2;
		}
	}

	char buf[BUFMAX];
	for(int i=0;i<PAGESTREAM;i++){
		sprintf(buf,"%d %d %d\n",
					tmpStream[i],tmpUseBits[i],tmpModifyBits[i]);
		fputs(buf,fp);
	}
	fclose(fp);

	if((fp=fopen(sampleFile,"r"))==NULL){
			fprintf(stderr,"fopen error for %s\n",sampleFile);
			exit(1);
	}

	free(tmpStream);
	free(tmpUseBits);
	free(tmpModifyBits);
}

void numbersFileRead(int* pageStream,int* useBitStream,int* modifyBitStream){
	char buf[BUFMAX];
	fseek(fp,0,SEEK_SET);

	int index=0;
	while(fgets(buf,BUFMAX,fp)!=NULL){
		buf[BUFMAX-1]='\0';
		char* ptr = NULL;
		ptr = strtok(buf, " ");
		int i=0;
		while (ptr != NULL) {
			if(i==0){
				pageStream[index] = atoi(ptr);
				if(pageStream[index]<1 || pageStream[index]>30){
					fprintf(stderr,"ERROR: The number in the file must be greater than 1 and less than 30\n");
					exit(1);
				}
			}
			else if(i==1 && useBitStream!=NULL){
				useBitStream[index] = atoi(ptr);
				if(useBitStream[index]<0 || useBitStream[index]>1){
					fprintf(stderr,"ERROR: R_bit must be 0 or 1\n");
					exit(1);
				}
			}
			else if(i==2 && modifyBitStream!=NULL){
				modifyBitStream[index] = atoi(ptr);
				if(modifyBitStream[index]<0 || modifyBitStream[index]>1){
					fprintf(stderr,"ERROR: M_bit must be 0 or 1\n");
					exit(1);
				}

				// 해당 esc 알고리즘은 0 1은 고려하지 않는 알고리즘입니다.
				if(useBitStream[index]==0 && modifyBitStream[index]==1){
					useBitStream[index]=1; // 0 1은 1 1로 수정해서 받음.
				}
			}

			i++;
			ptr = strtok(NULL, " ");
		}
		if(i!=3){
				fprintf(stderr,"ERROR: There should be three numbers per line.\n");
				exit(1);
		}
		index++;
	}
	fseek(fp,0,SEEK_SET);
}

