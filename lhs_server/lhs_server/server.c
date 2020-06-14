// KNU 2016115743 이한성
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <winsock2.h>
#include <string.h>
#include <windows.h>

#define BUF_SIZE 1024
#define NAME_SIZE 30
#define DEF_NAME "NEW BIE"
#define DEF_PORT 50000
#define MAX_CLI 20
#define MAX_ROOM 11
#define READ	3
#define	WRITE	5

#define	R_EMPTY	-1

//state 정의//
#define S_EMPTY -1
#define S_WAIT	0
#define S_RECV	1
#define S_SEND	2
#define S_CHAT	3
//----------//

//	클라로 명령	//
#define SC_CHAT		"!0"
#define SC_EXIST	"!1"
#define	SC_ITARGET	"!2"
#define	SC_INVITE	"!3!"
#define SC_SUCCESS	"!4"
#define	SC_FAIL		"!5!"
#define SC_SINFO	"!6!"
//--------------//

/*	클라이언트에서 명령	/
	종료					:	!Q or !q				1	//대기실에서 가능
	사용자 리스트			:	!L or !l				2	//대기실에서는 전체 리스트, 채팅중일때는 참여 리스트.
	채팅방 시작 			:	!R or !r				3	//대기실에서 가능
	채팅 요청 수락			:	!Y or !y				4	//요청 받음 상태일때만 가능
	채팅 요청 거절			:	!N or !n				5	//요청 받음 상태일때만 가능
	채팅 종료				:	!E or !e				6	//채팅중 상태에서 가능
	이름변경				:	!CC_NAMEname			7	//클라에서 작업후 보냄 , 이름겹치면 클라로 명령어(SC_EXIST)*/
#define	CC_NAME	"!1"
/*	채널 리스트 출력		:	!C or !c				8	//대기실에서 가능
	채팅 초대				:	!I or !i				9	//채팅방에서 가능	클라이언트에게 명령(SC_ITARGET)
	현재 상태 출력			:	!S or !s				10	//전부 사용, 현재 상황 출력
	초대 대상 획득			:	!CC_ITARGETname			11	//초대요청후 들어온 이름을 획득.채팅요청에 대한 응답, 초대는 대기중인 상대만 가능*/
#define	CC_ITARGET	"!2"	
//	-1은 명령이 아닌걸로 판단 , 일반 대화는 명령 (!CC_CHAT)*/
#define	CC_CHAT	"!0"
//----------------------//

typedef struct user_data {
	// unique
	char name[NAME_SIZE];
	SOCKET sock;

	//non_unique
	int state; 
	int room; // -1은 대기실, 대기상태일때는 요청받은 room을 표현
} USR;

typedef struct    // socket info
{
	SOCKET hClntSock;
	SOCKADDR_IN clntAdr;
} PER_HANDLE_DATA, * LPPER_HANDLE_DATA;

typedef struct    // buffer info
{
	OVERLAPPED overlapped;	//연구 필요
	WSABUF wsaBuf;			//연구 필요
	char buffer[BUF_SIZE];
	int rwMode;    // READ or WRITE
} PER_IO_DATA, * LPPER_IO_DATA;


//	전역 변수	//
int g_max_client = MAX_CLI;
int g_port = 0;
CRITICAL_SECTION g_CS;
USR user_list[MAX_CLI];
int room_list[MAX_ROOM]; // 유저수 보유, 0이면 없는 방
//--------------//


DWORD WINAPI ThreadMain(LPVOID CompletionPortIO);
void ErrorHandling(char* message);

int u_index(void* data, int target);
int u_in(USR usr);
int* u_state_filter(int state, int room);
int opr_check(char* buf);
int r_open();
int ser_send(LPPER_IO_DATA* ioInfo, SOCKET* sock, char* str);
int make_chat_opr(char* str, char* text) {
	char buf[BUF_SIZE];
	strcpy(buf, text);
	strcpy(str, SC_CHAT);
	strcat(str, buf);
	return 0;
}

