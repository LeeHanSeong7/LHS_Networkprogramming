#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <winsock2.h>
#include <string.h>
#include <windows.h>

#define BUF_SIZE 1024
#define NAME_SIZE 30
#define DEF_NAME "2016115743 이한성"
#define DEF_PORT 50000
#define MAX_CLI 20
#define MAX_ROOM 11
#define READ	3
#define	WRITE	5

#define ST_WAIT 0
#define ST_CHAT 1

//state 정의//
#define S_EMPTY -1
#define S_WAIT	0
#define S_RECV	1
#define S_SEND	2
#define S_CHAT	3
//----------//

//
int g_max_client = MAX_CLI;
int g_port = 0;
//

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

DWORD WINAPI EchoThreadMain(LPVOID CompletionPortIO);
void ErrorHandling(char* message);

int u_index(void* data, int target);
int u_in(USR usr);
int u_out(USR usr); 
int* u_state_filter(int state, int room);
int opr_check(char* buf);
int r_open();

int user_num = 0;
USR user_list[MAX_CLI];
int room_list[MAX_ROOM]; // 유저수 보유, 0이면 없는 방

int main(int argc, char* argv[]) {
	WSADATA wsaData;
	SOCKET hServSock;
	SOCKADDR_IN servAdr;
	LPPER_IO_DATA ioInfo;
	LPPER_HANDLE_DATA handleInfo;

	HANDLE hComPort;
	SYSTEM_INFO sysInfo;

	int recvBytes, i, flags = 0;
	char str[BUF_SIZE];

	//	init	//
	if (argc != 2) {
		printf("Type port, if you want default port <%d>, type 'Y'or'y' : ", DEF_PORT);

		scanf("%s",str);
		i = strcmp(str,"Y");

		if (strcmp(str, "Y") == 0 || strcmp(str, "y") == 0) {
			printf("server port : %d", DEF_PORT);
			g_port = DEF_PORT;
		}
		else
			g_port = atoi(str);
	}

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartup() error!");

	for (i = 0; i < MAX_CLI; i++)
		user_list[i].state = -1;
	for (i = 0; i < MAX_ROOM; i++)
		room_list[i] = 0;
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
	for (i = 0; i < sysInfo.dwNumberOfProcessors; i++) {
		if (_beginthreadex(NULL, 0, EchoThreadMain, (LPVOID)hComPort, 0, NULL) == 0)
			ErrorHandling("_beginthreadex() error!");
	}
	//--------------------------------------//

	//	body	//
	while (1) {
		SOCKET hClntSock;
		SOCKADDR_IN clntAdr;
		int addrLen = sizeof(clntAdr);

		hClntSock = accept(hServSock, (SOCKADDR*)&clntAdr, &addrLen);// accept 대기

		//소켓의 정보를 넘겨줌
		handleInfo = (LPPER_HANDLE_DATA)malloc(sizeof(PER_HANDLE_DATA));
		handleInfo->hClntSock = hClntSock;
		memcpy(&(handleInfo->clntAdr), &clntAdr, addrLen);
		//user에 등록

		CreateIoCompletionPort((HANDLE)hClntSock, hComPort, (DWORD)handleInfo, 0);
		// hClntSock과 hComPort 연결,handleInfo로 관련 정보 넘겨줌

		//	넘겨줄 정보를 작성	//
		ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
		memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
		ioInfo->wsaBuf.len = BUF_SIZE;		//wsaBuf의 길이에 BUF_SIZE
		ioInfo->wsaBuf.buf = ioInfo->buffer;//wsabuf의 버퍼와 구조체의 버퍼 연결
		ioInfo->rwMode = READ;				//처음 연결은 읽기로 받음
		//----------------------//

		WSARecv(handleInfo->hClntSock, &(ioInfo->wsaBuf),
			1, &recvBytes, &flags, &(ioInfo->overlapped), NULL);
	}
	//----------//

	//	term	//
	WSACleanup();
	return 0;
	//----------//
}

