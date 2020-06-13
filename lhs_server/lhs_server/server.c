#include <stdio.h>
#include <stdlib.h>
#include <process.h>
#include <winsock2.h>
#include <string.h>
#include <windows.h>

#define BUF_SIZE 1024
#define NAME_SIZE 30
#define DEF_PORT 50000
#define MAX_CLI 20
#define MAX_ROOM 11
#define READ	3
#define	WRITE	5

#define ST_WAIT 0
#define ST_CHAT 1

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
	int room;
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

DWORD WINAPI EchoThreadMain(LPVOID CompletionPortIO);
void ErrorHandling(char* message);

int u_index(USR usr, int target);
int u_in(USR usr);
int u_out(USR usr); 
int* u_state_filter(int state, int room);
int opr_check(char* buf);
int r_open();
int r_close(int index);

int user_num = 0;
USR user_list[MAX_CLI];
int room_list[MAX_ROOM];

int main(int argc, char* argv[]) {
	WSADATA wsaData;
	SOCKET hServSock;
	SOCKADDR_IN servAdr;
	LPPER_IO_DATA ioInfo;
	LPPER_HANDLE_DATA handleInfo;

	HANDLE hComPort;
	SYSTEM_INFO sysInfo;

	int recvBytes, i, flags = 0;

	int i;
	char c;

	//	init	//
	if (argc != 2) {
		printf("Usage: %s <port>, if you want default port <%d>, type 'Y'or'y' : ", argv[0], DEF_PORT);
		scanf("%c",&c);
		if (c == 'Y' || c == 'y') {
			printf("server port : %d", DEF_PORT);
			g_port = DEF_PORT;
		}
		else 
			exit(1);
	}
	else
		g_port = argv[1];

	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		ErrorHandling("WSAStartup() error!");

	for (i = 0; i < MAX_ROOM; i++)
		room_list[i] = 0;
	//----------//

	//	server socket add	//
	hServSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);

	memset(&servAdr, 0, sizeof(servAdr));
	servAdr.sin_family = AF_INET;
	servAdr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAdr.sin_port = htons(atoi(argv[1]));

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

		hClntSock = accept(hServSock, (SOCKADDR*)&clntAdr, &addrLen);// accept ���

		//������ ������ �Ѱ���
		handleInfo = (LPPER_HANDLE_DATA)malloc(sizeof(PER_HANDLE_DATA));
		handleInfo->hClntSock = hClntSock;
		memcpy(&(handleInfo->clntAdr), &clntAdr, addrLen);
		//user�� ���



		CreateIoCompletionPort((HANDLE)hClntSock, hComPort, (DWORD)handleInfo, 0);
		// hClntSock�� hComPort ����,handleInfo�� ���� ���� �Ѱ���

		//	�Ѱ��� ������ �ۼ�	//
		ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
		memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
		ioInfo->wsaBuf.len = BUF_SIZE;		//wsaBuf�� ���̿� BUF_SIZE
		ioInfo->wsaBuf.buf = ioInfo->buffer;//wsabuf�� ���ۿ� ����ü�� ���� ����
		ioInfo->rwMode = READ;				//ó�� ������ �б�� ����
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
	LPPER_HANDLE_DATA handleInfo; // Ŭ���̾�Ʈ�� ���ϰ� �ּ� ����
	LPPER_IO_DATA ioInfo; // Ŭ���̾�Ʈ �������� ����
	DWORD flags = 0;

	while (1) {
		//�̺�Ʈ �߻��Ѱ��� cp���� ���� ���ڵ鿡 ����
		GetQueuedCompletionStatus(hComPort, &bytesTrans, //complition port�� �Ѱ���, �ۼ��� ������ ũ������
			(LPDWORD)&handleInfo, (LPOVERLAPPED*)&ioInfo, INFINITE); //hComPort������  handleInfo, ioInfo�� ����,���ð� ����
		sock = handleInfo->hClntSock; // sock�� ��� Ŭ���̾�Ʈ ���� ���
		
		//	body	//
		if (ioInfo->rwMode == READ) {
			puts("message received!");
			if (bytesTrans == 0)    // EOF ���� ��
			{
				closesocket(sock);
				free(handleInfo); free(ioInfo);
				continue;
			}
			///
			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = bytesTrans;
			ioInfo->rwMode = WRITE;
			WSASend(sock, &(ioInfo->wsaBuf),		// Ŭ�󿡰� �����ϴ� �κ�, ���⼭ ��
				1, NULL, 0, &(ioInfo->overlapped), NULL);

			ioInfo = (LPPER_IO_DATA)malloc(sizeof(PER_IO_DATA));
			memset(&(ioInfo->overlapped), 0, sizeof(OVERLAPPED));
			ioInfo->wsaBuf.len = BUF_SIZE;
			ioInfo->wsaBuf.buf = ioInfo->buffer;
			ioInfo->rwMode = READ;
			WSARecv(sock, &(ioInfo->wsaBuf),
				1, NULL, &flags, &(ioInfo->overlapped), NULL);
			///
		}
		else {
			puts("message sent!");
			free(ioInfo);
		}
		//�޽��� ������ ��ɾ����� �ľ�(!)
			// ���� ��ɾ� ����ġ �Լ� �����
			//����� ����Ʈ���� Ż����Ű��
		//��ɾ� �ƴϸ� ���°� ä�û������� �ľ�
			//ä�û��¸� Ŭ���̾�Ʈ�� ���� �ֵ鳢�� ������
			//ä�û��� �ƴϸ� �����޽���
		//
		//----------//
	}
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
int u_index(USR usr, int target) {
	int i;

	switch (target) {
		case 1:		//name
			for (i = 0; i < user_num; i++) {
				if (strcmp(user_list[i].name, usr.name) == 0)
					return i;
			}
			return -1; // ��ġ�Ǵ°� ����
		case 2:		//sock
			for (i = 0; i < user_num; i++) {
				if (user_list[i].sock == usr.sock)
					return i;
			}	
			return -1; // ��ġ�Ǵ°� ����
		default :
			return -2; // Ÿ�پ���
	}
}

int u_in(USR usr) {
	int i;

	for (i = 0; i < user_num; i++) {
		if (user_list[i].state == -1) {
			user_list[i] = usr;
			return 0;
		}
	}
	return -1;
}
int u_out(USR usr) {
	int i;

	for (i = 0; i < user_num; i++) {
		if (user_list[i].sock == usr.sock) {
			user_list[i].state = -1;
			return 0;
		}
	}
	return -1;
}

// ���� üũ, room�� -1�� ������ ��� ����, return�� �ε��� �迭�� �ָ� ���� -1�� �� ����
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
	����			:	!Q or !q	1
	����� ����Ʈ	:	!L or !l	2
	ä�� ��û		:	!R or !r	3	
	����			:	!Y or !y	4
	����			:	!N or !n	5
	ä�� ����		:	!E or !e	6
	-1 : �� ����� �ƴѰɷ� �Ǵ�
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
int r_close(int index) {
	if (room_list[index] == 1)
		room_list[index] == 0;
	else
		return -1;
	return 0;
}