int main(int argc, char* argv[]) {
	WSADATA wsaData;
	SOCKET hServSock;
	SOCKADDR_IN servAdr;
	LPPER_IO_DATA ioInfo;
	LPPER_HANDLE_DATA handleInfo;

	HANDLE hComPort;
	SYSTEM_INFO sysInfo;

	int recvBytes, i, flags = 0;
	unsigned ui;
	char str[BUF_SIZE];

	WSAEVENT evObj;

	//	init	//
	printf("Type port, if you want default port <%d>, type 'Y'or'y' : ", DEF_PORT);
	scanf("%s",str);

	if (strcmp(str, "Y") == 0 || strcmp(str, "y") == 0) {
		g_port = DEF_PORT;
		printf("server port : %d\n", DEF_PORT);
	}
	else {
		g_port = atoi(str);
		printf("server port : %d\n", g_port);
	}

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartup() error!");

	for (i = 0; i < MAX_CLI; i++)
		user_list[i].state = S_EMPTY;
	for (i = 0; i < MAX_ROOM; i++)
		room_list[i] = 0;

	InitializeCriticalSection(&g_CS);
	//----------//
	
	//	server socket add	//
	hServSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family = AF_INET;
	servAdr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAdr.sin_port = htons(g_port);

	bind(hServSock, (SOCKADDR*)&servAdr, sizeof(servAdr));
	listen(hServSock, MAX_CLI);
	//----------------------//

	//	CompletionPort, thread pool create	//
	hComPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
	GetSystemInfo(&sysInfo);
	for (ui = 0; ui < sysInfo.dwNumberOfProcessors; ui++) {
		if (_beginthreadex(NULL, 0, ThreadMain, (LPVOID)hComPort, 0, NULL) == 0)
			ErrorHandling("_beginthreadex() error!");
	}
	//--------------------------------------//

	//	body	//
	while (1) {
		SOCKET hClntSock;
		SOCKADDR_IN clntAdr;
		int addrLen = sizeof(clntAdr);

		hClntSock = accept(hServSock, (SOCKADDR*)&clntAdr, &addrLen);// accept 대기
		printf("user accepted\n");
		//소켓의 정보를 넘겨줌
		handleInfo = (LPPER_HANDLE_DATA)malloc(sizeof(PER_HANDLE_DATA));
		handleInfo->hClntSock = hClntSock;
		memcpy(&(handleInfo->clntAdr), &clntAdr, addrLen);
		//user에 등록

		CreateIoCompletionPort((HANDLE)hClntSock, hComPort, (DWORD)handleInfo, 0);
		// hClntSock과 hComPort 연결,handleInfo로 관련 정보 넘겨줌

		evObj = WSACreateEvent();
		ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
		memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
		ioInfo->wsaBuf.len = BUF_SIZE;		//wsaBuf의 길이에 BUF_SIZE
		ioInfo->wsaBuf.buf = ioInfo->buffer;//wsabuf의 버퍼와 구조체의 버퍼 연결
		ioInfo->rwMode = READ;				//처음 연결은 읽기로 받음
		ioInfo->overlapped.hEvent = evObj;
		if (WSARecv(handleInfo->hClntSock, &(ioInfo->wsaBuf),
			1, &recvBytes, &flags, &(ioInfo->overlapped), NULL)== SOCKET_ERROR){
			if   (WSAGetLastError() == WSA_IO_PENDING){
				puts("Background data receive");
				WSAWaitForMultipleEvents(1, &evObj, TRUE, WSA_INFINITE, FALSE);
				WSAGetOverlappedResult(handleInfo->hClntSock, &(ioInfo->overlapped), &recvBytes, FALSE, NULL);
			}
			else {ErrorHandling("WSARecv() error");}
		}
	}
	//----------//

	//	term	//
	DeleteCriticalSection(&g_CS);
	WSACleanup();
	return 0;
	//----------//
}