DWORD WINAPI EchoThreadMain(LPVOID pComPort){
	HANDLE hComPort = (HANDLE)pComPort;
	SOCKET sock;
	DWORD bytesTrans;
	LPPER_HANDLE_DATA handleInfo; // 클라이언트의 소켓과 주소 전달
	LPPER_IO_DATA ioInfo; // 클라이언트 버퍼정보 저장
	DWORD flags = 0;
	USR user, target;

	int u_i,i,j,k;
	char buf[BUF_SIZE];
	char str[BUF_SIZE];
	char name[NAME_SIZE];
	int* filter_arr;

	while (1) {
		//이벤트 발생한것을 cp에서 꺼내 인자들에 저장
		GetQueuedCompletionStatus(hComPort, &bytesTrans, //complition port를 넘겨줌, 송수신 데이터 크기저장
			(LPDWORD)&handleInfo, (LPOVERLAPPED*)&ioInfo, INFINITE); //hComPort의정보  handleInfo, ioInfo에 저장,대기시간 무한
		sock = handleInfo->hClntSock; // sock에 대상 클라이언트 소켓 등록

		//	body	//
		if (ioInfo->rwMode == READ) {
			puts("message received!");
			if (bytesTrans == 0)    // EOF 전송 시
			{
				closesocket(sock);
				free(handleInfo); free(ioInfo);
				continue;
			}
			//유저와 소켓 연결//
			u_i = u_index(&sock, 2);
			if (u_i == -1) {	//신규유저
				strcpy(user.name, DEF_NAME);
				user.sock = sock;
				user.state = S_WAIT;
				user.room = -1;

				u_i = u_in(user);	//리스트에 유저 추가
				if (u_i == -1) {}	// 유저리스트에 있을때 오류처리
				else {}
			}
			else				//기존유저
				user = user_list[u_i];
			//----------------//

			// 유저 메시지 //
			strcpy(buf, &(ioInfo->wsaBuf));

			i = opr_check(buf);
				switch (i) {	//명령어 체크
				case -1:	//일반 채팅
					break;
				case 1:		//종료
					if (user.state == S_WAIT) {
						user_list[u_i].state = S_EMPTY;

						strcpy(str, "접속 종료.\n");

						// 송신 //
						memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
						ioInfo->wsaBuf.buf = str;
						ioInfo->wsaBuf.len = strlen(str);
						ioInfo->rwMode = WRITE;
						WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
						//------//
						closesocket(sock);
						free(handleInfo); free(ioInfo);
						continue;

						printf("[%s] 접속 종료\n", user.name);
					}
					else
						sprintf(str, "대기실에서 종료 가능, 채팅중이라면 !E, !e로 채팅 종료.\n");

					break;

				case 2:		//사용자리스트
					if (user.state == S_WAIT) {	// 대기실
						strcpy(str, "");
						for (i = 0; i < MAX_CLI; i++) {
							j = user_list[i].state;
							switch (j) {
							case S_WAIT:
								sprintf(str, "%s이름 : [%s] , 상태 : 대기실\n", str, user_list[i].name);
								break;
							case S_RECV:
								sprintf(str, "%s이름 : [%s] , 상태 : 채팅요청수신\n", str, user_list[i].name);
								break;
							case S_SEND:
								sprintf(str, "%s이름 : [%s] , 상태 : 채팅요쳥송신\n", str, user_list[i].name);
								break;
							case S_CHAT:
								sprintf(str, "%s이름 : [%s] , 상태 : 채팅중(ch:%d)\n", str, user_list[i].name, user_list[i].room);
								break;
							default:
								break;
							}
						}
						printf("list to [%s]\n", user.name);
					}
					else if (user.state == S_CHAT) {	// 채팅중
						j = 1;
						for (i = 0; i < MAX_CLI; i++) {
							strcpy(str, "\t< ch.%d 참여자 리스트 >\n");
							if (user_list[i].room == user.room)
								sprintf(str, "%s%d. [%s]",str, j++, user_list[i].name);
						}
						printf("list to [%s]\n", user.name);
					}
					else
						sprintf(str, "대기실, 채팅중일때 사용 가능.\n");

					// 송신 //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 3:		//채팅방 시작
					if (user.state == S_WAIT) {
						i = r_open();
						user.state = S_CHAT;
						user.room = i;
						user_list[u_i] = user;

						sprintf(str, "ch.%d 채팅방 생성!\n",i);
						printf("[%s] open ch.%d\n", user.name,i);
					}
					else
						sprintf(str, "대기실에서 사용 가능.\n");

					// 송신 //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 4:		//채팅요청 수락
					if (user.state == S_RECV) {
						user_list[u_i].state == S_CHAT;
						room_list[user.room]++;
						//방의 인원들에게 알림
						sprintf(str, "[%s]가 입장했습니다.\n", user.name);
						filter_arr = u_state_filter(S_CHAT, user.room);
						for (i = 0; i < MAX_CLI + 1; i++) {
							j = filter_arr[i];
							if (j == -1)
								break;
							memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
							ioInfo->wsaBuf.buf = str;
							ioInfo->wsaBuf.len = strlen(str);
							ioInfo->rwMode = WRITE;
							WSASend(user_list[j].sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
						}
					}
					else
						sprintf(str, "초대를 받았을때만 사용 가능.\n");
					break;

				case 5:		//채팅요청 거절
					if (user.state == S_RECV) {
						user_list[u_i].state == S_WAIT;
						//방의 인원들에게 알림
						sprintf(str, "[%s]가 초대 거절.\n",user.name);
						filter_arr = u_state_filter(S_CHAT, user.room);
						for (i = 0; i < MAX_CLI + 1; i++) {
							j = filter_arr[i];
							if (j == -1)
								break;
							memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
							ioInfo->wsaBuf.buf = str;
							ioInfo->wsaBuf.len = strlen(str);
							ioInfo->rwMode = WRITE;
							WSASend(user_list[j].sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
						}
						user_list[u_i].room == -1;
					}
					else
						sprintf(str, "초대를 받았을때만 사용 가능.\n");
					break;

				case 6:		//채팅 종료
					if (user.state == S_CHAT) {
						user.state = S_WAIT;
						room_list[user.room]--;
						user.room = -1;
						user_list[u_i] = user;
						sprintf(str, "ch.%d 채팅방에서 퇴장!\n", i);
						printf("[%s] exit from ch.%d\n", user.name, i);
					}
					else
						sprintf(str, "대기실에서 사용 가능.\n");

					// 송신 //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 7:		//이름 변경
					strcpy(str, user.name);
					sscanf(buf, "!1%s\n", name);
					i = u_index(name, 1);
					if (i == -1) {
						strcpy(user.name, name);
						strcpy(user_list[u_i].name, name);
						sprintf(str, "[%s]에서 [%s]로 이름 변경\n", str, name);

						puts(str);
					}
					else
						sprintf(str, "!1\n", name);

					// 송신 //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 8:		//채팅방 리스트
					strcpy(str, "");
					for (i = 0; i < MAX_ROOM; i++) {
						if (room_list[i] != 0) {
							sprintf(str, "%s <ch.%d>\n", str, i);
							filter_arr = u_state_filter(S_CHAT, i);
							k = 1;
							for (j = 0; j < MAX_CLI + 1; j++) {
								if (filter_arr[j] == -1)
									break;
								sprintf(str, "%s%d) [%s]\n", str, k++, user_list[filter_arr[j]].name);
							}
						}
					}
					// 송신 //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 9:		//채팅 초대
					if (user.state == S_CHAT)
						sprintf(str, "!2", i);
					else
						sprintf(str, "대기실에서 사용 가능.\n");

					// 송신 //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;

				case 10:	//현재 상태 출력
					i = user.state;
					switch (i) {
					case S_WAIT:
						sprintf(str, "이름 : [%s] , 상태 : 대기실\n", user.name);
						break;
					case S_RECV:
						sprintf(str, "이름 : [%s] , 상태 : 채팅요청수신\n", user.name);
						break;
					case S_SEND:
						sprintf(str, "이름 : [%s] , 상태 : 채팅요쳥송신\n", user.name);
						break;
					case S_CHAT:
						sprintf(str, "이름 : [%s] , 상태 : 채팅중(ch:%d)\n", user.name, user.room);
						break;
					default:
						break;
					}
					break;

				case 11:	//초대 대상 이름 획득, 초대 진행
					sscanf(buf, "!2%s\n", name);
					i = u_index(name, 1);
					if (i == -1)
						sprintf(str, "대상이 없음, 리스트 확인 : !L or !l\n");
					else {
						if (user_list[i].state == S_WAIT) {
							user_list[i].state == S_RECV;
							user_list[i].room == user.room;

							sprintf(str, "!3[%s]가 채팅방 ch.%d로 초대했습니다. (수락: !Y,!y)(거절: !N,!n)\n", name, user.room);
							// 송신 //
							memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
							ioInfo->wsaBuf.buf = str;
							ioInfo->wsaBuf.len = strlen(str);
							ioInfo->rwMode = WRITE;
							WSASend(user_list[i].sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
							//------//

							sprintf(str, "[%s]를 ch.%d 로 초대했습니다.\n",name,user.room);
						}
						else {
							if (target.state == S_RECV)
								sprintf(str, "대상이 먼저 초대받음\n");
							else if (target.state == S_CHAT)
								sprintf(str, "대상이 채팅중\n");
							else
								sprintf(str, "대상의 상태가 확인 안됨\n");
						}
					}

					// 송신 //
					memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
					ioInfo->wsaBuf.buf = str;
					ioInfo->wsaBuf.len = strlen(str);
					ioInfo->rwMode = WRITE;
					WSASend(sock, &(ioInfo->wsaBuf), 1, NULL, 0, &(ioInfo->overlapped), NULL);
					//------//
					break;
				}
			//-------------//

			//	유저의 다음 요청	//
			ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = BUF_SIZE;
			ioInfo->wsaBuf.buf = ioInfo->buffer;
			ioInfo->rwMode = READ;
			WSARecv(sock, &(ioInfo->wsaBuf),
				1, NULL, &flags, &(ioInfo->overlapped), NULL);
			//----------------------//
		}
		else {
			puts("message sent!");
			free(ioInfo);
		}
	}
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
			for (i = 0; i < user_num; i++) {
				if (strcmp(user_list[i].name, (char*)data) == 0)
					return i;
			}
			return -1; // 일치되는값 없음
		case 2:		//sock
			for (i = 0; i < user_num; i++) {
				if (user_list[i].sock == (SOCKET*)data)
					return i;
			}	
			return -1; // 일치되는값 없음
		default :
			return -2; // 타겟없음
	}
}

int u_in(USR usr) {
	int i;

	for (i = 0; i < user_num; i++) {
		if (user_list[i].state == S_EMPTY) {
			user_list[i] = usr;
			return i;
		}
	}
	return -1;
}
int u_out(USR usr) {
	int i;

	for (i = 0; i < user_num; i++) {
		if (user_list[i].sock == usr.sock) {
			user_list[i].state = S_EMPTY;
			return i;
		}
	}
	return -1;
}

// 상태 체크, room은 -1을 넣으면 상관 안함, return은 인덱스 배열로 주며 끝엔 -1이 들어가 있음
int* u_state_filter(int state, int room) { 
	int i,num=0;
	int arr[MAX_CLI+1];

	arr[0] = -1;
	if (room == -1) {
		for (i = 0; i < user_num; i++)
			if (user_list[i].state == state)
				arr[num++] = i;
	}
	else {
		for (i = 0; i < user_num; i++)
			if (user_list[i].state == state && user_list[i].room == room)
				arr[num++] = i;
	}
	arr[num + 1] = -1;
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
	채팅 초대				:	!I or !i	9	//채팅방에서 가능	클라이언트에게 명령(!3!text)
	현재 상태 출력			:	!S or !s	10	//전부 사용, 현재 상황 출력
	초대 대상 획득			:	!2name!room		11	//초대요청후 들어온 이름을 획득. 이것을 보내는것은 서버쪽에서 클라쪽으로 명령어(!2!room)를 보낸것, 초대는 대기중인 상대만 가능
	-1은 명령이 아닌걸로 판단 , 일반 대화는 명령 (!0)
*/
int opr_check(char* buf) {
	if (buf[0] != '!') return -1;

	if (strcmp(buf, "!Q") == 0 || strcmp(buf, "!q") == 0)
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
	else if (buf[1] == '1')	// !exist
		return 7;
	else if (strcmp(buf, "!C") == 0 || strcmp(buf, "!c") == 0)
		return 8;
	else if (strcmp(buf, "!I") == 0 || strcmp(buf, "!i") == 0)
		return 9;
	else if (strcmp(buf, "!S") == 0 || strcmp(buf, "!s") == 0)
		return 10;
	else if (buf[1] == '2')	// !target!room
		return 11;
	else
		return -1;
}
int r_open() {
	int i;

	for(i=0;i< MAX_ROOM;i++)
		if (room_list[i] == 0) {
			room_list[i] == 1;
			return i;
		}
	return -1;
}