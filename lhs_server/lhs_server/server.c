// KNU 2016115743 ���Ѽ�
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
#define SEV_NAME "<����>"
#define DEF_PORT 50000
#define MAX_CLI 20
#define MAX_ROOM 11
#define READ	3
#define	WRITE	5

#define	R_EMPTY	-1

//state ����//
#define S_EMPTY -1
#define S_WAIT	0
#define S_RECV	1
#define S_SEND	2
#define S_CHAT	3
//----------//

//	Ŭ��� ���	//
#define SC_CHAT		"!7"
#define SC_EXIST	"!1"
#define	SC_ITARGET	"!2"
#define	SC_INVITE	"!3!"
#define SC_SUCCESS	"!4"
#define	SC_FAIL		"!5!"
#define SC_SINFO	"!6!"
//--------------//

/*	Ŭ���̾�Ʈ���� ���	/
	����					:	!Q or !q				1	//���ǿ��� ����
	����� ����Ʈ			:	!L or !l				2	//���ǿ����� ��ü ����Ʈ, ä�����϶��� ���� ����Ʈ.
	ä�ù� ���� 			:	!R or !r				3	//���ǿ��� ����
	ä�� ��û ����			:	!Y or !y				4	//��û ���� �����϶��� ����
	ä�� ��û ����			:	!N or !n				5	//��û ���� �����϶��� ����
	ä�� ����				:	!E or !e				6	//ä���� ���¿��� ����
	�̸�����				:	!CC_NAMEname			7	//Ŭ�󿡼� �۾��� ���� , �̸���ġ�� Ŭ��� ��ɾ�(SC_EXIST)*/
#define	CC_NAME	"!1"
/*	ä�� ����Ʈ ���		:	!C or !c				8	//���ǿ��� ����
	ä�� �ʴ�				:	!I or !i				9	//ä�ù濡�� ����	Ŭ���̾�Ʈ���� ���(SC_ITARGET)
	���� ���� ���			:	!S or !s				10	//���� ���, ���� ��Ȳ ���
	�ʴ� ��� ȹ��			:	!CC_ITARGETname			11	//�ʴ��û�� ���� �̸��� ȹ��.ä�ÿ�û�� ���� ����, �ʴ�� ������� ��븸 ����*/
#define	CC_ITARGET	"!2"	
//	-1�� ����� �ƴѰɷ� �Ǵ� , �Ϲ� ��ȭ�� ��� (!CC_CHAT)*/
#define	CC_CHAT	"!7"
//----------------------//

typedef struct user_data {
	// unique
	char name[NAME_SIZE];
	SOCKET sock;

	//non_unique
	int state; 
	int room; // -1�� ����, �������϶��� ��û���� room�� ǥ��
} USR;

typedef struct    // socket info
{
	SOCKET hClntSock;
	SOCKADDR_IN clntAdr;
} PER_HANDLE_DATA, * LPPER_HANDLE_DATA;

typedef struct    // buffer info
{
	OVERLAPPED overlapped;	//���� �ʿ�
	WSABUF wsaBuf;			//���� �ʿ�
	char buffer[BUF_SIZE];
	int rwMode;    // READ or WRITE
} PER_IO_DATA, * LPPER_IO_DATA;


//	���� ����	//
int g_max_client = MAX_CLI;
int g_port = 0;
CRITICAL_SECTION g_CS;
USR user_list[MAX_CLI];
int room_list[MAX_ROOM]; // ������ ����, 0�̸� ���� ��
//--------------//


DWORD WINAPI ThreadMain(LPVOID CompletionPortIO);
void ErrorHandling(char* message);