DWORD WINAPI ThreadMain(LPVOID pComPort){
	HANDLE hComPort = (HANDLE)pComPort;
	SOCKET sock;
	DWORD bytesTrans;
	LPPER_HANDLE_DATA handleInfo; // 클라이언트의 소켓과 주소 전달
	LPPER_IO_DATA ioInfo; // 클라이언트 버퍼정보 저장
	DWORD flags = 0;
	USR user;

	int u_i, opr, i, j, k;
	//scdOpr : -1일떈 종료
	int scdOpr = 0, recvBytes;
	char buf[BUF_SIZE];
	char* parse_buf;
	char str[BUF_SIZE];
	char str2[BUF_SIZE];
	char name[NAME_SIZE];
	int* filter_arr;

	WSAEVENT evObj;

	parse_buf = (char*)malloc(sizeof(char) * BUF_SIZE);
	while (1) {
		//이벤트 발생한것을 cp에서 꺼내 인자들에 저장
		 GetQueuedCompletionStatus(hComPort, &bytesTrans, //complition port를 넘겨줌, 송수신 데이터 크기저장
			(LPDWORD)&handleInfo, (LPOVERLAPPED*)&ioInfo, INFINITE); //hComPort의정보  handleInfo, ioInfo에 저장,대기시간 무한
		sock = handleInfo->hClntSock; // sock에 대상 클라이언트 소켓 등록

		//	body	//
		if (ioInfo->rwMode == READ) {
			printf("message received!");
			//유저와 소켓 연결//
			u_i = u_index(&sock, 2);
			if (u_i == -1) {	//신규유저
				strcpy(user.name, DEF_NAME);
				user.sock = sock;
				user.state = S_WAIT;
				user.room = R_EMPTY;

				u_i = u_in(user);	//리스트에 유저 추가
				printf("[%s] connected\n", user.name);
				//if (u_i == -1) {}	// 유저리스트에 있을때 오류처리
				//else {}
			}
			else {			//기존유저
				EnterCriticalSection(&g_CS);
				user = user_list[u_i];
				LeaveCriticalSection(&g_CS);
			}
			if (bytesTrans == 0)    // EOF 전송 시
			{
				EnterCriticalSection(&g_CS);
				user_list[u_i].sock = NULL;
				user_list[u_i].state = S_EMPTY;
				strcpy(user_list[u_i].name, DEF_NAME);
				LeaveCriticalSection(&g_CS);

				closesocket(sock);
				free(handleInfo); free(ioInfo);
				printf("[%s] disconnected\n", user.name);
				continue;
			}
			//----------------//

			// 유저 메시지 //
			strcpy(buf, ioInfo->wsaBuf.buf);
			printf("msg :%s\n", buf);
			//-------------//

			scdOpr = -2; // 의미없음
			opr = opr_check(buf);
			switch (opr) {	//명령어 체크
			case 0:	//일반 채팅
				parse_buf = strtok(buf, CC_CHAT);
				sprintf(str, "[%s] : ", user.name);
				strcat(str, parse_buf);
				EnterCriticalSection(&g_CS);
				filter_arr = u_state_filter(S_CHAT, user.room);
				for (j = 0; j < MAX_CLI + 1; j++) {
					if (filter_arr[j] == -1)
						break;
					make_chat_opr(str, str);
					strcat(str, "\n");
					ser_send(&ioInfo, &(user_list[i].sock), str);
				}
				LeaveCriticalSection(&g_CS);
				free(filter_arr);
				break;
			case 1:		//종료
				puts(buf);
				if (user.state == S_WAIT) {
					EnterCriticalSection(&g_CS);
					user_list[u_i].sock = NULL;
					user_list[u_i].state = S_EMPTY;
					LeaveCriticalSection(&g_CS);

					closesocket(sock);
					free(handleInfo); free(ioInfo);
					scdOpr = -1;// 접속종료로 나감
					printf("[%s] 접속 종료", user.name);
				}
				else {
					make_chat_opr(str, "대기실에서 종료 가능, 채팅중이라면 !E, !e로 채팅 종료.");
					// 송신 //
					ser_send(&ioInfo, &sock, str);
					//------//
				}
				break;

			case 2:		//사용자리스트
				strcpy(str, "<유저 리스트>\n");
				EnterCriticalSection(&g_CS);
				for (i = 0; i < MAX_CLI; i++) {
					j = user_list[i].state;
					switch (j) {
					case S_WAIT:
						sprintf(str2, "%s이름 : [%s] , 상태 : 대기실\n", str, user_list[i].name);
						make_chat_opr(str, str2);
						break;
					case S_RECV:
						sprintf(str2, " %s이름 : [%s] , 상태 : 채팅요청수신\n", str, user_list[i].name);
						make_chat_opr(str, str2);
						break;
					case S_SEND:
						sprintf(str2, "%s이름 : [%s] , 상태 : 채팅요쳥송신\n", str, user_list[i].name);
						make_chat_opr(str, str2);
						break;
					case S_CHAT:
						sprintf(str2, "%s이름 : [%s] , 상태 : 채팅중(ch:%d)\n", str, user_list[i].name, user_list[i].room);
						make_chat_opr(str, str2);
						break;
					default:
						break;
					}
				}
				LeaveCriticalSection(&g_CS);
				printf("list to [%s]\n", user.name);

				// 송신 //
				ser_send(&ioInfo, &sock, str);
				//------//
				break;

			case 3:		//채팅방 시작
				if (user.state == S_WAIT) {
					i = r_open();
					if (i == -1)
						make_chat_opr(str, "최대 채팅방 초과!");
					else {
						user.state = S_CHAT;
						user.room = i;

						EnterCriticalSection(&g_CS);
						user_list[u_i] = user;
						LeaveCriticalSection(&g_CS);

						/*sprintf(str, "%s%d", SC_SINFO, S_CHAT);
						ser_send(&ioInfo, &sock, str);*/

						sprintf(str2, "ch.%d 채팅방 생성!", i);
						make_chat_opr(str, str2);
						printf("[%s] open ch.%d\n", user.name, i);
					}
				}
				else {
					strcpy(str, "채팅방은 대기실 상태에서만 만들수 있습니다.\n");
					make_chat_opr(str, str);
				}
				// 송신 //
				ser_send(&ioInfo, &sock, str);
				//------//
				break;

			case 4:		//채팅요청 수락
				if (user.state == S_RECV) {
					EnterCriticalSection(&g_CS);
					user_list[u_i].state = S_CHAT;
					room_list[user.room]++;
					sprintf(str, "%s%d", SC_SINFO, S_CHAT);
					ser_send(&ioInfo, &sock, str);
					//방의 인원들에게 알림
					sprintf(str2, "[%s]가 입장했습니다.", user.name);
					make_chat_opr(str, str2);
					filter_arr = u_state_filter(S_CHAT, user.room);
					for (i = 0; i < MAX_CLI + 1; i++) {
						j = filter_arr[i];
						if (j == -1)
							break;
						ser_send(&ioInfo, &(user_list[j].sock), str);
					}
					LeaveCriticalSection(&g_CS);
					free(filter_arr);
				}
				else {
					/*strcpy(str, "요청이 들어와야 수락가능\n");
					ser_send(&ioInfo, &sock, str);*/
				}
				break;

			case 5:		//채팅요청 거절
				if (user.state == S_RECV) {
					EnterCriticalSection(&g_CS);
					user_list[u_i].state = S_WAIT;
					//방의 인원들에게 알림
					sprintf(str2, "[%s]가 초대 거절.", user.name);
					make_chat_opr(str, str2);
					filter_arr = u_state_filter(S_CHAT, user.room);
					for (i = 0; i < MAX_CLI + 1; i++) {
						j = filter_arr[i];
						if (j == -1)
							break;
						ser_send(&ioInfo, &(user_list[j].sock), str);
					}
					user_list[u_i].room = R_EMPTY;
					LeaveCriticalSection(&g_CS);
					free(filter_arr);
				}
				else {//일반 채팅으로 넘김}
					break;

			case 6:		//채팅 종료
				if (user.state == S_CHAT) {
					i = user.room;
					EnterCriticalSection(&g_CS);
					user.state = S_WAIT;
					room_list[user.room]--;
					user.room = R_EMPTY;
					user_list[u_i] = user;
					LeaveCriticalSection(&g_CS);
					sprintf(str, "%s%d", SC_SINFO, S_WAIT);
					ser_send(&ioInfo, &sock, str);
					sprintf(str2, "ch.%d 채팅방에서 퇴장!", i);
					make_chat_opr(str, str2);
					printf("[%s] exit from ch.%d\n", user.name, i);
				}
				else {
					if (user.state == S_CHAT) { // 일반 채팅으로 파악
						filter_arr = u_state_filter(S_CHAT, user.room);
						EnterCriticalSection(&g_CS);
						for (j = 0; j < MAX_CLI + 1; j++) {
							if (filter_arr[j] == -1)
								break;
							make_chat_opr(str, str);
							ser_send(&ioInfo, &(user_list[i].sock), str);
						}
						LeaveCriticalSection(&g_CS);
						free(filter_arr);
					}
				}

				// 송신 //
				ser_send(&ioInfo, &sock, str);
				//------//
				break;

			case 7:		//이름 변경
				strcpy(str, user.name);
				parse_buf = strtok(buf, CC_NAME);
				if (parse_buf != NULL) {
					i = u_index(parse_buf, 1);
					if (i == -1) {
						strcpy(name, parse_buf);
						strcpy(user.name, name);
						EnterCriticalSection(&g_CS);
						strcpy(user_list[u_i].name, name);
						LeaveCriticalSection(&g_CS);
						sprintf(str2, "[%s]에서 [%s]로 이름 변경", str, name);
						make_chat_opr(str, str2);
					}
					else
						sprintf(str, "%s", SC_EXIST); //클라에서 처리
				}
				else
					sprintf(str, "%s", SC_EXIST); //클라에서 처리

				// 송신 //
				puts(str);
				ser_send(&ioInfo, &sock, str);
				//------//
				break;

			case 8:		//채팅방 리스트
				strcpy(str, "<채팅방 리스트>\n");
				EnterCriticalSection(&g_CS);
				for (i = 1; i < MAX_ROOM; i++) {
					if (room_list[i] != 0) {
						sprintf(str2, "<ch.%d>\n", i);
						strcat(str, str2);
						filter_arr = u_state_filter(S_CHAT, i);
						k = 1;
						for (j = 0; j < MAX_CLI; j++) {
							if (filter_arr[j] == -1)
								break;
							printf("%d\n", filter_arr[j]);
							sprintf(str2, "%d.[%s]\n", k++, user_list[filter_arr[j]].name);
							strcat(str, str2);
						}
						free(filter_arr);
					}
				}
				LeaveCriticalSection(&g_CS);
				// 송신 //
				make_chat_opr(str, str);
				ser_send(&ioInfo, &sock, str);
				//------//
				break;

			case 9:		//채팅 초대
				if (user.state == S_CHAT)
					sprintf(str, SC_ITARGET);
				else
					make_chat_opr(str, "채팅방에서 사용 가능.");

				// 송신 //
				ser_send(&ioInfo, &sock, str);
				//------//
				break;

			case 10:	//현재 상태 출력
				i = user.state;
				switch (i) {
				case S_WAIT:
					sprintf(str2, "<내상태>\n이름 : [%s] , 상태 : 대기실", user.name);
					make_chat_opr(str, str2);
					break;
				case S_RECV:
					sprintf(str2, "<내상태>\n이름 : [%s] , 상태 : 채팅요청수신", user.name);
					make_chat_opr(str, str2);
					break;
				case S_SEND:
					sprintf(str2, "<내상태>\n이름 : [%s] , 상태 : 채팅요쳥송신", user.name);
					make_chat_opr(str, str2);
					break;
				case S_CHAT:
					sprintf(str2, "<내상태>\n이름 : [%s] , 상태 : 채팅중(ch:%d)", user.name, user.room);
					make_chat_opr(str, str2);
					break;
				default:
					sprintf(str2, "<내상태>\n이름 : [%s] , 상태 : 알 수 없음 ", user.name);
					make_chat_opr(str, str2);
					break;
				}
				puts(str);
				ser_send(&ioInfo, &sock, str);
				break;

			case 11:	//초대 대상 이름 획득, 초대 진행
				sprintf(str2, "%s%%s", SC_ITARGET);
				parse_buf = strtok(buf, SC_ITARGET);
				strcpy(name, parse_buf);
				i = u_index(name, 1);
				if (i == -1)
					make_chat_opr(str, "대상이 없음, 리스트 확인 : !L or !l");
				else {
					EnterCriticalSection(&g_CS);
					if (user_list[i].state == S_WAIT) {
						user_list[i].state = S_RECV;
						user_list[i].room = user.room;
						sprintf(str, "%s%d", SC_SINFO, S_RECV);
						ser_send(&ioInfo, &(user_list[i].sock), str);

						sprintf(str2, "%s[%s]가 채팅방 ch.%d로 초대했습니다. (수락: !Y,!y)(거절: !N,!n)", SC_INVITE, name, user.room);
						make_chat_opr(str, str2);
						// 송신 //
						ser_send(&ioInfo, &(user_list[i].sock), str);
						//------//
						sprintf(str2, "[%s]를 ch.%d 로 초대했습니다.", name, user.room);
						make_chat_opr(str, str2);
					}
					else {
						if (user_list[i].state == S_RECV)
							make_chat_opr(str, "대상이 먼저 초대받음");
						else if (user_list[i].state == S_CHAT)
							make_chat_opr(str, "대상이 채팅중");
						else
							make_chat_opr(str, "대상의 상태가 확인 안됨");
					}
					LeaveCriticalSection(&g_CS);
				}
				}//??
				// 송신 //
				ser_send(&ioInfo, &sock, str);
				//------//
				break;
			}
			if (scdOpr == -1)// 접속종료로 나감 261
				continue;
			//-------------//

			//	유저의 다음 요청	//
			evObj = WSACreateEvent();
			ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = BUF_SIZE;
			ioInfo->wsaBuf.buf = ioInfo->buffer;
			ioInfo->rwMode = READ;
			ioInfo->overlapped.hEvent = evObj;
			if (WSARecv(sock, &(ioInfo->wsaBuf),
				1, &recvBytes, &flags, &(ioInfo->overlapped), NULL) == SOCKET_ERROR)
			{
				if (WSAGetLastError() == WSA_IO_PENDING) {
					puts("Background data receive");
					WSAWaitForMultipleEvents(1, &evObj, TRUE, WSA_INFINITE, FALSE);
					WSAGetOverlappedResult(sock, &(ioInfo->overlapped), &recvBytes, FALSE, NULL);
				}
				else { ErrorHandling("WSARecv() error"); }
			}
			//----------------------//
		}
		else {
			puts("message sent!");
			free(ioInfo);
		}
	}
	free(parse_buf);
}

void ErrorHandling(char* message){
	fputs(message, stderr);
	fputc('\n', stderr);
	exit(1);
}
/*
	target 정의 
		name : 1
		sock : 2
*/
int u_index(void* data, int target) {
	int i;
	switch (target) {
		case 1:		//name
			EnterCriticalSection(&g_CS);
			for (i = 0; i < MAX_CLI; i++) {
				if (strcmp(user_list[i].name, (char*)data) == 0) {
					LeaveCriticalSection(&g_CS);
					return i;
				}
			}
			LeaveCriticalSection(&g_CS);
			return -1; // 일치되는값 없음
		case 2:		//sock
			EnterCriticalSection(&g_CS);
			for (i = 0; i < MAX_CLI; i++) {
				if (user_list[i].sock == *((SOCKET*)data)) {
					LeaveCriticalSection(&g_CS);
					return i;
				}
			}
			LeaveCriticalSection(&g_CS);
			return -1; // 일치되는값 없음
		default:
			return -2; // 타겟없음
	}
}

int u_in(USR usr) {
	int i;

	EnterCriticalSection(&g_CS);
	for (i = 0; i < MAX_CLI; i++) {
		if (user_list[i].state == S_EMPTY) {
			user_list[i] = usr;
			LeaveCriticalSection(&g_CS);
			return i;
		}
	}
	LeaveCriticalSection(&g_CS);
	return -1;
}