int u_index(void* data, int target);
int u_in(USR usr);
int* u_state_filter(int state, int room);
int opr_check(char* buf);
int r_open();
int ser_send(LPPER_IO_DATA* ioInfo, SOCKET* sock, char* str);
int make_serv_opr(char* str, char* text, char* opr) {
	char buf[BUF_SIZE];
	strcpy(buf, text);
	strcpy(str, opr);
	strcat(str, buf);
	return 0;
}
int strcut(char* str, int count) {
	int i;
	for (i = 0; 1; i++) {
		str[i] = str[i + count];
		if (str[i] == '\0')
			break;
	}
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
	gets(str);

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

		hClntSock = accept(hServSock, (SOCKADDR*)&clntAdr, &addrLen);// accept ���
		printf("user accepted\n");
		//������ ������ �Ѱ���
		handleInfo = (LPPER_HANDLE_DATA)malloc(sizeof(PER_HANDLE_DATA));
		handleInfo->hClntSock = hClntSock;
		memcpy(&(handleInfo->clntAdr), &clntAdr, addrLen);
		//user�� ���

		CreateIoCompletionPort((HANDLE)hClntSock, hComPort, (DWORD)handleInfo, 0);
		// hClntSock�� hComPort ����,handleInfo�� ���� ���� �Ѱ���

		evObj = WSACreateEvent();
		ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
		memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
		ioInfo->wsaBuf.len = BUF_SIZE;		//wsaBuf�� ���̿� BUF_SIZE
		ioInfo->wsaBuf.buf = ioInfo->buffer;//wsabuf�� ���ۿ� ����ü�� ���� ����
		ioInfo->rwMode = READ;				//ó�� ������ �б�� ����
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
	LPPER_HANDLE_DATA handleInfo; // Ŭ���̾�Ʈ�� ���ϰ� �ּ� ����
	LPPER_IO_DATA ioInfo; // Ŭ���̾�Ʈ �������� ����
	DWORD flags = 0;
	USR user;

	int u_i, opr, i, j, k;
	//scdOpr : -1�ϋ� ����
	int scdOpr = 0, recvBytes;
	char buf[BUF_SIZE];
	char* parse_buf;
	char str[BUF_SIZE];
	char str2[BUF_SIZE];
	char name[NAME_SIZE];
	int* filter_arr;

	WSAEVENT evObj;

	parse_buf = (char*)malloc(sizeof(char) * BUF_SIZE);
	evObj = WSACreateEvent();
	while (1) {
		//�̺�Ʈ �߻��Ѱ��� cp���� ���� ���ڵ鿡 ����
		 GetQueuedCompletionStatus(hComPort, &bytesTrans, //complition port�� �Ѱ���, �ۼ��� ������ ũ������
			(LPDWORD)&handleInfo, (LPOVERLAPPED*)&ioInfo, INFINITE); //hComPort������  handleInfo, ioInfo�� ����,���ð� ����
		sock = handleInfo->hClntSock; // sock�� ��� Ŭ���̾�Ʈ ���� ���

		//	body	//
		if (ioInfo->rwMode == READ) {
			printf("message received!");
			//������ ���� ����//
			u_i = u_index(&sock, 2);
			if (u_i == -1) {	//�ű�����
				strcpy(user.name, DEF_NAME);
				user.sock = sock;
				user.state = S_WAIT;
				user.room = R_EMPTY;

				u_i = u_in(user);	//����Ʈ�� ���� �߰�
				printf("[%s] connected\n", user.name);
				//if (u_i == -1) {}	// ��������Ʈ�� ������ ����ó��
				//else {}
			}
			else {			//��������
				EnterCriticalSection(&g_CS);
				user = user_list[u_i];
				LeaveCriticalSection(&g_CS);
			}
			if (bytesTrans == 0)    // EOF ���� ��
			{
				EnterCriticalSection(&g_CS);
				user_list[u_i].sock = NULL;
				if (user_list[u_i].state == S_CHAT) 
					room_list[user_list[u_i].room]--;
				user_list[u_i].state = S_EMPTY;
				strcpy(user_list[u_i].name, DEF_NAME);
				LeaveCriticalSection(&g_CS);

				closesocket(sock);
				free(handleInfo); free(ioInfo);
				printf("[%s] disconnected\n", user.name);
				continue;
			}
			//----------------//

			// ���� �޽��� //
			strcpy(buf, ioInfo->wsaBuf.buf);
			printf("msg :%s\n", buf);
			//-------------//

			scdOpr = -2; // �ǹ̾���
			opr = opr_check(buf);
			switch (opr) {	//��ɾ� üũ
			case 0:	//�Ϲ� ä��
				if (user.state == S_WAIT) {
					sprintf(str2, "%d%s : ��ɾ� ����\n", S_WAIT, SEV_NAME);
					make_serv_opr(str, str2, SC_SINFO);
					ser_send(&ioInfo, &sock, str);
					break;
				}
				strcpy(parse_buf, buf);
				strcut(parse_buf, strlen(CC_CHAT));
				sprintf(str, "[%s] : ", user.name);
				strcat(str, parse_buf);
				make_serv_opr(str, str, SC_CHAT);
				EnterCriticalSection(&g_CS);
				filter_arr = u_state_filter(S_CHAT, user.room);
				for (j = 0; j < MAX_CLI + 1; j++) {
					i = filter_arr[j];
					if (i == -1)
						break;
					if (u_i != j) {
						ser_send(&ioInfo, &(user_list[i].sock), str);
					}
				}
				LeaveCriticalSection(&g_CS);
				free(filter_arr);
				break;
			case 1:		//����
				puts(buf);
				if (user.state == S_WAIT) {
					EnterCriticalSection(&g_CS);
					user_list[u_i].sock = NULL;
					if (user_list[u_i].state == S_CHAT)
						room_list[user_list[u_i].room]--;
					user_list[u_i].state = S_EMPTY;
					LeaveCriticalSection(&g_CS);

					closesocket(sock);
					free(handleInfo); free(ioInfo);
					scdOpr = -1;// ��������� ����
					printf("[%s] ���� ����", user.name);
				}
				else {
					sprintf(str2,"%s ���ǿ��� ���� ����, ä�����̶�� !E, !e�� ä�� ����.",SEV_NAME);
					make_serv_opr(str, str2, SC_CHAT);
					// �۽� //
					ser_send(&ioInfo, &sock, str);
					//------//
				}
				break;

			case 2:		//����ڸ���Ʈ
				strcpy(str, "<���� ����Ʈ>\n");
				EnterCriticalSection(&g_CS);
				for (i = 0; i < MAX_CLI; i++) {
					j = user_list[i].state;
					switch (j) {
					case S_WAIT:
						sprintf(str2, "�̸� : [%s] , ���� : ����\n", user_list[i].name);
						strcat(str,str2);
						break;
					case S_RECV:
						sprintf(str2, " �̸� : [%s] , ���� : ä�ÿ�û����\n", user_list[i].name);
						strcat(str, str2);
						break;
					case S_SEND:
						sprintf(str2, "�̸� : [%s] , ���� : ä�ÿ䫊�۽�\n", user_list[i].name);
						strcat(str, str2);
						break;
					case S_CHAT:
						sprintf(str2, "�̸� : [%s] , ���� : ä����(ch:%d)\n", user_list[i].name, user_list[i].room);
						strcat(str, str2);
						break;
					default:
						break;
					}
				}
				make_serv_opr(str, str,SC_CHAT);
				LeaveCriticalSection(&g_CS);
				printf("list to [%s]\n", user.name);

				// �۽� //
				ser_send(&ioInfo, &sock, str);
				//------//
				break;

			case 3:		//ä�ù� ����
				if (user.state == S_WAIT) {
					i = r_open();
					if (i == -1) {
						sprintf(str2, "%s : �ִ� ä�ù� �ʰ�!", SEV_NAME);
						make_serv_opr(str, str2, SC_CHAT);
					}
					else {
						user.state = S_CHAT;
						user.room = i;

						EnterCriticalSection(&g_CS);
						user_list[u_i] = user;
						LeaveCriticalSection(&g_CS);

						sprintf(str2, "%d%s : ch.%d ä�ù� ����!",S_CHAT, SEV_NAME, i);
						make_serv_opr(str, str2, SC_SINFO);
						puts(str);
						ser_send(&ioInfo, &sock, str);
						printf("[%s] open ch.%d\n", user.name, i);
					}
				}
				else {
					strcpy(str, "%s : ä�ù��� ���� ���¿����� ����� �ֽ��ϴ�.\n", SEV_NAME);
					make_serv_opr(str, str, SC_CHAT);
					ser_send(&ioInfo, &sock, str);
				}
				break;

			case 4:		//ä�ÿ�û ����
				if (user.state == S_RECV) {
					EnterCriticalSection(&g_CS);
					user_list[u_i].state = S_CHAT;
					room_list[user.room]++;
					//���� �ο��鿡�� �˸�
					filter_arr = u_state_filter(S_CHAT, user.room);
					for (i = 0; i < MAX_CLI + 1; i++) {
						j = filter_arr[i];
						if (j == -1)
							break;
						if (u_i == j) {
							sprintf(str2, "%d%s : ch.%d�� �����߽��ϴ�.",S_CHAT, SEV_NAME, user.room);
							make_serv_opr(str, str2, SC_SINFO);
						}
						else {
							sprintf(str2, "%s : [%s]�� �����߽��ϴ�.", SEV_NAME, user.name);
							make_serv_opr(str, str2, SC_CHAT);
						}
						ser_send(&ioInfo, &(user_list[j].sock), str);
					}
					LeaveCriticalSection(&g_CS);
					free(filter_arr);
				}
				else {
					/*strcpy(str, "��û�� ���;� ��������\n");
					ser_send(&ioInfo, &sock, str);*/
				}
				break;

			case 5:		//ä�ÿ�û ����
				if (user.state == S_RECV) {
					EnterCriticalSection(&g_CS);
					user_list[u_i].state = S_WAIT;
					//���� �ο��鿡�� �˸�
					filter_arr = u_state_filter(S_CHAT, user.room);
					for (i = 0; i < MAX_CLI + 1; i++) {
						j = filter_arr[i];
						if (j == -1)
							break;
						if (u_i == j) {
							sprintf(str2, "%d%s : �ʴ� ����.",S_WAIT, SEV_NAME);
							make_serv_opr(str, str2, SC_SINFO);
						}
						else {
							sprintf(str2, "%s : [%s]�� �ʴ� ����.", SEV_NAME, user.name);
							make_serv_opr(str, str2, SC_CHAT);
						}
						ser_send(&ioInfo, &(user_list[j].sock), str);
					}
					user_list[u_i].room = R_EMPTY;
					LeaveCriticalSection(&g_CS);
					free(filter_arr);
				}
				else {//�Ϲ� ä������ �ѱ�}
					break;

			case 6:		//ä�� ����
				if (user.state == S_CHAT) {
					i = user.room;
					EnterCriticalSection(&g_CS);
					//���� �ο��鿡�� �˸�
					filter_arr = u_state_filter(S_CHAT, user.room);
					for (i = 0; i < MAX_CLI + 1; i++) {
						j = filter_arr[i];
						if (j == -1)
							break;
						if (u_i == j) {
							sprintf(str2, "%d%s : ch.%d ä�ù濡�� ����!", S_WAIT, SEV_NAME, user.room);
							make_serv_opr(str, str2, SC_SINFO);
						}
						else {
							sprintf(str2, "%s : [%s]�� �����߽��ϴ�.", SEV_NAME, user.name);
							make_serv_opr(str, str2, SC_CHAT);
						}
						ser_send(&ioInfo, &(user_list[j].sock), str);
					}
					user.state = S_WAIT;
					room_list[user.room]--;
					user.room = R_EMPTY;
					user_list[u_i] = user;
					LeaveCriticalSection(&g_CS);
					printf("[%s] exit from ch.%d\n", user.name, i);
				}
				else {}
				//------//
				break;

			case 7:		//�̸� ����
				strcpy(str, user.name);
				strcpy(parse_buf, buf);
				strcut(parse_buf, strlen(CC_NAME));
				if (parse_buf != NULL) {
					i = u_index(parse_buf, 1);
					if (i == -1) {
						strcpy(name, parse_buf);
						strcpy(user.name, name);
						EnterCriticalSection(&g_CS);
						strcpy(user_list[u_i].name, name);
						LeaveCriticalSection(&g_CS);
						sprintf(str2, "%s : [%s]���� [%s]�� �̸� ����", SEV_NAME, str, name);
						make_serv_opr(str, str2, SC_CHAT);
					}
					else
						sprintf(str, "%s", SC_EXIST); //Ŭ�󿡼� ó��
				}
				else
					sprintf(str, "%s", SC_EXIST); //Ŭ�󿡼� ó��

				// �۽� //
				puts(str);
				ser_send(&ioInfo, &sock, str);
				//------//
				break;

			case 8:		//ä�ù� ����Ʈ
				strcpy(str, "<ä�ù� ����Ʈ>\n");
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
				// �۽� //
				make_serv_opr(str, str, SC_CHAT);
				ser_send(&ioInfo, &sock, str);
				//------//
				break;

			case 9:		//ä�� �ʴ�
				if (user.state == S_CHAT)
					sprintf(str, SC_ITARGET);
				else {
					sprintf(str2, "%s : ä�ù濡�� ��� ����.", SEV_NAME);
					make_serv_opr(str, str2, SC_CHAT);
				}

				// �۽� //
				ser_send(&ioInfo, &sock, str);
				//------//
				break;

			case 10:	//���� ���� ���
				i = user.state;
				switch (i) {
				case S_WAIT:
					sprintf(str2, "<������>\n�̸� : [%s] , ���� : ����", user.name);
					make_serv_opr(str, str2, SC_CHAT);
					break;
				case S_RECV:
					sprintf(str2, "<������>\n�̸� : [%s] , ���� : ä�ÿ�û����", user.name);
					make_serv_opr(str, str2, SC_CHAT);
					break;
				case S_SEND:
					sprintf(str2, "<������>\n�̸� : [%s] , ���� : ä�ÿ䫊�۽�", user.name);
					make_serv_opr(str, str2, SC_CHAT);
					break;
				case S_CHAT:
					sprintf(str2, "<������>\n�̸� : [%s] , ���� : ä����(ch:%d)", user.name, user.room);
					make_serv_opr(str, str2, SC_CHAT);
					break;
				default:
					sprintf(str2, "<������>\n�̸� : [%s] , ���� : �� �� ���� ", user.name);
					make_serv_opr(str, str2, SC_CHAT);
					break;
				}
				puts(str);
				ser_send(&ioInfo, &sock, str);
				break;

			case 11:	//�ʴ� ��� �̸� ȹ��, �ʴ� ����
				sprintf(str2, "%s%%s", SC_ITARGET);
				strcpy(parse_buf, buf);
				strcut(parse_buf, strlen(SC_ITARGET));
				strcpy(name, parse_buf);
				i = u_index(name, 1);
				if (i == -1) {
					sprintf(str2, "%s : ����� ����, ����Ʈ Ȯ�� <!L ,!l>", SEV_NAME);
					make_serv_opr(str, str2, SC_CHAT);
				}
				else {
					EnterCriticalSection(&g_CS);
					if (user_list[i].state == S_WAIT) {
						user_list[i].state = S_RECV;
						user_list[i].room = user.room;

						sprintf(str, "%s%s : [%s]�� ä�ù� ch.%d�� �ʴ��߽��ϴ�.", SC_INVITE, SEV_NAME, user.name, user.room);
						// �۽� //
						ser_send(&ioInfo, &(user_list[i].sock), str);
						//------//
						//���� �ο��鿡�� �˸�
						filter_arr = u_state_filter(S_CHAT, user.room);
						for (i = 0; i < MAX_CLI + 1; i++) {
							j = filter_arr[i];
							if (j == -1)
								break;
							sprintf(str2, "%s : [%s]�� ch.%d �� �ʴ��߽��ϴ�.", SEV_NAME, name, user.room);
							make_serv_opr(str, str2, SC_CHAT);
							ser_send(&ioInfo, &(user_list[j].sock), str);
						}
					}
					else {
						if (user_list[i].state == S_RECV) {
							sprintf(str2, "%s : ����� ���� �ʴ����", SEV_NAME);
							make_serv_opr(str, str2, SC_CHAT);
						}
						else if (user_list[i].state == S_CHAT){
							sprintf(str2, "%s : ����� ä����", SEV_NAME);
							make_serv_opr(str, str2, SC_CHAT);
						}
						else {
							sprintf(str2, "%s : ����� ���°� Ȯ�� �ȵ�", SEV_NAME);
							make_serv_opr(str, str2, SC_CHAT);
						}
						// �۽� //
						ser_send(&ioInfo, &sock, str);
						//------//
					}
					LeaveCriticalSection(&g_CS);
				}
				}//q//
				break;
			}
			if (scdOpr == -1)// ��������� ���� 261
				continue;
			//-------------//

			//	������ ���� ��û	//
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
			//free(ioInfo);
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
	target ���� 
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
			return -1; // ��ġ�Ǵ°� ����
		case 2:		//sock
			EnterCriticalSection(&g_CS);
			for (i = 0; i < MAX_CLI; i++) {
				if (user_list[i].sock == *((SOCKET*)data)) {
					LeaveCriticalSection(&g_CS);
					return i;
				}
			}
			LeaveCriticalSection(&g_CS);
			return -1; // ��ġ�Ǵ°� ����
		default:
			return -2; // Ÿ�پ���
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

// ���� üũ, room�� -1�� ������ ��� ����, return�� �ε��� �迭�� �ָ� ���� -1�� �� ����
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
		printf("filter \n");
		for (i = 0; i < MAX_CLI; i++)
			if (user_list[i].state == state && user_list[i].room == room) {
				arr[num++] = i;
				printf("user : %s %d, %d\n", user_list[i].name, user_list[i].state, user_list[i].room);
			}
	}
	arr[num] = -1;
	return arr;
}

/*
	����					:	!Q or !q	1	//���ǿ��� ����
	����� ����Ʈ			:	!L or !l	2	//���ǿ����� ��ü ����Ʈ, ä�����϶��� ���� ����Ʈ.
	ä�ù� ���� 			:	!R or !r	3	//���ǿ��� ����
	ä�� ��û ����			:	!Y or !y	4	//��û ���� �����϶��� ����
	ä�� ��û ����			:	!N or !n	5	//��û ���� �����϶��� ����
	ä�� ����				:	!E or !e	6	//ä���� ���¿��� ����
	�̸�����				:	!1name		7	//Ŭ�󿡼� �۾��� ���� , �̸���ġ�� Ŭ��� ��ɾ�(!1)
	ä�� ����Ʈ ���		:	!C or !c	8	//���ǿ��� ����
	ä�� �ʴ�				:	!I or !i	9	//ä�ù濡�� ����	Ŭ���̾�Ʈ���� ���(!2)
	���� ���� ���			:	!S or !s	10	//���� ���, ���� ��Ȳ ���
	�ʴ� ��� ȹ��			:	!2name!room		11	//�ʴ��û�� ���� �̸��� ȹ��. �̰��� �����°��� �����ʿ��� Ŭ�������� ��ɾ�(!2)�� ������, �ʴ�� ������� ��븸 ����
	-1�� ����� �ƴѰɷ� �Ǵ� , �Ϲ� ��ȭ�� ��� (!0)
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
	WSAEVENT evObj;
	evObj = WSACreateEvent();
	memset(&((*ioInfo)->overlapped), 0, sizeof(OVERLAPPED));
	(*ioInfo)->wsaBuf.buf = str;
	(*ioInfo)->wsaBuf.len = strlen(str)+1;
	(*ioInfo)->rwMode = WRITE;
	if (WSASend(*sock, &((*ioInfo)->wsaBuf), 1, NULL, 0, &((*ioInfo)->overlapped), NULL) == SOCKET_ERROR){
		if (WSAGetLastError() == WSA_IO_PENDING){
			puts("Background data send");
			WSAWaitForMultipleEvents(1, &evObj, TRUE, WSA_INFINITE, FALSE);
			WSAGetOverlappedResult(*sock, &((*ioInfo)->overlapped), NULL, FALSE, NULL);
		}
		else{
			ErrorHandling("WSASend() error");
		}
	}
	return 0;
}