// 상태 체크, room은 -1을 넣으면 상관 안함, return은 인덱스 배열로 주며 끝엔 -1이 들어가 있음
int* u_state_filter(int state, int room) { 
	int i,num=0;
	int* arr;
	arr = (int*)malloc(sizeof(int)*(MAX_CLI + 1));
	arr[0] = -1;
	if (room == -1) {
		for (i = 0; i < MAX_CLI; i++)
			if (user_list[i].state == state)
				arr[num++] = i;
	}
	else {
		for (i = 0; i < MAX_CLI; i++)
			if (user_list[i].state == state && user_list[i].room == room)
				arr[num++] = i;
	}
	arr[num] = -1;
	return arr;
}

/*
	종료					:	!Q or !q	1	//대기실에서 가능
	사용자 리스트			:	!L or !l	2	//대기실에서는 전체 리스트, 채팅중일때는 참여 리스트.
	채팅방 시작 			:	!R or !r	3	//대기실에서 가능
	채팅 요청 수락			:	!Y or !y	4	//요청 받음 상태일때만 가능
	채팅 요청 거절			:	!N or !n	5	//요청 받음 상태일때만 가능
	채팅 종료				:	!E or !e	6	//채팅중 상태에서 가능
	이름변경				:	!1name		7	//클라에서 작업후 보냄 , 이름겹치면 클라로 명령어(!1)
	채널 리스트 출력		:	!C or !c	8	//대기실에서 가능
	채팅 초대				:	!I or !i	9	//채팅방에서 가능	클라이언트에게 명령(!2)
	현재 상태 출력			:	!S or !s	10	//전부 사용, 현재 상황 출력
	초대 대상 획득			:	!2name!room		11	//초대요청후 들어온 이름을 획득. 이것을 보내는것은 서버쪽에서 클라쪽으로 명령어(!2)를 보낸것, 초대는 대기중인 상대만 가능
	-1은 명령이 아닌걸로 판단 , 일반 대화는 명령 (!0)
*/
int opr_check(char* buf) {
	if (buf[0] != '!') return -1;

	if (buf[1] == CC_CHAT[1])
		return 0;
	else if (strcmp(buf, "!Q") == 0 || strcmp(buf, "!q") == 0)
		return 1;
	else if (strcmp(buf, "!L") == 0 || strcmp(buf, "!l") == 0)
		return 2;
	else if (strcmp(buf, "!R") == 0 || strcmp(buf, "!r") == 0)
		return 3;
	else if (strcmp(buf, "!Y") == 0 || strcmp(buf, "!y") == 0)
		return 4;
	else if (strcmp(buf, "!N") == 0 || strcmp(buf, "!n") == 0)
		return 5;
	else if (strcmp(buf, "!E") == 0 || strcmp(buf, "!e") == 0)
		return 6;
	else if (buf[1] == CC_NAME[1])
		return 7;
	else if (strcmp(buf, "!C") == 0 || strcmp(buf, "!c") == 0)
		return 8;
	else if (strcmp(buf, "!I") == 0 || strcmp(buf, "!i") == 0)
		return 9;
	else if (strcmp(buf, "!S") == 0 || strcmp(buf, "!s") == 0)
		return 10;
	else if (buf[1] == CC_ITARGET[1])
		return 11;
	else
		return -1;
}
int r_open() {
	int i;
	EnterCriticalSection(&g_CS);
	for(i=1;i< MAX_ROOM;i++)
		if (room_list[i] == 0) {
			room_list[i] = 1; 
			LeaveCriticalSection(&g_CS);
			return i;
		}
	LeaveCriticalSection(&g_CS);
	return -1;
}

int ser_send(LPPER_IO_DATA* ioInfo, SOCKET* sock, char* str) {
	memset(&((*ioInfo)->overlapped), 0, sizeof(OVERLAPPED));
	(*ioInfo)->wsaBuf.buf = str;
	(*ioInfo)->wsaBuf.len = BUF_SIZE;
	(*ioInfo)->rwMode = WRITE;
	WSASend(*sock, &((*ioInfo)->wsaBuf), 1, NULL, 0, &((*ioInfo)->overlapped), NULL);
	return 0;
}